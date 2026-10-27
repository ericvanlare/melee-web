#pragma once
// Private byte identity leaf shared without enabling the provenance recorder.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace melee_web::private_sha256 {
using std::size_t;
class Sha256 {
public:
    Sha256() {
        state_ = {UINT32_C(0x6a09e667), UINT32_C(0xbb67ae85),
                  UINT32_C(0x3c6ef372), UINT32_C(0xa54ff53a),
                  UINT32_C(0x510e527f), UINT32_C(0x9b05688c),
                  UINT32_C(0x1f83d9ab), UINT32_C(0x5be0cd19)};
    }

    void update(const uint8_t* bytes, size_t length) {
        while (length) {
            const size_t take = std::min(length, sizeof(buffer_) - buffered_);
            std::memcpy(buffer_.data() + buffered_, bytes, take);
            buffered_ += take;
            bytes += take;
            length -= take;
            bit_count_ += static_cast<uint64_t>(take) * 8;
            if (buffered_ == sizeof(buffer_)) {
                compress(buffer_.data());
                buffered_ = 0;
            }
        }
    }

    std::array<uint8_t, 32> finish() {
        const uint64_t original_bits = bit_count_;
        buffer_[buffered_++] = 0x80;
        if (buffered_ > 56) {
            while (buffered_ < 64) buffer_[buffered_++] = 0;
            compress(buffer_.data());
            buffered_ = 0;
        }
        while (buffered_ < 56) buffer_[buffered_++] = 0;
        for (unsigned i = 0; i != 8; ++i) {
            buffer_[56 + i] = static_cast<uint8_t>(original_bits >> (56 - i * 8));
        }
        compress(buffer_.data());

        std::array<uint8_t, 32> digest{};
        for (unsigned i = 0; i != 8; ++i) {
            digest[i * 4] = static_cast<uint8_t>(state_[i] >> 24);
            digest[i * 4 + 1] = static_cast<uint8_t>(state_[i] >> 16);
            digest[i * 4 + 2] = static_cast<uint8_t>(state_[i] >> 8);
            digest[i * 4 + 3] = static_cast<uint8_t>(state_[i]);
        }
        return digest;
    }

private:
    static uint32_t rotr(uint32_t value, unsigned amount) {
        return (value >> amount) | (value << (32 - amount));
    }

    void compress(const uint8_t* block) {
        static constexpr uint32_t k[64] = {
            UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf),
            UINT32_C(0xe9b5dba5), UINT32_C(0x3956c25b), UINT32_C(0x59f111f1),
            UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5), UINT32_C(0xd807aa98),
            UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
            UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7),
            UINT32_C(0xc19bf174), UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786),
            UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc), UINT32_C(0x2de92c6f),
            UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
            UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8),
            UINT32_C(0xbf597fc7), UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147),
            UINT32_C(0x06ca6351), UINT32_C(0x14292967), UINT32_C(0x27b70a85),
            UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
            UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e),
            UINT32_C(0x92722c85), UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b),
            UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3), UINT32_C(0xd192e819),
            UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
            UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c),
            UINT32_C(0x34b0bcb5), UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a),
            UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3), UINT32_C(0x748f82ee),
            UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
            UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7),
            UINT32_C(0xc67178f2)};
        uint32_t words[64]{};
        for (unsigned i = 0; i != 16; ++i) {
            words[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
                       (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                       (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
                       static_cast<uint32_t>(block[i * 4 + 3]);
        }
        for (unsigned i = 16; i != 64; ++i) {
            const uint32_t s0 = rotr(words[i - 15], 7) ^ rotr(words[i - 15], 18) ^
                                (words[i - 15] >> 3);
            const uint32_t s1 = rotr(words[i - 2], 17) ^ rotr(words[i - 2], 19) ^
                                (words[i - 2] >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }
        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (unsigned i = 0; i != 64; ++i) {
            const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t choice = (e & f) ^ ((~e) & g);
            const uint32_t temp1 = h + s1 + choice + k[i] + words[i];
            const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<uint32_t, 8> state_{};
    std::array<uint8_t, 64> buffer_{};
    size_t buffered_ = 0;
    uint64_t bit_count_ = 0;
};

inline std::array<uint8_t, 32> digest(const void* bytes, size_t length) {
    Sha256 sha;
    if (length) sha.update(static_cast<const uint8_t*>(bytes), length);
    return sha.finish();
}

}
