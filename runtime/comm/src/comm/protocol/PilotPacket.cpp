#include "comm/protocol/PilotPacket.h"

#include <algorithm>
#include <stdexcept>

namespace {
std::vector<int64_t> packBytes(const std::vector<uint8_t> &bytes) {
    std::vector<int64_t> words((bytes.size() + 7) / 8, 0);
    for (size_t i = 0; i < bytes.size(); ++i) {
        const auto shift = static_cast<unsigned>((7 - (i % 8)) * 8);
        words[i / 8] = static_cast<int64_t>(static_cast<uint64_t>(words[i / 8]) |
                                            (static_cast<uint64_t>(bytes[i]) << shift));
    }
    return words;
}

std::vector<uint8_t> unpackBytes(const std::vector<int64_t>::const_iterator begin,
                                 size_t wordCount,
                                 size_t byteCount) {
    std::vector<uint8_t> bytes(byteCount);
    for (size_t i = 0; i < byteCount; ++i) {
        const auto word = static_cast<uint64_t>(*(begin + static_cast<std::ptrdiff_t>(i / 8)));
        const auto shift = static_cast<unsigned>((7 - (i % 8)) * 8);
        bytes[i] = static_cast<uint8_t>(word >> shift);
    }
    if (wordCount != (byteCount + 7) / 8) {
        throw std::runtime_error("Encrypted BMT trailer packing mismatch.");
    }
    return bytes;
}
}

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
    const auto trailerBytes = BmtCrypto::serialize(packet.encryptedTrailer);
    if (trailerBytes.size() >= ENCRYPTED_TRAILER_FLAG) {
        throw std::runtime_error("Encrypted BMT trailer is too large.");
    }
    const auto packedTrailer = packBytes(trailerBytes);
    std::vector<int64_t> words;
    words.reserve(7 + packet.payload.size() + packedTrailer.size());
    words.push_back(ENVELOPE_MAGIC);
    words.push_back(static_cast<int64_t>(packet.payload.size()));
    words.push_back(static_cast<int64_t>(ENCRYPTED_TRAILER_FLAG | trailerBytes.size()));
    words.push_back(packet.srcRank);
    words.push_back(packet.dstRank);
    words.push_back(packet.tag);
    words.push_back(packet.laneId);
    words.insert(words.end(), packet.payload.begin(), packet.payload.end());
    words.insert(words.end(), packedTrailer.begin(), packedTrailer.end());
    return words;
}

PilotPacketEnvelope PilotPacket::decodeEnvelope(const std::vector<int64_t> &words) {
    if (words.size() < 7 || words[0] != ENVELOPE_MAGIC) {
        throw std::runtime_error("Invalid pilot envelope packet.");
    }

    if (words[1] < 0) {
        throw std::runtime_error("Pilot envelope has a negative payload size.");
    }
    const auto payloadSize = static_cast<size_t>(words[1]);
    if (payloadSize > words.size() - 7) {
        throw std::runtime_error("Pilot envelope payload exceeds the packet size.");
    }
    const auto trailerDescriptor = static_cast<uint64_t>(words[2]);
    if ((trailerDescriptor & ENCRYPTED_TRAILER_FLAG) == 0) {
        throw std::runtime_error("Unencrypted pilot BMT trailers are no longer accepted.");
    }
    const auto trailerBytes = static_cast<size_t>(trailerDescriptor & ~ENCRYPTED_TRAILER_FLAG);
    const auto availableTrailerWords = words.size() - 7 - payloadSize;
    if (trailerBytes > availableTrailerWords * sizeof(int64_t)) {
        throw std::runtime_error("Encrypted BMT trailer exceeds the packet size.");
    }
    const auto trailerWords = (trailerBytes + 7) / 8;
    if (words.size() != 7 + payloadSize + trailerWords) {
        throw std::runtime_error("Pilot envelope packet size mismatch.");
    }

    PilotPacketEnvelope packet;
    packet.srcRank = static_cast<int>(words[3]);
    packet.dstRank = static_cast<int>(words[4]);
    packet.tag = static_cast<int>(words[5]);
    packet.laneId = static_cast<int>(words[6]);
    packet.payload.assign(words.begin() + 7, words.begin() + 7 + payloadSize);
    const auto trailerIt = words.begin() + static_cast<std::ptrdiff_t>(7 + payloadSize);
    packet.encryptedTrailer = BmtCrypto::deserialize(unpackBytes(trailerIt, trailerWords, trailerBytes));
    return packet;
}
