// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "wtp/wire.hpp"

namespace wsprrypi::wtp {
bool FrameWriter::start(std::string_view payload, std::uint64_t now_ms, std::uint64_t total_ms,
                        std::uint64_t idle_ms) {
    if (state_ == WriteState::Pending)
        return false;
    if (payload.empty() || payload.size() > kMaximumPayloadBytes || !total_ms || !idle_ms) {
        fail();
        return false;
    }
    frame_ = encode_frame({reinterpret_cast<const std::uint8_t *>(payload.data()), payload.size()});
    offset_ = 0;
    budget_.emplace(now_ms, total_ms, idle_ms);
    state_ = WriteState::Pending;
    return true;
}
std::span<const std::uint8_t> FrameWriter::remaining(std::uint64_t now_ms) {
    if (state_ != WriteState::Pending)
        return {};
    if (budget_->expired(now_ms)) {
        fail();
        return {};
    }
    return std::span<const std::uint8_t>(frame_).subspan(offset_);
}
bool FrameWriter::consume(std::size_t count, std::uint64_t now_ms) {
    const auto pending = remaining(now_ms);
    if (state_ != WriteState::Pending)
        return false;
    if (count > pending.size()) {
        fail();
        return false;
    }
    if (count) {
        if (!budget_->progress(now_ms)) {
            fail();
            return false;
        }
        offset_ += count;
    }
    if (offset_ == frame_.size()) {
        state_ = WriteState::Complete;
        frame_.clear();
        budget_.reset();
    }
    return true;
}
void FrameWriter::fail() noexcept {
    state_ = WriteState::Failed;
    frame_.clear();
    budget_.reset();
}
void FrameWriter::cancel() noexcept {
    if (state_ != WriteState::Pending)
        return;
    fail();
    state_ = WriteState::Cancelled;
}
std::optional<RequestPacket> RequestPacket::create(const Request &request) {
    auto result = encode_request(request);
    if (!result)
        return {};
    return RequestPacket(std::move(*result.payload));
}
} // namespace wsprrypi::wtp
