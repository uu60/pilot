#include "comm/security/BmtCrypto.h"

#include "comm/Comm.h"

#include <algorithm>
#include <array>
#include <memory>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdexcept>

namespace {
using Key = std::array<uint8_t, 16>;

// Simulation keys mirror the placeholder RTL keys. They model two independent
// server channels; production keys will be provisioned by the future hardware
// security module and must never be compiled into the application.
constexpr Key kServer0Key{
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};
constexpr Key kServer1Key{
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
};

const Key &keyForRank(int rank) {
    if (rank == Comm::SERVER0_RANK) return kServer0Key;
    if (rank == Comm::SERVER1_RANK) return kServer1Key;
    throw std::runtime_error("BMT encryption destination is not a server rank.");
}

void writeU32(std::array<uint8_t, 24> &out, size_t offset, uint32_t value) {
    out[offset] = static_cast<uint8_t>(value >> 24);
    out[offset + 1] = static_cast<uint8_t>(value >> 16);
    out[offset + 2] = static_cast<uint8_t>(value >> 8);
    out[offset + 3] = static_cast<uint8_t>(value);
}

void writeU64(uint8_t *out, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        *out++ = static_cast<uint8_t>(value >> shift);
    }
}

uint64_t readU64(const uint8_t *data) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value = (value << 8) | data[i];
    return value;
}

std::array<uint8_t, 24> makeAad(int destinationRank, int laneId, int tag,
                                uint32_t entryIndex, size_t payloadWords) {
    if (payloadWords > UINT32_MAX) throw std::runtime_error("BMT clear payload is too large.");
    std::array<uint8_t, 24> aad{};
    writeU32(aad, 0, BmtCrypto::FORMAT_VERSION);
    writeU32(aad, 4, static_cast<uint32_t>(destinationRank));
    writeU32(aad, 8, static_cast<uint32_t>(laneId));
    writeU32(aad, 12, static_cast<uint32_t>(tag));
    writeU32(aad, 16, entryIndex);
    writeU32(aad, 20, static_cast<uint32_t>(payloadWords));
    return aad;
}

void requireSuccess(int result, const char *message) {
    if (result != 1) throw std::runtime_error(message);
}

void authenticatePayload(EVP_CIPHER_CTX *context, const std::vector<int64_t> &payload, bool encrypting) {
    std::array<uint8_t, 8> encoded{};
    for (const auto word: payload) {
        writeU64(encoded.data(), static_cast<uint64_t>(word));
        int written = 0;
        const int result = encrypting
                               ? EVP_EncryptUpdate(context, nullptr, &written, encoded.data(),
                                                   static_cast<int>(encoded.size()))
                               : EVP_DecryptUpdate(context, nullptr, &written, encoded.data(),
                                                   static_cast<int>(encoded.size()));
        requireSuccess(result, "Unable to authenticate the clear e/f payload.");
    }
}

using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
}

EncryptedBitwiseBmt BmtCrypto::encrypt(const RawBitwiseBmt &bmt,
                                       int destinationRank,
                                       int laneId,
                                       int tag,
                                       uint32_t entryIndex,
                                       const std::vector<int64_t> &clearPayload) {
    std::array<uint8_t, EncryptedBitwiseBmt::CIPHERTEXT_SIZE> plaintext{};
    writeU64(plaintext.data(), static_cast<uint64_t>(bmt.a));
    writeU64(plaintext.data() + 8, static_cast<uint64_t>(bmt.b));
    writeU64(plaintext.data() + 16, static_cast<uint64_t>(bmt.c));
    const auto aad = makeAad(destinationRank, laneId, tag, entryIndex, clearPayload.size());

    EncryptedBitwiseBmt result;
    requireSuccess(RAND_bytes(result.iv.data(), static_cast<int>(result.iv.size())),
                   "Unable to generate an AES-GCM IV for a BMT share.");

    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!context) throw std::runtime_error("Unable to allocate an AES-GCM encryption context.");
    requireSuccess(EVP_EncryptInit_ex(context.get(), EVP_aes_128_gcm(), nullptr, nullptr, nullptr),
                   "Unable to initialize AES-GCM encryption.");
    requireSuccess(EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN,
                                       static_cast<int>(result.iv.size()), nullptr),
                   "Unable to configure the AES-GCM IV length.");
    const auto &key = keyForRank(destinationRank);
    requireSuccess(EVP_EncryptInit_ex(context.get(), nullptr, nullptr, key.data(), result.iv.data()),
                   "Unable to load the AES-GCM BMT key.");

    int written = 0;
    requireSuccess(EVP_EncryptUpdate(context.get(), nullptr, &written, aad.data(), static_cast<int>(aad.size())),
                   "Unable to authenticate BMT metadata.");
    authenticatePayload(context.get(), clearPayload, true);
    requireSuccess(EVP_EncryptUpdate(context.get(), result.ciphertext.data(), &written,
                                     plaintext.data(), static_cast<int>(plaintext.size())),
                   "Unable to encrypt a BMT share.");
    if (written != static_cast<int>(plaintext.size())) {
        throw std::runtime_error("AES-GCM returned an unexpected BMT ciphertext length.");
    }
    int finalBytes = 0;
    requireSuccess(EVP_EncryptFinal_ex(context.get(), result.ciphertext.data() + written, &finalBytes),
                   "Unable to finalize BMT encryption.");
    requireSuccess(EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG,
                                       static_cast<int>(result.authenticationTag.size()),
                                       result.authenticationTag.data()),
                   "Unable to obtain the BMT authentication tag.");
    return result;
}

RawBitwiseBmt BmtCrypto::decrypt(const EncryptedBitwiseBmt &encrypted,
                                 int destinationRank,
                                 int laneId,
                                 int tag,
                                 uint32_t entryIndex,
                                 const std::vector<int64_t> &clearPayload) {
    const auto aad = makeAad(destinationRank, laneId, tag, entryIndex, clearPayload.size());
    std::array<uint8_t, EncryptedBitwiseBmt::CIPHERTEXT_SIZE> plaintext{};

    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!context) throw std::runtime_error("Unable to allocate an AES-GCM decryption context.");
    requireSuccess(EVP_DecryptInit_ex(context.get(), EVP_aes_128_gcm(), nullptr, nullptr, nullptr),
                   "Unable to initialize AES-GCM decryption.");
    requireSuccess(EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN,
                                       static_cast<int>(encrypted.iv.size()), nullptr),
                   "Unable to configure the AES-GCM IV length.");
    const auto &key = keyForRank(destinationRank);
    requireSuccess(EVP_DecryptInit_ex(context.get(), nullptr, nullptr, key.data(), encrypted.iv.data()),
                   "Unable to load the AES-GCM BMT key.");

    int written = 0;
    requireSuccess(EVP_DecryptUpdate(context.get(), nullptr, &written, aad.data(), static_cast<int>(aad.size())),
                   "Unable to authenticate BMT metadata.");
    authenticatePayload(context.get(), clearPayload, false);
    requireSuccess(EVP_DecryptUpdate(context.get(), plaintext.data(), &written,
                                     encrypted.ciphertext.data(),
                                     static_cast<int>(encrypted.ciphertext.size())),
                   "Unable to decrypt a BMT share.");
    requireSuccess(EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG,
                                       static_cast<int>(encrypted.authenticationTag.size()),
                                       const_cast<uint8_t *>(encrypted.authenticationTag.data())),
                   "Unable to configure the expected BMT authentication tag.");
    int finalBytes = 0;
    if (EVP_DecryptFinal_ex(context.get(), plaintext.data() + written, &finalBytes) != 1) {
        throw std::runtime_error("BMT AES-GCM authentication failed.");
    }

    return RawBitwiseBmt{
        static_cast<int64_t>(readU64(plaintext.data())),
        static_cast<int64_t>(readU64(plaintext.data() + 8)),
        static_cast<int64_t>(readU64(plaintext.data() + 16))
    };
}

std::vector<uint8_t> BmtCrypto::serialize(const std::vector<EncryptedBitwiseBmt> &entries) {
    std::vector<uint8_t> bytes;
    bytes.reserve(entries.size() * EncryptedBitwiseBmt::SERIALIZED_SIZE);
    for (const auto &entry: entries) {
        bytes.insert(bytes.end(), entry.iv.begin(), entry.iv.end());
        bytes.insert(bytes.end(), entry.ciphertext.begin(), entry.ciphertext.end());
        bytes.insert(bytes.end(), entry.authenticationTag.begin(), entry.authenticationTag.end());
    }
    return bytes;
}

std::vector<EncryptedBitwiseBmt> BmtCrypto::deserialize(const std::vector<uint8_t> &bytes) {
    if (bytes.size() % EncryptedBitwiseBmt::SERIALIZED_SIZE != 0) {
        throw std::runtime_error("Encrypted BMT trailer has an invalid byte length.");
    }
    std::vector<EncryptedBitwiseBmt> entries(bytes.size() / EncryptedBitwiseBmt::SERIALIZED_SIZE);
    auto input = bytes.begin();
    for (auto &entry: entries) {
        std::copy_n(input, entry.iv.size(), entry.iv.begin());
        input += static_cast<std::ptrdiff_t>(entry.iv.size());
        std::copy_n(input, entry.ciphertext.size(), entry.ciphertext.begin());
        input += static_cast<std::ptrdiff_t>(entry.ciphertext.size());
        std::copy_n(input, entry.authenticationTag.size(), entry.authenticationTag.begin());
        input += static_cast<std::ptrdiff_t>(entry.authenticationTag.size());
    }
    return entries;
}
