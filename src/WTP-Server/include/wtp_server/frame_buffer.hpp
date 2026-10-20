#pragma once

#include "wtp_server/input_view.hpp"

#include <array>

namespace wsprrypico::wtp {
// A maximum input frame may occupy separate heap holes. The fixed page table
// owns nullable InputBuffers; failed growth is released by parser closure.
class FrameBuffer {
  public:
    static constexpr std::size_t page_bytes = InputView::page_bytes;
    static constexpr std::size_t maximum_bytes = 65536 + 16 + 4096;
    FrameBuffer() = default;
    FrameBuffer(const FrameBuffer&) = delete;
    FrameBuffer& operator=(const FrameBuffer&) = delete;
    FrameBuffer(FrameBuffer&& other) noexcept
        : pages_(std::move(other.pages_)), size_(std::exchange(other.size_, 0)),
          capacity_(std::exchange(other.capacity_, 0)) {}
    FrameBuffer& operator=(FrameBuffer&& other) noexcept {
        pages_ = std::move(other.pages_);
        size_ = std::exchange(other.size_, 0);
        capacity_ = std::exchange(other.capacity_, 0);
        return *this;
    }
    bool reserve(std::size_t bytes) {
        if (bytes <= capacity_)
            return true;
        if (bytes > maximum_bytes)
            return false;
        for (std::size_t offset = 0; offset < bytes; offset += page_bytes)
            if (!pages_[offset / page_bytes].reserve(std::min(page_bytes, bytes - offset)))
                return false;
        capacity_ = bytes;
        return true;
    }
    void append(std::span<const std::uint8_t> bytes) {
        while (!bytes.empty()) {
            const auto count = std::min(bytes.size(), page_bytes - size_ % page_bytes);
            pages_[size_ / page_bytes].append(bytes.first(count));
            bytes = bytes.subspan(count);
            size_ += count;
        }
    }
    void append(InputView bytes) {
        for (std::size_t offset = 0; offset < bytes.size();) {
            auto part = bytes.at(offset);
            append(part);
            offset += part.size();
        }
    }
    void discard(std::size_t count) {
        const auto remaining = size_ - count;
        for (std::size_t i = 0; i < remaining; ++i)
            pages_[i / page_bytes].data()[i % page_bytes] = (*this)[i + count];
        size_ = remaining;
        for (std::size_t i = 0; i < pages_.size(); ++i) {
            const auto offset = i * page_bytes;
            pages_[i].truncate(offset < size_ ? std::min(page_bytes, size_ - offset) : 0);
        }
    }
    InputView view() const {
        return {pages_.data(), size_};
    }
    auto begin() const {
        return view().begin();
    }
    auto end() const {
        return view().end();
    }
    std::uint8_t operator[](std::size_t index) const {
        return pages_[index / page_bytes][index % page_bytes];
    }
    std::size_t size() const {
        return size_;
    }
    std::size_t capacity() const {
        return capacity_;
    }
    bool empty() const {
        return !size_;
    }
    bool operator==(std::span<const std::uint8_t> other) const {
        if (size_ != other.size())
            return false;
        for (std::size_t i = 0; i < size_; ++i)
            if ((*this)[i] != other[i])
                return false;
        return true;
    }

  private:
    std::array<InputBuffer, (maximum_bytes + page_bytes - 1) / page_bytes> pages_;
    std::size_t size_ = 0, capacity_ = 0;
};
} // namespace wsprrypico::wtp
