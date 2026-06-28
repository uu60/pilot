#include "comm/PilotPacket.h"

#include <algorithm>
#include <stdexcept>

std::vector<int64_t> PilotPacket::encodeRequest(const PilotPacketRequest &packet) {
    std::vector<int64_t> words;
    words.reserve(5 + packet.payload.size());
    words.push_back(REQUEST_MAGIC);
    words.push_back(packet.tag);
    words.push_back(packet.laneId);
    words.push_back(std::max(0, packet.bmtRequestCount));
    words.push_back(static_cast<int64_t>(packet.payload.size()));
    words.insert(words.end(), packet.payload.begin(), packet.payload.end());
    return words;
}

PilotPacketRequest PilotPacket::decodeRequest(const std::vector<int64_t> &words) {
    if (words.size() >= 5 && words[0] == REQUEST_MAGIC) {
        const auto payloadSize = static_cast<size_t>(words[4]);
        if (words.size() != 5 + payloadSize) {
            throw std::runtime_error("Invalid pilot request packet size.");
        }

        PilotPacketRequest packet;
        packet.tag = static_cast<int>(words[1]);
        packet.laneId = static_cast<int>(words[2]);
        packet.bmtRequestCount = static_cast<int>(words[3]);
        packet.payload.assign(words.begin() + 5, words.end());
        return packet;
    }

    // Backward-compatible path for pre-packetized server messages.
    PilotPacketRequest packet;
    packet.payload = words;
    return packet;
}

std::vector<int64_t> PilotPacket::encodeEnvelope(const PilotPacketEnvelope &packet) {
    const int trailerWords = static_cast<int>(packet.trailer.size()) * 3;
    std::vector<int64_t> words;
    words.reserve(7 + packet.payload.size() + static_cast<size_t>(trailerWords));
    words.push_back(ENVELOPE_MAGIC);
    words.push_back(static_cast<int64_t>(packet.payload.size()));
    words.push_back(trailerWords);
    words.push_back(packet.srcRank);
    words.push_back(packet.dstRank);
    words.push_back(packet.tag);
    words.push_back(packet.laneId);
    words.insert(words.end(), packet.payload.begin(), packet.payload.end());

    for (const auto &bmt: packet.trailer) {
        words.push_back(bmt.a);
        words.push_back(bmt.b);
        words.push_back(bmt.c);
    }
    return words;
}

PilotPacketEnvelope PilotPacket::decodeEnvelope(const std::vector<int64_t> &words) {
    if (words.size() < 7 || words[0] != ENVELOPE_MAGIC) {
        throw std::runtime_error("Invalid pilot envelope packet.");
    }

    const auto payloadSize = static_cast<size_t>(words[1]);
    const auto trailerSize = static_cast<size_t>(words[2]);
    if (words.size() != 7 + payloadSize + trailerSize) {
        throw std::runtime_error("Pilot envelope packet size mismatch.");
    }
    if (trailerSize % 3 != 0) {
        throw std::runtime_error("Pilot envelope BMT trailer size mismatch.");
    }

    PilotPacketEnvelope packet;
    packet.srcRank = static_cast<int>(words[3]);
    packet.dstRank = static_cast<int>(words[4]);
    packet.tag = static_cast<int>(words[5]);
    packet.laneId = static_cast<int>(words[6]);
    packet.payload.assign(words.begin() + 7, words.begin() + 7 + payloadSize);
    packet.trailer.reserve(trailerSize / 3);

    auto trailerIt = words.begin() + 7 + payloadSize;
    for (size_t i = 0; i < trailerSize; i += 3) {
        packet.trailer.push_back(RawBitwiseBmt{*(trailerIt + i), *(trailerIt + i + 1), *(trailerIt + i + 2)});
    }
    return packet;
}
