#pragma once

#include "wtp_server/input_buffer.hpp"

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>

namespace wsprrypico::wtp {
// Non-owning immutable bytes, either contiguous or in independently allocated
// pages. Subviews retain the same storage identity; none materialize a copy.
class InputView {
  public:
    static constexpr std::size_t page_bytes = 4096;
    class Iterator {
      public:
        using value_type = char;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;
        using reference = char;
        using pointer = void;
        Iterator() = default;
        Iterator(const char* flat, const InputBuffer* pages, std::size_t offset)
            : flat_(flat), pages_(pages), offset_(offset) {}
        char operator*() const {
            return pages_ ? static_cast<char>(pages_[offset_ / page_bytes][offset_ % page_bytes])
                          : flat_[offset_];
        }
        Iterator& operator++() {
            ++offset_;
            return *this;
        }
        Iterator operator++(int) {
            auto old = *this;
            ++*this;
            return old;
        }
        bool operator==(const Iterator&) const = default;
        difference_type operator-(const Iterator& other) const {
            return static_cast<difference_type>(offset_) -
                   static_cast<difference_type>(other.offset_);
        }

      private:
        const char* flat_ = nullptr;
        const InputBuffer* pages_ = nullptr;
        std::size_t offset_ = 0;
    };
    InputView() = default;
    InputView(const char* data, std::size_t size) : flat_(data), size_(size) {}
    InputView(std::string_view text) : InputView(text.data(), text.size()) {}
    InputView(const std::string& text) : InputView(text.data(), text.size()) {}
    InputView(const char* text) : InputView(std::string_view(text)) {}
    InputView(std::span<const std::uint8_t> bytes)
        : InputView(reinterpret_cast<const char*>(bytes.data()), bytes.size()) {}
    InputView(const InputBuffer* pages, std::size_t size) : pages_(pages), size_(size) {}
    std::size_t size() const {
        return size_;
    }
    std::size_t offset() const {
        return offset_;
    }
    bool empty() const {
        return !size_;
    }
    char operator[](std::size_t index) const {
        return *Iterator(flat_, pages_, offset_ + index);
    }
    Iterator begin() const {
        return {flat_, pages_, offset_};
    }
    Iterator end() const {
        return {flat_, pages_, offset_ + size_};
    }
    InputView substr(std::size_t offset, std::size_t count = std::string_view::npos) const {
        InputView result = *this;
        offset = std::min(offset, size_);
        result.offset_ += offset;
        result.size_ = std::min(count, size_ - offset);
        return result;
    }
    std::span<const std::uint8_t> at(std::size_t index) const {
        if (index >= size_)
            return {};
        const auto absolute = offset_ + index;
        if (!pages_)
            return {reinterpret_cast<const std::uint8_t*>(flat_ + absolute), size_ - index};
        return {pages_[absolute / page_bytes].data() + absolute % page_bytes,
                std::min(size_ - index, page_bytes - absolute % page_bytes)};
    }
    bool operator==(std::string_view other) const {
        return size_ == other.size() && std::equal(begin(), end(), other.begin());
    }
    explicit operator std::string() const {
        return {begin(), end()};
    }

  private:
    const char* flat_ = nullptr;
    const InputBuffer* pages_ = nullptr;
    std::size_t offset_ = 0, size_ = 0;
};
} // namespace wsprrypico::wtp
