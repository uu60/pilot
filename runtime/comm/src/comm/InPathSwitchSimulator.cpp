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
#include <utility>

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
    PilotPacketRequest packet;
    packet.tag = tag;
    packet.laneId = IntermediateDataSupport::currentLane();
    packet.bmtRequestCount = bmtRequestCount;
    packet.payload = payload;
    return PilotPacket::encodeRequest(packet);
}

void InPathSwitchSimulator::unpackSwitchRequest(const std::vector<int64_t> &request,
                                                int &laneId,
                                                int &bmtRequestCount,
                                                std::vector<int64_t> &payload) {
    auto packet = PilotPacket::decodeRequest(request);
    laneId = packet.laneId;
    bmtRequestCount = packet.bmtRequestCount;
    payload = std::move(packet.payload);
}

std::vector<int64_t> InPathSwitchSimulator::makeEnvelope(int srcRank, int dstRank, int tag, int laneId,
                                                         const std::vector<int64_t> &payload,
                                                         const std::vector<RawBitwiseBmt> &trailer) {
    PilotPacketEnvelope packet;
    packet.srcRank = srcRank;
    packet.dstRank = dstRank;
    packet.tag = tag;
    packet.laneId = laneId;
    packet.payload = payload;
    packet.trailer = trailer;
    return PilotPacket::encodeEnvelope(packet);
}

std::vector<int64_t> InPathSwitchSimulator::forwardRequest(int srcRank, int tag,
                                                           const std::vector<int64_t> &request) {
    if (!Comm::isServerRank(srcRank)) {
        throw std::runtime_error("Switch simulator only forwards party 0/1 traffic.");
    }
    const int dstRank = srcRank == Comm::SERVER0_RANK ? Comm::SERVER1_RANK : Comm::SERVER0_RANK;
    auto packet = PilotPacket::decodeRequest(request);
    auto trailer = generateShareBundle(packet.laneId, tag, dstRank, packet.bmtRequestCount);
    return makeEnvelope(srcRank, dstRank, tag, packet.laneId, packet.payload, trailer);
}

std::vector<int64_t> InPathSwitchSimulator::unpackPayload(const std::vector<int64_t> &envelope) {
    auto packet = PilotPacket::decodeEnvelope(envelope);
    if (!packet.trailer.empty()) {
        IntermediateDataSupport::offerRawBitwiseBmts(packet.laneId, packet.trailer);
    }
    return packet.payload;
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
