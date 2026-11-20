// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
namespace wsprrypi {
struct LocalEvent {std::uint64_t sequence;std::string message;};
struct LocalEvents {std::uint64_t sequence;std::uint64_t head;bool resync_required;std::vector<LocalEvent> events;};
// Observations and command-result notifications only. Delivery grants no
// durable receipt or authority. A lagging consumer must obtain an authoritative
// snapshot; loss never means a command succeeded or a Stop hold cleared.
class EventFeed {
public:
    // Match the existing WebSocket frame and pending-byte bounds.
    static constexpr std::size_t maximum_message=64*1024;
    static constexpr std::size_t maximum_bytes=128*1024;
    static constexpr std::size_t maximum_events=32;
    bool publish(const std::string &message) noexcept;
    LocalEvents read(std::uint64_t after,std::size_t count=16) const;
private:
    mutable std::mutex mutex_;
    std::deque<LocalEvent> events_;
    std::size_t bytes_{};
    std::atomic<std::uint64_t> sequence_{0},gap_{0};
};
EventFeed &backend_events();
}
