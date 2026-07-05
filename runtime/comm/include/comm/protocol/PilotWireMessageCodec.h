#ifndef PILOT_WIRE_MESSAGE_CODEC_H
#define PILOT_WIRE_MESSAGE_CODEC_H

#include "comm/transport/peer/RoutedPeerTransport.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class PilotWireMessageCodec {
public:
    static constexpr uint32_t MAGIC = 0x50494c54; // "PILT"
    static constexpr uint8_t VERSION = 1;
    static constexpr size_t HEADER_SIZE = 32;

    static constexpr uint32_t FLAG_SWITCH_REQUEST = 1u << 0;
    static constexpr uint32_t FLAG_SWITCH_ENVELOPE = 1u << 1;

    static std::vector<uint8_t> encode(const RoutedPeerMessage &message);

    static bool decode(const uint8_t *data, size_t size, RoutedPeerMessage &message);

    static bool payloadBytesFromHeader(const uint8_t *data, size_t size, size_t &payloadBytes);
};

#endif
