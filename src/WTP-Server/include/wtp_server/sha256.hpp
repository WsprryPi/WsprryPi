#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace wsprrypico::wtp {

using PayloadDigest = std::array<std::uint8_t, 32>;

class Sha256 {
  public:
    void update(std::span<const std::uint8_t> bytes);
    PayloadDigest finish() const;

  private:
    void compress(const std::uint8_t* block);
    std::array<std::uint32_t, 8> hash_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                       0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::array<std::uint8_t, 64> tail_{};
    std::size_t pending_ = 0;
    std::uint64_t bytes_ = 0;
};

PayloadDigest sha256(std::span<const std::uint8_t> bytes);

} // namespace wsprrypico::wtp
