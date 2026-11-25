// SPDX-License-Identifier: MIT
#pragma once

#include "wspr_transmit.hpp"

#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace wsprrypi::testing {

// Private to the actual-main recurrence executable. The parent test releases
// each worker by publishing the sample for that exact reserved slot. The
// application owner yields only after its work and locks have drained.
class SimulatedLaunchLatch final : public WsprTransmitter::SimulatedLaunchClock
{
public:
    explicit SimulatedLaunchLatch(const std::string &ledger)
        : directory_(ledger + ".simulated-launch")
    {
        std::filesystem::create_directories(directory_);
    }

    bool wait_for_slot(Clock::time_point start,
                       const std::function<bool()> &interrupted) override
    {
        using namespace std::chrono;
        const auto slot = std::to_string(duration_cast<nanoseconds>(start.time_since_epoch()).count());
        const auto waiting = directory_ / (slot + ".waiting");
        std::unique_lock lock(mutex_);
        if (!std::ofstream(waiting))
            throw std::runtime_error("Could not report simulated launch readiness");

        pending_release_ = directory_ / (slot + ".release");
        // Stop uses the transmitter's own condition variable. A bounded
        // wake here observes it even when the test supplies no clock sample.
        while (!released_ && !interrupted())
            ready_.wait_for(lock, milliseconds(5));
        return released_ && !interrupted();
    }

    void release_at_owner_idle()
    {
        using namespace std::chrono;
        std::unique_lock lock(mutex_);
        if (pending_release_.empty() || released_)
            return;
        std::ifstream command(pending_release_);
        if (!command)
            return;

        std::int64_t sample = 0;
        if (!(command >> sample) || !(command >> std::ws).eof())
            throw std::runtime_error("Invalid simulated launch clock sample");
        sample_ = Clock::time_point(duration_cast<Clock::duration>(nanoseconds(sample)));
        command.close();
        std::filesystem::remove(pending_release_);
        released_ = true;
        ready_.notify_all();
        if (!ready_.wait_for(lock, seconds(5), [this] { return pending_release_.empty(); }))
            throw std::runtime_error("Simulated worker did not finish launch admission");
    }

    Clock::time_point now() const noexcept override { return sample_; }

    void admission_complete() noexcept override
    {
        std::lock_guard lock(mutex_);
        pending_release_.clear();
        released_ = false;
        ready_.notify_all();
    }

private:
    std::filesystem::path directory_;
    std::filesystem::path pending_release_;
    std::mutex mutex_;
    std::condition_variable ready_;
    bool released_ = false;
    // Published by the owner before the worker leaves its condition wait.
    Clock::time_point sample_{};
};

}
