#include "wtp_server/frame_parser.hpp"

#include "wtp_server/memory_budget.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace wsprrypico::wtp {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'W', 'T', 'P', 'F'};
constexpr std::size_t kChunkBytes = 4096;
constexpr std::size_t kMaximumResyncDiscardBytes = 131'072;
constexpr std::size_t kMaximumInvalidFrames = 3;
constexpr std::uint64_t kPartialFrameTimeoutMs = 5000;

std::uint32_t read_u32_be(const std::uint8_t* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
           (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) | static_cast<std::uint32_t>(bytes[3]);
}

void write_u32_be(std::uint8_t* output, std::uint32_t value) {
    output[0] = static_cast<std::uint8_t>(value >> 24U);
    output[1] = static_cast<std::uint8_t>(value >> 16U);
    output[2] = static_cast<std::uint8_t>(value >> 8U);
    output[3] = static_cast<std::uint8_t>(value);
}

} // namespace

std::uint32_t crc32c(std::span<const std::uint8_t> bytes, std::uint32_t previous) {
    std::uint32_t crc = previous ^ 0xffffffffU;
    for (const auto byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0x82f63b78U : 0U);
        }
    }
    return crc ^ 0xffffffffU;
}

std::array<std::uint8_t, kFrameHeaderBytes>
encode_frame_header(std::span<const std::uint8_t> payload) {
    if (payload.empty() || payload.size() > kMaximumPayloadBytes)
        return {};
    return encode_frame_header(payload.size(), crc32c(payload));
}
std::array<std::uint8_t, kFrameHeaderBytes> encode_frame_header(std::size_t payload_bytes,
                                                                std::uint32_t checksum) {
    if (!payload_bytes || payload_bytes > kMaximumPayloadBytes)
        return {};
    std::array<std::uint8_t, kFrameHeaderBytes> header{'W', 'T', 'P', 'F', 1, 1, 0, 0};
    write_u32_be(header.data() + 8, static_cast<std::uint32_t>(payload_bytes));
    write_u32_be(header.data() + 12, checksum);
    return header;
}

std::vector<std::uint8_t> encode_frame(std::span<const std::uint8_t> payload) {
    if (payload.empty() || payload.size() > kMaximumPayloadBytes) {
        return {};
    }
    std::vector<std::uint8_t> frame;
    frame.reserve(kFrameHeaderBytes + payload.size());
    const auto header = encode_frame_header(payload);
    frame.insert(frame.end(), header.begin(), header.end());
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

std::vector<FrameEvent> FrameParser::feed(std::span<const std::uint8_t> bytes,
                                          std::uint64_t now_ms) {
    std::vector<FrameEvent> events;
    if (closed_) {
        return events;
    }
    for (std::size_t offset = 0; offset < bytes.size() && !closed_; offset += kChunkBytes) {
        const auto count = std::min(kChunkBytes, bytes.size() - offset);
        // Geometric growth must not turn a 65,552-byte frame into a 128 KiB
        // allocation on the target. One bounded feed chunk may follow a frame.
        if (buffer_.size() + count > buffer_.capacity()) {
            auto capacity = std::min(kMaximumPayloadBytes + kFrameHeaderBytes + kChunkBytes,
                                     std::max(buffer_.size() + count, buffer_.capacity() * 2));
            // process() leaves a complete header only for an incomplete,
            // validated frame. Size its next allocation to that frame instead
            // of rounding a WSPR upload up to 32 KiB. A batched feed can also
            // include following frames, so retain room for the current chunk.
            if (buffer_.size() >= kFrameHeaderBytes) {
                const auto frame_size =
                    kFrameHeaderBytes +
                    static_cast<std::size_t>(read_u32_be(buffer_.view().at(8).data()));
                capacity = std::max(buffer_.size() + count, frame_size);
            }
            if (!input_memory_admitted(capacity) || !buffer_.reserve(capacity)) {
                close(events);
                break;
            }
        }
        buffer_.append(bytes.subspan(offset, count));
        last_progress_ms_ = now_ms;
        process(events);
    }
    if (bytes.empty()) {
        auto timeout_events = check_timeout(now_ms);
        events.insert(events.end(), std::make_move_iterator(timeout_events.begin()),
                      std::make_move_iterator(timeout_events.end()));
    }
    return events;
}

std::vector<FrameEvent> FrameParser::check_timeout(std::uint64_t now_ms) {
    std::vector<FrameEvent> events;
    if (!closed_ && partial_ && now_ms >= last_progress_ms_ &&
        now_ms - last_progress_ms_ >= kPartialFrameTimeoutMs) {
        close(events);
    }
    return events;
}

void FrameParser::end_of_stream() {
    buffer_ = {};
    partial_ = false;
    closed_ = true;
}

void FrameParser::process(std::vector<FrameEvent>& events) {
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
        const auto length = read_u32_be(buffer_.view().at(8).data());
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
        const auto expected_crc = read_u32_be(buffer_.view().at(12).data());
        const auto payload = buffer_.view().substr(kFrameHeaderBytes, length);
        std::uint32_t checksum = 0;
        for (std::size_t offset = 0; offset < payload.size();) {
            const auto part = payload.at(offset);
            checksum = crc32c(part, checksum);
            offset += part.size();
        }
        if (checksum != expected_crc) {
            invalid_frame(events);
            if (!closed_) {
                buffer_.discard(frame_size);
            }
            continue;
        }
        if (buffer_.size() == frame_size) {
            // Endpoint feeds single bytes, so the complete frame can transfer
            // storage to dispatch without a second maximum-sized allocation.
            buffer_.discard(kFrameHeaderBytes);
            events.push_back({FrameEventKind::Payload, std::move(buffer_)});
            buffer_ = {};
        } else {
            FrameBuffer delivered;
            if (!input_memory_admitted(length) || !delivered.reserve(length)) {
                close(events);
                return;
            }
            delivered.append(payload);
            events.push_back({FrameEventKind::Payload, std::move(delivered)});
            buffer_.discard(frame_size);
        }
        consecutive_invalid_frames_ = 0;
        resync_discard_bytes_ = 0;
        partial_ = false;
    }
}

void FrameParser::discard_prefix(std::size_t count, std::vector<FrameEvent>& events) {
    if (count == 0) {
        return;
    }
    buffer_.discard(count);
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

void FrameParser::invalid_frame(std::vector<FrameEvent>& events) {
    events.push_back({FrameEventKind::InvalidFrame, {}});
    ++consecutive_invalid_frames_;
    if (consecutive_invalid_frames_ >= kMaximumInvalidFrames) {
        close(events);
    }
}

void FrameParser::close(std::vector<FrameEvent>& events) {
    if (!closed_) {
        closed_ = true;
        buffer_ = {};
        partial_ = false;
        events.push_back({FrameEventKind::Closed, {}});
    }
}

} // namespace wsprrypico::wtp
