#pragma once

#include "wtp_server/frame_buffer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace wsprrypico::wtp {

constexpr std::size_t kFrameHeaderBytes = 16;
constexpr std::size_t kMaximumPayloadBytes = 65'536;

std::uint32_t crc32c(std::span<const std::uint8_t> bytes, std::uint32_t previous = 0);
// Invalid payload lengths return a zero header, which is never a valid frame.
std::array<std::uint8_t, kFrameHeaderBytes>
encode_frame_header(std::span<const std::uint8_t> payload);
std::array<std::uint8_t, kFrameHeaderBytes> encode_frame_header(std::size_t payload_bytes,
                                                                std::uint32_t checksum);
std::vector<std::uint8_t> encode_frame(std::span<const std::uint8_t> payload);

enum class FrameEventKind { Payload, InvalidFrame, Closed };

struct FrameEvent {
    FrameEventKind kind;
    FrameBuffer payload;
};

class FrameParser {
  public:
    std::vector<FrameEvent> feed(std::span<const std::uint8_t> bytes, std::uint64_t now_ms);
    std::vector<FrameEvent> check_timeout(std::uint64_t now_ms);
    void end_of_stream();

    [[nodiscard]] bool closed() const {
        return closed_;
    }
    [[nodiscard]] std::size_t buffered_bytes() const {
        return buffer_.size();
    }
    [[nodiscard]] std::size_t buffered_capacity() const {
        return buffer_.capacity();
    }

  private:
    void process(std::vector<FrameEvent>& events);
    void discard_prefix(std::size_t count, std::vector<FrameEvent>& events);
    void invalid_frame(std::vector<FrameEvent>& events);
    void close(std::vector<FrameEvent>& events);

    FrameBuffer buffer_;
    std::size_t consecutive_invalid_frames_ = 0;
    std::size_t resync_discard_bytes_ = 0;
    std::uint64_t last_progress_ms_ = 0;
    bool partial_ = false;
    bool closed_ = false;
};

} // namespace wsprrypico::wtp
