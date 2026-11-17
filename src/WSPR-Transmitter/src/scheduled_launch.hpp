// SPDX-License-Identifier: MIT
#pragma once
#include <chrono>
#include <functional>
namespace wsprrypi {
// Optional application-neutral binding of one prepared execution to its
// evaluated slot. An empty binding preserves ordinary scheduler behavior.
struct ScheduledLaunch {
    using Clock=std::chrono::system_clock;
    Clock::time_point start{};
    std::function<bool()> guard;
    bool bound() const noexcept {return start.time_since_epoch().count()!=0;}
    bool current(Clock::time_point now) const noexcept {
        if(bound()&&(now<start||now-start>std::chrono::milliseconds(50)))return false;
        try{return !guard||guard();}catch(...){return false;}
    }
};
}
