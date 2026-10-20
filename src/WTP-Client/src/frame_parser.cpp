// SPDX-License-Identifier: MIT
// Derived from WsprryPico; Copyright (c) 2026 Lee Bussy. See LICENSE.md and PROVENANCE.json.
#include "wtp/frame_parser.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace wsprrypi::wtp {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'W', 'T', 'P', 'F'};
constexpr std::size_t kMaximumResyncDiscardBytes = 131'072;
constexpr std::size_t kMaximumInvalidFrames = 3;
constexpr std::uint64_t kPartialFrameTimeoutMs = 5000;

std::uint32_t read_u32_be(const std::uint8_t *bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
           (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) | static_cast<std::uint32_t>(bytes[3]);
}

void append_u32_be(std::vector<std::uint8_t> &output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value >> 24U));
    output.push_back(static_cast<std::uint8_t>(value >> 16U));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value));
}

} // namespace

std::uint32_t crc32c(std::span<const std::uint8_t> bytes) {
    std::uint32_t crc = 0xffffffffU;
    for (const auto byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0x82f63b78U : 0U);
        }
    }
    return crc ^ 0xffffffffU;
}

std::vector<std::uint8_t> encode_frame(std::span<const std::uint8_t> payload) {
    if (payload.empty() || payload.size() > kMaximumPayloadBytes) {
        return {};
    }
    std::vector<std::uint8_t> frame;
    frame.reserve(kFrameHeaderBytes + payload.size());
    frame.insert(frame.end(), kMagic.begin(), kMagic.end());
    frame.push_back(1);
    frame.push_back(1);
    frame.push_back(0);
    frame.push_back(0);
    append_u32_be(frame, static_cast<std::uint32_t>(payload.size()));
    append_u32_be(frame, crc32c(payload));
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

FrameRead FrameParser::feed(std::span<const std::uint8_t> bytes, std::uint64_t now_ms) {
    FrameRead result{0, check_timeout(now_ms)};
    if (closed_)
        return result;
    const auto count = std::min(kInputChunkBytes, bytes.size());
    if (count) {
        buffer_.insert(buffer_.end(), bytes.begin(),
                       bytes.begin() + static_cast<std::ptrdiff_t>(count));
        result.consumed = count;
        last_progress_ms_ = now_ms;
        process(result.events);
    }
    return result;
}

std::vector<FrameEvent> FrameParser::check_timeout(std::uint64_t now_ms) {
    std::vector<FrameEvent> events;
    if (!closed_ && (now_ms < last_observed_ms_ ||
                     (partial_ && now_ms - last_progress_ms_ >= kPartialFrameTimeoutMs))) {
        close(events);
    }
    last_observed_ms_ = now_ms;
    return events;
}

void FrameParser::end_of_stream() {
    buffer_.clear();
    partial_ = false;
    closed_ = true;
}

void FrameParser::process(std::vector<FrameEvent> &events) {
    while (!closed_) {
        const auto magic =
            std::search(buffer_.begin(), buffer_.end(), kMagic.begin(), kMagic.end());
        if (magic != buffer_.begin()) {
            if (magic == buffer_.end()) {
                const auto keep = std::min<std::size_t>(3, buffer_.size());
                discard_prefix(buffer_.size() - keep, events);
                partial_ = !buffer_.empty();
                return;
            }
            discard_prefix(static_cast<std::size_t>(magic - buffer_.begin()), events);
            if (closed_) {
                return;
            }
        }
        if (buffer_.size() < kFrameHeaderBytes) {
            partial_ = !buffer_.empty();
            return;
        }
        const auto length = read_u32_be(buffer_.data() + 8);
        const bool valid_header = buffer_[4] == 1 && buffer_[5] == 1 && buffer_[6] == 0 &&
                                  buffer_[7] == 0 && length >= 1 && length <= kMaximumPayloadBytes;
        if (!valid_header) {
            invalid_frame(events);
            if (!closed_) {
                discard_prefix(1, events);
            }
            continue;
        }
        const auto frame_size = kFrameHeaderBytes + static_cast<std::size_t>(length);
        if (buffer_.size() < frame_size) {
            partial_ = true;
            return;
        }
        const auto expected_crc = read_u32_be(buffer_.data() + 12);
        const std::span<const std::uint8_t> payload(buffer_.data() + kFrameHeaderBytes, length);
        if (crc32c(payload) != expected_crc) {
            invalid_frame(events);
            if (!closed_) {
                buffer_.erase(buffer_.begin(),
                              buffer_.begin() + static_cast<std::ptrdiff_t>(frame_size));
            }
            continue;
        }
        events.push_back({FrameEventKind::Payload, {payload.begin(), payload.end()}});
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(frame_size));
        consecutive_invalid_frames_ = 0;
        resync_discard_bytes_ = 0;
        partial_ = false;
    }
}

void FrameParser::discard_prefix(std::size_t count, std::vector<FrameEvent> &events) {
    if (count == 0) {
        return;
    }
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(count));
    if (count >
        kMaximumResyncDiscardBytes - std::min(resync_discard_bytes_, kMaximumResyncDiscardBytes)) {
        resync_discard_bytes_ = kMaximumResyncDiscardBytes;
    } else {
        resync_discard_bytes_ += count;
    }
    if (resync_discard_bytes_ >= kMaximumResyncDiscardBytes) {
        close(events);
    }
}

void FrameParser::invalid_frame(std::vector<FrameEvent> &events) {
    events.push_back({FrameEventKind::InvalidFrame, {}});
    ++consecutive_invalid_frames_;
    if (consecutive_invalid_frames_ >= kMaximumInvalidFrames) {
        close(events);
    }
}

void FrameParser::close(std::vector<FrameEvent> &events) {
    if (!closed_) {
        closed_ = true;
        buffer_.clear();
        partial_ = false;
        events.push_back({FrameEventKind::Closed, {}});
    }
}

} // namespace wsprrypi::wtp
