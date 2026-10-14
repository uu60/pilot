#ifndef PILOT_ETHERNET_FRAME_CODEC_H
#define PILOT_ETHERNET_FRAME_CODEC_H

#include "comm/protocol/PilotMessage.h"

#include <array>
#include <cstdint>
#include <vector>

class EthernetFrameCodec {
public:
    static constexpr uint16_t ETHERTYPE = 0x88B5;
    static constexpr size_t ETHERNET_HEADER_SIZE = 14;

    static std::array<uint8_t, 6> macForRank(int rank);

    static std::vector<uint8_t> encode(const PilotMessage &message);

    static bool decode(const uint8_t *data, size_t size, PilotMessage &message);
};

#endif
