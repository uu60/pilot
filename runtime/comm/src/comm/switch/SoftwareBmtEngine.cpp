#include "comm/switch/SoftwareBmtEngine.h"

#include "comm/Comm.h"
#include "comm/protocol/InPathBmtProtocol.h"
#include "comm/security/BmtCrypto.h"
#include "comm/timing/InPathTimingModel.h"
#include "intermediate/item/BitwiseBmt.h"

#include <algorithm>
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

std::vector<int64_t> SoftwareBmtEngine::processRequest(
        int srcRank, int tag, const std::vector<int64_t> &request) {
    if (!Comm::isServerRank(srcRank)) {
        throw std::runtime_error("Software BMT engine only processes party 0/1 traffic.");
    }
    const int dstRank = srcRank == Comm::SERVER0_RANK ? Comm::SERVER1_RANK : Comm::SERVER0_RANK;
    auto packet = InPathBmtProtocol::decodeRequest(request);
    const auto shares = generateShareBundle(packet.laneId, tag, dstRank, packet.bmtRequestCount);

    std::vector<EncryptedBitwiseBmt> encryptedTrailer;
    encryptedTrailer.reserve(shares.size());
    for (uint32_t i = 0; i < shares.size(); ++i) {
        encryptedTrailer.push_back(BmtCrypto::encrypt(shares[i], dstRank, packet.laneId, tag, i,
                                                      packet.payload));
    }
    InPathTimingModel::recordRequest(packet.laneId, tag, dstRank,
                                     static_cast<int>(shares.size()), packet.payload.size(),
                                     encryptedTrailer.size() * EncryptedBitwiseBmt::SERIALIZED_SIZE);
    return InPathBmtProtocol::makeEnvelope(srcRank, dstRank, tag, packet.laneId,
                                           packet.payload, encryptedTrailer);
}
