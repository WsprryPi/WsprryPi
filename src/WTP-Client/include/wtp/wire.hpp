// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include "wtp/codec.hpp"
#include "wtp/frame_parser.hpp"
#include <utility>

namespace wsprrypi::wtp {

// The owning transaction can use this budget for responses as well as writes.
// Progress never extends the absolute deadline. No wall-clock or OS dependency.
class ProgressBudget {
  public:
    ProgressBudget(std::uint64_t now_ms, std::uint64_t total_ms, std::uint64_t idle_ms)
        : started_(now_ms), progressed_(now_ms), observed_(now_ms), total_(total_ms),
          idle_(idle_ms) {}
    bool expired(std::uint64_t now_ms) noexcept {
        failed_ = failed_ || now_ms < observed_ || now_ms - started_ >= total_ ||
                  now_ms - progressed_ >= idle_;
        observed_ = now_ms;
        return failed_;
    }
    bool progress(std::uint64_t now_ms) noexcept {
        if (expired(now_ms))
            return false;
        progressed_ = now_ms;
        return true;
    }

  private:
    std::uint64_t started_, progressed_, observed_, total_, idle_;
    bool failed_{};
};

enum class WriteState { Idle, Pending, Complete, Failed, Cancelled };
class FrameWriter {
  public:
    // Rejects replacing a pending frame. Payload must already be codec-validated
    // when used for WTP messages. No acknowledgment or mutation success is implied.
    bool start(std::string_view payload, std::uint64_t now_ms, std::uint64_t total_ms,
               std::uint64_t idle_ms);
    std::span<const std::uint8_t> remaining(std::uint64_t now_ms);
    bool consume(std::size_t accepted_bytes, std::uint64_t now_ms);
    void cancel() noexcept;
    WriteState state() const noexcept { return state_; }

  private:
    std::vector<std::uint8_t> frame_;
    std::size_t offset_{};
    std::optional<ProgressBudget> budget_;
    WriteState state_{WriteState::Idle};
    void fail() noexcept;
};

// This wire primitive does not negotiate or retry. Session retains this
// immutable packet throughout a transaction and any explicit retry.
class RequestPacket {
  public:
    static std::optional<RequestPacket> create(const Request &request);
    const std::string &payload() const noexcept { return payload_; }

  private:
    explicit RequestPacket(std::string payload) : payload_(std::move(payload)) {}
    std::string payload_;
};

} // namespace wsprrypi::wtp
