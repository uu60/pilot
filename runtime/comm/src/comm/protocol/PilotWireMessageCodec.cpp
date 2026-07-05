#include "comm/protocol/PilotWireMessageCodec.h"

#include "comm/protocol/PilotPacket.h"

#include <limits>
#include <stdexcept>

namespace {
constexpr uint8_t kStringMessage = 3;

void appendU8(std::vector<uint8_t> &out, uint8_t value) {
    out.push_back(value);
}

void appendU16(std::vector<uint8_t> &out, uint16_t value) {
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(value & 0xff));
}

void appendU32(std::vector<uint8_t> &out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(value & 0xff));
}

void appendU64(std::vector<uint8_t> &out, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xff));
    }
}

uint16_t readU16(const uint8_t *data) {
    return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
}

uint32_t readU32(const uint8_t *data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

uint64_t readU64(const uint8_t *data) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | data[i];
    }
    return value;
}

uint32_t flagsForMessage(const RoutedPeerMessage &message) {
    if (message.type != 2 || message.words.empty()) {
        return 0;
    }
    if (message.words[0] == PilotPacket::REQUEST_MAGIC) {
        return PilotWireMessageCodec::FLAG_SWITCH_REQUEST;
    }
    if (message.words[0] == PilotPacket::ENVELOPE_MAGIC) {
        return PilotWireMessageCodec::FLAG_SWITCH_ENVELOPE;
    }
    return 0;
}
}

std::vector<uint8_t> PilotWireMessageCodec::encode(const RoutedPeerMessage &message) {
    const bool isString = message.type == kStringMessage;
    const size_t payloadItems = isString ? 0 : message.words.size();
    const size_t payloadBytes = isString ? message.text.size() : message.words.size() * sizeof(int64_t);
    if (payloadItems > std::numeric_limits<uint32_t>::max() ||
        payloadBytes > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("Pilot wire message payload is too large.");
    }

    std::vector<uint8_t> out;
    out.reserve(HEADER_SIZE + payloadBytes);
    appendU32(out, MAGIC);
    appendU8(out, VERSION);
    appendU8(out, static_cast<uint8_t>(message.type));
    appendU16(out, static_cast<uint16_t>(HEADER_SIZE));
    appendU16(out, static_cast<uint16_t>(message.senderRank));
    appendU16(out, static_cast<uint16_t>(message.receiverRank));
    appendU32(out, static_cast<uint32_t>(message.tag));
    appendU32(out, static_cast<uint32_t>(payloadItems));
    appendU32(out, static_cast<uint32_t>(payloadBytes));
    appendU32(out, flagsForMessage(message));
    appendU32(out, 0);

    if (isString) {
        out.insert(out.end(), message.text.begin(), message.text.end());
        return out;
    }

    for (int64_t word: message.words) {
        appendU64(out, static_cast<uint64_t>(word));
    }
    return out;
}

bool PilotWireMessageCodec::decode(const uint8_t *data, size_t size, RoutedPeerMessage &message) {
    size_t payloadBytes = 0;
    if (!payloadBytesFromHeader(data, size, payloadBytes)) {
        return false;
    }

    const auto type = static_cast<int>(data[5]);
    const auto headerBytes = static_cast<size_t>(readU16(data + 6));
    const auto payloadItems = static_cast<size_t>(readU32(data + 16));
    if (size < headerBytes + payloadBytes) {
        return false;
    }

    message.senderRank = static_cast<int16_t>(readU16(data + 8));
    message.receiverRank = static_cast<int16_t>(readU16(data + 10));
    message.tag = static_cast<int32_t>(readU32(data + 12));
    message.type = type;
    message.words.clear();
    message.text.clear();

    const uint8_t *payload = data + headerBytes;
    if (type == kStringMessage) {
        message.text.assign(reinterpret_cast<const char *>(payload), payloadBytes);
        return true;
    }

    if (payloadBytes != payloadItems * sizeof(int64_t)) {
        return false;
    }
    message.words.reserve(payloadItems);
    for (size_t i = 0; i < payloadItems; ++i) {
        message.words.push_back(static_cast<int64_t>(readU64(payload + i * sizeof(int64_t))));
    }
    return true;
}

bool PilotWireMessageCodec::payloadBytesFromHeader(const uint8_t *data, size_t size, size_t &payloadBytes) {
    if (size < HEADER_SIZE) {
        return false;
    }
    if (readU32(data) != MAGIC || data[4] != VERSION) {
        return false;
    }
    const auto headerBytes = static_cast<size_t>(readU16(data + 6));
    if (headerBytes != HEADER_SIZE || size < headerBytes) {
        return false;
    }
    payloadBytes = static_cast<size_t>(readU32(data + 20));
    return true;
}
