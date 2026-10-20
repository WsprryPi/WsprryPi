// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace wsprrypi::wtp {
enum class IoState { Progress, WouldBlock, Closed, Failed };
struct IoResult {
    IoState state;
    std::size_t count{};
};

// Adapter calls must return promptly; they must not retain these spans.
// Progress means 1..span.size() bytes. Every other state has count == 0.
// WouldBlock guarantees no bytes were transferred. Failed/Closed during a write
// may have an unknown effect. The caller owns the stream and its trust boundary.
class ByteStream {
  public:
    virtual ~ByteStream() = default;
    virtual IoResult read(std::span<std::uint8_t> destination) = 0;
    virtual IoResult write(std::span<const std::uint8_t> source) = 0;
    virtual void close() noexcept = 0;
};
} // namespace wsprrypi::wtp
