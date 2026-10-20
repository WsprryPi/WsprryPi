#pragma once

#include "rp1_gpclk_planner.hpp"

#include <functional>
#include <string>

namespace wsprrypi
{

struct Rp1GpclkLifecycleResult
{
    bool ok{false};
    std::string error;
};

/**
 * Resource boundary for a future RP1 GPCLK implementation.
 *
 * Phase 3 supplies only a fake implementation in tests.  No production
 * adapter, register mapping, pin control, or clock access is provided here.
 */
class Rp1GpclkResourceAdapter
{
public:
    virtual ~Rp1GpclkResourceAdapter() = default;

    virtual bool acquireClock(std::string& error) = 0;
    virtual bool acquirePin(std::string& error) = 0;
    virtual bool configureClock(
        const Rp1GpclkPlan& plan,
        std::string& error) = 0;
    virtual bool configurePin(std::string& error) = 0;
    virtual bool enableOutput(std::string& error) = 0;

    virtual void disableOutput() noexcept = 0;
    virtual void releasePin() noexcept = 0;
    virtual void releaseClock() noexcept = 0;
};

/**
 * Fail-closed lifecycle coordinator for an RP1 GPCLK resource adapter.
 *
 * The coordinator owns no hardware knowledge.  It guarantees reverse-order
 * cleanup for normal stop, cancellation, setup failure, and destruction.
 */
class Rp1GpclkLifecycle
{
public:
    using CancellationCheck = std::function<bool()>;

    explicit Rp1GpclkLifecycle(Rp1GpclkResourceAdapter& adapter) noexcept;
    ~Rp1GpclkLifecycle();

    Rp1GpclkLifecycle(const Rp1GpclkLifecycle&) = delete;
    Rp1GpclkLifecycle& operator=(const Rp1GpclkLifecycle&) = delete;

    Rp1GpclkLifecycleResult start(
        const Rp1GpclkPlan& plan,
        CancellationCheck cancelled = {});
    void cancel() noexcept;
    void stop() noexcept;

    bool ownsClock() const noexcept;
    bool ownsPin() const noexcept;
    bool outputEnabled() const noexcept;
    bool running() const noexcept;

private:
    Rp1GpclkLifecycleResult fail(const std::string& error) noexcept;
    bool cancellationRequested(const CancellationCheck& cancelled) const;

    Rp1GpclkResourceAdapter& adapter_;
    bool clock_owned_{false};
    bool pin_owned_{false};
    bool output_may_be_enabled_{false};
    bool running_{false};
};

} // namespace wsprrypi
