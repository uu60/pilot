#ifndef PILOT_TAP_FRAME_CODEC_H
#define PILOT_TAP_FRAME_CODEC_H

#include "comm/transport/peer/RoutedPeerTransport.h"

#include <array>
#include <cstdint>
#include <vector>

class TapFrameCodec {
public:
    static constexpr uint16_t ETHERTYPE = 0x88B5;
    static constexpr size_t ETHERNET_HEADER_SIZE = 14;

    static std::array<uint8_t, 6> macForRank(int rank);

    static std::vector<uint8_t> encode(const RoutedPeerMessage &message);

    static bool decode(const uint8_t *data, size_t size, RoutedPeerMessage &message);
};

#endif
