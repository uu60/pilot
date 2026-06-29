#include "comm/transport/peer/TapFrameCodec.h"

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
    const int64_t payloadSize = message.type == 3 ? static_cast<int64_t>(message.text.size())
                                                   : static_cast<int64_t>(message.words.size());
    const int64_t header[5]{
        static_cast<int64_t>(message.senderRank),
        static_cast<int64_t>(message.receiverRank),
        static_cast<int64_t>(message.tag),
        static_cast<int64_t>(message.type),
        payloadSize
    };

    std::vector<uint8_t> frame;
    frame.reserve(ETHERNET_HEADER_SIZE + sizeof(header) +
                  (message.type == 3 ? message.text.size() : message.words.size() * sizeof(int64_t)));
    appendBytes(frame, dst.data(), dst.size());
    appendBytes(frame, src.data(), src.size());
    frame.push_back(static_cast<uint8_t>((ETHERTYPE >> 8) & 0xff));
    frame.push_back(static_cast<uint8_t>(ETHERTYPE & 0xff));
    appendBytes(frame, header, sizeof(header));
    if (message.type == 3) {
        appendBytes(frame, message.text.data(), message.text.size());
    } else if (!message.words.empty()) {
        appendBytes(frame, message.words.data(), message.words.size() * sizeof(int64_t));
    }
    return frame;
}

bool TapFrameCodec::decode(const uint8_t *data, size_t size, RoutedPeerMessage &message) {
    if (size < ETHERNET_HEADER_SIZE + sizeof(int64_t) * 5) {
        return false;
    }
    const uint16_t ethertype = static_cast<uint16_t>((static_cast<uint16_t>(data[12]) << 8) | data[13]);
    if (ethertype != ETHERTYPE) {
        return false;
    }

    int64_t header[5]{};
    std::memcpy(header, data + ETHERNET_HEADER_SIZE, sizeof(header));
    if (header[4] < 0) {
        return false;
    }

    message.senderRank = static_cast<int>(header[0]);
    message.receiverRank = static_cast<int>(header[1]);
    message.tag = static_cast<int>(header[2]);
    message.type = static_cast<int>(header[3]);
    const auto payloadSize = static_cast<size_t>(header[4]);
    const size_t payloadOffset = ETHERNET_HEADER_SIZE + sizeof(header);
    const size_t payloadBytes = message.type == 3 ? payloadSize : payloadSize * sizeof(int64_t);
    if (size < payloadOffset + payloadBytes) {
        return false;
    }

    message.words.clear();
    message.text.clear();
    if (message.type == 3) {
        message.text.assign(reinterpret_cast<const char *>(data + payloadOffset), payloadBytes);
    } else {
        message.words.resize(payloadSize);
        if (payloadBytes > 0) {
            std::memcpy(message.words.data(), data + payloadOffset, payloadBytes);
        }
    }
    return true;
}
