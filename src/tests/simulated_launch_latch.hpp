// SPDX-License-Identifier: MIT
#pragma once

#include "wspr_transmit.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace wsprrypi::testing {

// Private to the actual-main recurrence executable. The parent test releases
// each worker by publishing the clock sample for that exact reserved slot.
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
        const auto release = directory_ / (slot + ".release");
        if (!std::ofstream(waiting))
            throw std::runtime_error("Could not report simulated launch readiness");

        while (!interrupted())
        {
            if (std::ifstream command(release); command)
            {
                std::int64_t sample = 0;
                if (!(command >> sample) || !(command >> std::ws).eof())
                    throw std::runtime_error("Invalid simulated launch clock sample");
                sample_ = Clock::time_point(duration_cast<Clock::duration>(nanoseconds(sample)));
                command.close();
                std::filesystem::remove(release);
                return true;
            }
            // Poll for the private signal. The interval cannot move the slot,
            // and Stop interrupts an unreleased worker.
            std::this_thread::sleep_for(milliseconds(5));
        }
        return false;
    }

    Clock::time_point now() const noexcept override { return sample_; }

private:
    std::filesystem::path directory_;
    Clock::time_point sample_{};
};

}
