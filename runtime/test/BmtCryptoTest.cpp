#include "comm/protocol/PilotPacket.h"
#include "comm/security/BmtCrypto.h"

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

template<typename Callable>
void requireFailure(Callable callable, const char *message) {
    try {
        callable();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error(message);
}
}

int main() {
    try {
        const RawBitwiseBmt original{
            static_cast<int64_t>(0xfedcba9876543210ULL),
            static_cast<int64_t>(0x0123456789abcdefULL),
            static_cast<int64_t>(0x8000000000000001ULL)
        };
        const std::vector<int64_t> clearPayload{1, -2, 3, -4};
        const auto encrypted = BmtCrypto::encrypt(original, 0, 3, 10003, 7, clearPayload);
        const auto decrypted = BmtCrypto::decrypt(encrypted, 0, 3, 10003, 7, clearPayload);
        require(decrypted.a == original.a && decrypted.b == original.b && decrypted.c == original.c,
                "AES-GCM BMT round trip changed the share.");

        auto tamperedCiphertext = encrypted;
        tamperedCiphertext.ciphertext[0] ^= 1;
        requireFailure([&] { BmtCrypto::decrypt(tamperedCiphertext, 0, 3, 10003, 7, clearPayload); },
                       "A modified BMT ciphertext was accepted.");

        auto tamperedPayload = clearPayload;
        tamperedPayload[1] ^= 1;
        requireFailure([&] { BmtCrypto::decrypt(encrypted, 0, 3, 10003, 7, tamperedPayload); },
                       "A modified clear e/f payload was accepted.");
        requireFailure([&] { BmtCrypto::decrypt(encrypted, 1, 3, 10003, 7, clearPayload); },
                       "A BMT encrypted for the other server was accepted.");

        PilotPacketEnvelope envelope;
        envelope.srcRank = 1;
        envelope.dstRank = 0;
        envelope.tag = 10003;
        envelope.laneId = 3;
        envelope.payload = clearPayload;
        envelope.encryptedTrailer = {
            BmtCrypto::encrypt(original, 0, 3, 10003, 0, clearPayload),
            BmtCrypto::encrypt(original, 0, 3, 10003, 1, clearPayload),
            BmtCrypto::encrypt(original, 0, 3, 10003, 2, clearPayload)
        };
        const auto decoded = PilotPacket::decodeEnvelope(PilotPacket::encodeEnvelope(envelope));
        require(decoded.srcRank == envelope.srcRank && decoded.dstRank == envelope.dstRank &&
                    decoded.tag == envelope.tag && decoded.laneId == envelope.laneId &&
                    decoded.payload == envelope.payload &&
                    decoded.encryptedTrailer.size() == envelope.encryptedTrailer.size(),
                "Encrypted pilot envelope round trip changed its metadata.");
        for (uint32_t i = 0; i < decoded.encryptedTrailer.size(); ++i) {
            const auto entry = BmtCrypto::decrypt(decoded.encryptedTrailer[i], decoded.dstRank,
                                                  decoded.laneId, decoded.tag, i, decoded.payload);
            require(entry.a == original.a && entry.b == original.b && entry.c == original.c,
                    "Encrypted pilot envelope changed a BMT share.");
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
