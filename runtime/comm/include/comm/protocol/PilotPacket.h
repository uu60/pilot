#ifndef PILOT_PACKET_H
#define PILOT_PACKET_H

#include <cstdint>
#include <vector>

#include "comm/security/BmtCrypto.h"

struct PilotPacketRequest {
    int tag{};
    int laneId{};
    int bmtRequestCount{};
    std::vector<int64_t> payload;
};

struct PilotPacketEnvelope {
    int srcRank{};
    int dstRank{};
    int tag{};
    int laneId{};
    std::vector<int64_t> payload;
    std::vector<EncryptedBitwiseBmt> encryptedTrailer;
};

class PilotPacket {
public:
    static constexpr int64_t ENVELOPE_MAGIC = 0x5041525345434550LL; // "PARSECEP" marker
    static constexpr int64_t REQUEST_MAGIC = 0x5041525352455154LL;  // "PARSREQT" marker
    static constexpr uint64_t ENCRYPTED_TRAILER_FLAG = uint64_t{1} << 63;

    static std::vector<int64_t> encodeRequest(const PilotPacketRequest &packet);

    static PilotPacketRequest decodeRequest(const std::vector<int64_t> &words);

    static std::vector<int64_t> encodeEnvelope(const PilotPacketEnvelope &packet);

    static PilotPacketEnvelope decodeEnvelope(const std::vector<int64_t> &words);
};

#endif
