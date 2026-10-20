#include "wtp_server/sha256.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace wsprrypico::wtp {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U};

std::uint32_t rotate_right(std::uint32_t value, unsigned count) {
    return (value >> count) | (value << (32U - count));
}

std::uint32_t read_u32_be(const std::uint8_t* input) {
    return (static_cast<std::uint32_t>(input[0]) << 24U) |
           (static_cast<std::uint32_t>(input[1]) << 16U) |
           (static_cast<std::uint32_t>(input[2]) << 8U) | static_cast<std::uint32_t>(input[3]);
}

} // namespace

void Sha256::compress(const std::uint8_t* block) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
        words[index] = read_u32_be(block + index * 4);
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
        const auto s0 = rotate_right(words[index - 15], 7) ^ rotate_right(words[index - 15], 18) ^
                        (words[index - 15] >> 3U);
        const auto s1 = rotate_right(words[index - 2], 17) ^ rotate_right(words[index - 2], 19) ^
                        (words[index - 2] >> 10U);
        words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }
    auto a = hash_[0];
    auto b = hash_[1];
    auto c = hash_[2];
    auto d = hash_[3];
    auto e = hash_[4];
    auto f = hash_[5];
    auto g = hash_[6];
    auto h = hash_[7];
    for (std::size_t index = 0; index < words.size(); ++index) {
        const auto sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        const auto choose = (e & f) ^ (~e & g);
        const auto temporary1 = h + sum1 + choose + kRoundConstants[index] + words[index];
        const auto sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto temporary2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }
    hash_[0] += a;
    hash_[1] += b;
    hash_[2] += c;
    hash_[3] += d;
    hash_[4] += e;
    hash_[5] += f;
    hash_[6] += g;
    hash_[7] += h;
}

void Sha256::update(std::span<const std::uint8_t> bytes) {
    bytes_ += bytes.size();
    while (!bytes.empty()) {
        if (!pending_ && bytes.size() >= 64) {
            compress(bytes.data());
            bytes = bytes.subspan(64);
            continue;
        }
        const auto count = std::min(bytes.size(), tail_.size() - pending_);
        std::copy_n(bytes.begin(), count, tail_.begin() + pending_);
        pending_ += count;
        bytes = bytes.subspan(count);
        if (pending_ == 64) {
            compress(tail_.data());
            pending_ = 0;
        }
    }
}
PayloadDigest Sha256::finish() const {
    auto state = *this;
    state.tail_[state.pending_++] = 0x80;
    std::fill(state.tail_.begin() + state.pending_, state.tail_.end(), 0);
    if (state.pending_ > 56) {
        state.compress(state.tail_.data());
        state.tail_.fill(0);
    }
    const auto bits = bytes_ * 8;
    for (std::size_t i = 0; i < 8; ++i)
        state.tail_[63 - i] = static_cast<std::uint8_t>(bits >> (i * 8));
    state.compress(state.tail_.data());
    PayloadDigest digest{};
    for (std::size_t i = 0; i < state.hash_.size(); ++i)
        for (std::size_t j = 0; j < 4; ++j)
            digest[i * 4 + j] = static_cast<std::uint8_t>(state.hash_[i] >> (24 - j * 8));
    return digest;
}
PayloadDigest sha256(std::span<const std::uint8_t> bytes) {
    Sha256 state;
    state.update(bytes);
    return state.finish();
}

} // namespace wsprrypico::wtp
