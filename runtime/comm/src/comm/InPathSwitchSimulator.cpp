#include "comm/InPathSwitchSimulator.h"

#include "comm/Comm.h"
#include "conf/Conf.h"
#include "intermediate/IntermediateDataSupport.h"
#include "utils/Log.h"

#include <algorithm>
#include <mpi.h>
#include <map>
#include <mutex>
#include <random>
#include <stdexcept>

namespace {
struct PendingTrailerShares {
    std::vector<RawBitwiseBmt> shares[2];
    bool delivered[2]{false, false};
};

std::mutex g_switchMutex;
std::map<std::pair<int, int>, PendingTrailerShares> g_pendingShares;
std::mt19937_64 g_rng{0x504152534543ULL};

std::vector<RawBitwiseBmt> generateShareBundle(int laneId, int tag, int dstRank, int count) {
    const int shareIndex = dstRank == Comm::SERVER0_RANK ? 0 : 1;
    const int otherIndex = 1 - shareIndex;
    const auto key = std::make_pair(laneId, tag);
    std::lock_guard<std::mutex> lock(g_switchMutex);
    auto &pending = g_pendingShares[key];
    if (pending.shares[0].empty() && pending.shares[1].empty()) {
        count = std::max(0, count);
        pending.shares[0].reserve(count);
        pending.shares[1].reserve(count);
        for (int i = 0; i < count; ++i) {
            const int64_t a = static_cast<int64_t>(g_rng());
            const int64_t b = static_cast<int64_t>(g_rng());
            const int64_t c = a & b;

            RawBitwiseBmt s0{
                static_cast<int64_t>(g_rng()),
                static_cast<int64_t>(g_rng()),
                static_cast<int64_t>(g_rng())
            };
            RawBitwiseBmt s1{s0.a ^ a, s0.b ^ b, s0.c ^ c};
            pending.shares[0].push_back(s0);
            pending.shares[1].push_back(s1);
        }
    }

    auto out = pending.shares[shareIndex];
    pending.delivered[shareIndex] = true;
    if (pending.delivered[shareIndex] && pending.delivered[otherIndex]) {
        g_pendingShares.erase(key);
    }
    return out;
}
}

bool InPathSwitchSimulator::enabled() {
    return Conf::ENABLE_IN_PATH_BMT_SWITCH;
}

bool InPathSwitchSimulator::isSwitchRank() {
    return enabled() && Comm::rank() == Conf::IN_PATH_SWITCH_RANK;
}

bool InPathSwitchSimulator::shouldRouteServerTraffic() {
    return enabled() && Comm::isServerRank(Comm::rank());
}

std::vector<int64_t> InPathSwitchSimulator::makeSwitchRequest(int tag, int bmtRequestCount,
                                                              const std::vector<int64_t> &payload) {
    const int laneId = IntermediateDataSupport::currentLane();
    std::vector<int64_t> request;
    request.reserve(5 + payload.size());
    request.push_back(REQUEST_MAGIC);
    request.push_back(tag);
    request.push_back(laneId);
    request.push_back(std::max(0, bmtRequestCount));
    request.push_back(static_cast<int64_t>(payload.size()));
    request.insert(request.end(), payload.begin(), payload.end());
    return request;
}

void InPathSwitchSimulator::unpackSwitchRequest(const std::vector<int64_t> &request,
                                                int &laneId,
                                                int &bmtRequestCount,
                                                std::vector<int64_t> &payload) {
    if (request.size() >= 5 && request[0] == REQUEST_MAGIC) {
        laneId = static_cast<int>(request[2]);
        bmtRequestCount = static_cast<int>(request[3]);
        const auto payloadSize = static_cast<size_t>(request[4]);
        if (request.size() != 5 + payloadSize) {
            throw std::runtime_error("Invalid in-path switch request size.");
        }
        payload.assign(request.begin() + 5, request.end());
        return;
    }

    // Backward-compatible path for messages sent before request wrapping.
    laneId = 0;
    bmtRequestCount = 0;
    payload = request;
}

std::vector<int64_t> InPathSwitchSimulator::makeEnvelope(int srcRank, int dstRank, int tag, int laneId,
                                                         const std::vector<int64_t> &payload,
                                                         const std::vector<RawBitwiseBmt> &trailer) {
    const int trailerWords = static_cast<int>(trailer.size()) * 3;
    std::vector<int64_t> envelope;
    envelope.reserve(7 + payload.size() + static_cast<size_t>(trailerWords));
    envelope.push_back(ENVELOPE_MAGIC);
    envelope.push_back(static_cast<int64_t>(payload.size()));
    envelope.push_back(trailerWords);
    envelope.push_back(srcRank);
    envelope.push_back(dstRank);
    envelope.push_back(tag);
    envelope.push_back(laneId);
    envelope.insert(envelope.end(), payload.begin(), payload.end());

    for (const auto &bmt: trailer) {
        envelope.push_back(bmt.a);
        envelope.push_back(bmt.b);
        envelope.push_back(bmt.c);
    }
    return envelope;
}

std::vector<int64_t> InPathSwitchSimulator::forwardRequest(int srcRank, int tag,
                                                           const std::vector<int64_t> &request) {
    if (!Comm::isServerRank(srcRank)) {
        throw std::runtime_error("Switch simulator only forwards party 0/1 traffic.");
    }
    const int dstRank = srcRank == Comm::SERVER0_RANK ? Comm::SERVER1_RANK : Comm::SERVER0_RANK;
    int laneId = 0;
    int bmtRequestCount = 0;
    std::vector<int64_t> payload;
    unpackSwitchRequest(request, laneId, bmtRequestCount, payload);
    auto trailer = generateShareBundle(laneId, tag, dstRank, bmtRequestCount);
    return makeEnvelope(srcRank, dstRank, tag, laneId, payload, trailer);
}

std::vector<int64_t> InPathSwitchSimulator::unpackPayload(const std::vector<int64_t> &envelope) {
    if (envelope.size() < 7 || envelope[0] != ENVELOPE_MAGIC) {
        throw std::runtime_error("Invalid in-path switch envelope.");
    }
    const auto payloadSize = static_cast<size_t>(envelope[1]);
    const auto trailerSize = static_cast<size_t>(envelope[2]);
    if (envelope.size() != 7 + payloadSize + trailerSize) {
        throw std::runtime_error("In-path switch envelope size mismatch.");
    }
    if (trailerSize % 3 != 0) {
        throw std::runtime_error("In-path switch BMT trailer size mismatch.");
    }
    const int laneId = static_cast<int>(envelope[6]);
    std::vector<RawBitwiseBmt> trailer;
    trailer.reserve(trailerSize / 3);
    auto trailerIt = envelope.begin() + 7 + payloadSize;
    for (size_t i = 0; i < trailerSize; i += 3) {
        trailer.push_back(RawBitwiseBmt{*(trailerIt + i), *(trailerIt + i + 1), *(trailerIt + i + 2)});
    }
    if (!trailer.empty()) {
        IntermediateDataSupport::offerRawBitwiseBmts(laneId, trailer);
    }
    return std::vector<int64_t>(envelope.begin() + 7, envelope.begin() + 7 + payloadSize);
}

void InPathSwitchSimulator::sendShutdown() {
    if (!shouldRouteServerTraffic()) {
        return;
    }
    std::vector<int64_t> stop{SHUTDOWN_MAGIC};
    MPI_Send(stop.data(), static_cast<int>(stop.size()), MPI_INT64_T,
             Conf::IN_PATH_SWITCH_RANK, CONTROL_TAG, MPI_COMM_WORLD);
}

void InPathSwitchSimulator::run() {
    if (!isSwitchRank()) {
        return;
    }
    if (Conf::COMM_TYPE != Conf::MPI) {
        throw std::runtime_error("In-path switch simulator currently supports MPI only.");
    }

    Log::i("In-path BMT switch simulator started.");
    int shutdowns = 0;
    while (shutdowns < 2) {
        MPI_Status status{};
        MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

        int count = 0;
        MPI_Get_count(&status, MPI_INT64_T, &count);
        std::vector<int64_t> request(count);
        MPI_Recv(request.data(), count, MPI_INT64_T, status.MPI_SOURCE, status.MPI_TAG,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        if (status.MPI_TAG == CONTROL_TAG && request.size() == 1 && request[0] == SHUTDOWN_MAGIC) {
            ++shutdowns;
            continue;
        }

        const int dstRank = status.MPI_SOURCE == Comm::SERVER0_RANK ? Comm::SERVER1_RANK : Comm::SERVER0_RANK;
        auto envelope = forwardRequest(status.MPI_SOURCE, status.MPI_TAG, request);
        MPI_Send(envelope.data(), static_cast<int>(envelope.size()), MPI_INT64_T,
                 dstRank, status.MPI_TAG, MPI_COMM_WORLD);
    }
    Log::i("In-path BMT switch simulator stopped.");
}
