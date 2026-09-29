#pragma once

#include "wtp_server/input_buffer.hpp"

#include <array>

namespace wsprrypico::wtp {
// A finite wire reply need not occupy one contiguous heap block. Allocate all
// pages before publishing any bytes; partial allocation failure releases them.
class OutputBuffer {
  public:
    static constexpr std::size_t page_bytes = 4096, maximum_bytes = 65536;
    OutputBuffer() = default;
    OutputBuffer(const OutputBuffer&) = delete;
    OutputBuffer& operator=(const OutputBuffer&) = delete;
    OutputBuffer(OutputBuffer&& other) noexcept
        : pages_(std::move(other.pages_)), size_(std::exchange(other.size_, 0)),
          capacity_(std::exchange(other.capacity_, 0)) {}
    OutputBuffer& operator=(OutputBuffer&& other) noexcept {
        pages_ = std::move(other.pages_);
        size_ = std::exchange(other.size_, 0);
        capacity_ = std::exchange(other.capacity_, 0);
        return *this;
    }
    bool reserve(std::size_t bytes) {
        if (capacity_)
            return bytes <= capacity_;
        if (!bytes || bytes > maximum_bytes)
            return false;
        for (std::size_t offset = 0; offset < bytes; offset += page_bytes) {
            if (!pages_[offset / page_bytes].reserve(std::min(page_bytes, bytes - offset))) {
                *this = OutputBuffer{};
                return false;
            }
        }
        capacity_ = bytes;
        return true;
    }
    bool append(std::span<const std::uint8_t> bytes) {
        if (bytes.size() > capacity_ - size_)
            return false;
        while (!bytes.empty()) {
            const auto count = std::min(bytes.size(), page_bytes - size_ % page_bytes);
            pages_[size_ / page_bytes].append(bytes.first(count));
            bytes = bytes.subspan(count);
            size_ += count;
        }
        return true;
    }
    std::span<const std::uint8_t> at(std::size_t offset) const {
        if (offset >= size_)
            return {};
        const auto& page = pages_[offset / page_bytes];
        return {page.data() + offset % page_bytes,
                std::min(size_ - offset, page_bytes - offset % page_bytes)};
    }
    std::size_t size() const {
        return size_;
    }
    bool empty() const {
        return size_ == 0;
    }

  private:
    std::array<InputBuffer, maximum_bytes / page_bytes> pages_;
    std::size_t size_ = 0, capacity_ = 0;
};
} // namespace wsprrypico::wtp
