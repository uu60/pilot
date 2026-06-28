#ifndef PILOT_PACKET_H
#define PILOT_PACKET_H

#include <cstdint>
#include <vector>

#include "intermediate/item/BitwiseBmt.h"

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
    std::vector<RawBitwiseBmt> trailer;
};

class PilotPacket {
public:
    static constexpr int64_t ENVELOPE_MAGIC = 0x5041525345434550LL; // "PARSECEP" marker
    static constexpr int64_t REQUEST_MAGIC = 0x5041525352455154LL;  // "PARSREQT" marker

    static std::vector<int64_t> encodeRequest(const PilotPacketRequest &packet);

    static PilotPacketRequest decodeRequest(const std::vector<int64_t> &words);

    static std::vector<int64_t> encodeEnvelope(const PilotPacketEnvelope &packet);

    static PilotPacketEnvelope decodeEnvelope(const std::vector<int64_t> &words);
};

#endif
