#include "comm/transport/peer/TapFrameCodec.h"

#include "comm/protocol/PilotWireMessageCodec.h"

#include <cstring>

namespace {
constexpr uint8_t kPilotMacPrefix[5]{0x02, 0x50, 0x49, 0x4c, 0x4f};

void appendBytes(std::vector<uint8_t> &out, const void *data, size_t size) {
    const auto *bytes = static_cast<const uint8_t *>(data);
    out.insert(out.end(), bytes, bytes + size);
}
}

std::array<uint8_t, 6> TapFrameCodec::macForRank(int rank) {
    std::array<uint8_t, 6> mac{};
    std::memcpy(mac.data(), kPilotMacPrefix, sizeof(kPilotMacPrefix));
    mac[5] = static_cast<uint8_t>(rank & 0xff);
    return mac;
}

std::vector<uint8_t> TapFrameCodec::encode(const RoutedPeerMessage &message) {
    const auto dst = macForRank(message.receiverRank);
    const auto src = macForRank(message.senderRank);
    auto payload = PilotWireMessageCodec::encode(message);

    std::vector<uint8_t> frame;
    frame.reserve(ETHERNET_HEADER_SIZE + payload.size());
    appendBytes(frame, dst.data(), dst.size());
    appendBytes(frame, src.data(), src.size());
    frame.push_back(static_cast<uint8_t>((ETHERTYPE >> 8) & 0xff));
    frame.push_back(static_cast<uint8_t>(ETHERTYPE & 0xff));
    appendBytes(frame, payload.data(), payload.size());
    return frame;
}

bool TapFrameCodec::decode(const uint8_t *data, size_t size, RoutedPeerMessage &message) {
    if (size < ETHERNET_HEADER_SIZE + PilotWireMessageCodec::HEADER_SIZE) {
        return false;
    }
    const uint16_t ethertype = static_cast<uint16_t>((static_cast<uint16_t>(data[12]) << 8) | data[13]);
    if (ethertype != ETHERTYPE) {
        return false;
    }

    return PilotWireMessageCodec::decode(data + ETHERNET_HEADER_SIZE, size - ETHERNET_HEADER_SIZE, message);
}
