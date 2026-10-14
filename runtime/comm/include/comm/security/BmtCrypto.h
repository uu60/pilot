#ifndef PILOT_BMT_CRYPTO_H
#define PILOT_BMT_CRYPTO_H

#include "intermediate/item/BitwiseBmt.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

struct EncryptedBitwiseBmt {
    static constexpr size_t IV_SIZE = 12;
    static constexpr size_t CIPHERTEXT_SIZE = 24;
    static constexpr size_t TAG_SIZE = 16;
    static constexpr size_t SERIALIZED_SIZE = IV_SIZE + CIPHERTEXT_SIZE + TAG_SIZE;

    std::array<uint8_t, IV_SIZE> iv{};
    std::array<uint8_t, CIPHERTEXT_SIZE> ciphertext{};
    std::array<uint8_t, TAG_SIZE> authenticationTag{};
};

class BmtCrypto {
public:
    static constexpr uint32_t FORMAT_VERSION = 1;

    static EncryptedBitwiseBmt encrypt(const RawBitwiseBmt &bmt,
                                       int destinationRank,
                                       int laneId,
                                       int tag,
                                       uint32_t entryIndex,
                                       const std::vector<int64_t> &clearPayload);

    static RawBitwiseBmt decrypt(const EncryptedBitwiseBmt &encrypted,
                                 int destinationRank,
                                 int laneId,
                                 int tag,
                                 uint32_t entryIndex,
                                 const std::vector<int64_t> &clearPayload);

    static std::vector<uint8_t> serialize(const std::vector<EncryptedBitwiseBmt> &entries);

    static std::vector<EncryptedBitwiseBmt> deserialize(const std::vector<uint8_t> &bytes);
};

#endif
