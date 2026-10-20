#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <utility>

namespace wsprrypico::wtp {
// Installed once before transports start. Input and large reply buffers use
// nullable newlib allocation rather than the SDK's panic wrapper.
inline void* (*allocate_input)(std::size_t) = std::malloc;
// Paired with allocate_input; replace only while no buffers are alive.
inline void (*deallocate_input)(void*) = std::free;

class InputBuffer {
  public:
    InputBuffer() = default;
    InputBuffer(const InputBuffer&) = delete;
    InputBuffer& operator=(const InputBuffer&) = delete;
    InputBuffer(InputBuffer&& other) noexcept {
        swap(other);
    }
    InputBuffer& operator=(InputBuffer&& other) noexcept {
        InputBuffer old(std::move(other));
        swap(old);
        return *this;
    }
    ~InputBuffer() {
        wipe();
        deallocate_input(data_);
    }
    bool reserve(std::size_t capacity) {
        if (capacity <= capacity_)
            return true;
        auto* next = static_cast<std::uint8_t*>(allocate_input(capacity));
        if (!next)
            return false;
        if (size_)
            std::memcpy(next, data_, size_);
        wipe();
        deallocate_input(data_);
        data_ = next;
        capacity_ = capacity;
        return true;
    }
    void append(std::span<const std::uint8_t> bytes) {
        if (!bytes.empty())
            std::memcpy(data_ + size_, bytes.data(), bytes.size());
        size_ += bytes.size();
    }
    void discard(std::size_t count) {
        size_ -= count;
        if (size_)
            std::memmove(data_, data_ + count, size_);
    }
    void truncate(std::size_t size) {
        size_ = std::min(size_, size);
    }
    void clear() {
        size_ = 0;
    }
    std::uint8_t* data() {
        return data_;
    }
    const std::uint8_t* data() const {
        return data_;
    }
    std::uint8_t* begin() {
        return data_;
    }
    std::uint8_t* end() {
        return size_ ? data_ + size_ : data_;
    }
    const std::uint8_t* begin() const {
        return data_;
    }
    const std::uint8_t* end() const {
        return size_ ? data_ + size_ : data_;
    }
    std::size_t size() const {
        return size_;
    }
    std::size_t capacity() const {
        return capacity_;
    }
    bool empty() const {
        return size_ == 0;
    }
    std::uint8_t operator[](std::size_t index) const {
        return data_[index];
    }
    operator std::span<const std::uint8_t>() const {
        return {data_, size_};
    }
    bool operator==(std::span<const std::uint8_t> other) const {
        return size_ == other.size() && (!size_ || std::equal(begin(), end(), other.begin()));
    }

  private:
    void wipe() noexcept {
        volatile std::uint8_t* bytes = data_;
        for (std::size_t i = 0; i < capacity_; ++i)
            bytes[i] = 0;
    }
    void swap(InputBuffer& other) noexcept {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
        std::swap(capacity_, other.capacity_);
    }
    std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
};
} // namespace wsprrypico::wtp
