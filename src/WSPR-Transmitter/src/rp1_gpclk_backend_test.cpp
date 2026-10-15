/* SPDX-License-Identifier: MIT */
#include "rp1_gpclk_backend.hpp"
#include "rp1_gpclk_uapi.h"

#include <iostream>
#include <string>
#include <vector>

namespace
{
int failures;
void expect(bool value, const char* message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
class Provider final : public wsprrypi::Rp1GpclkProvider
{
public:
    bool query(std::uint32_t, std::uint64_t, bool,
        wsprrypi::Rp1GpclkProviderIdentity&, std::string&) override
    { return true; }

    bool acquire(std::uint32_t route, std::uint64_t caps,
        std::string&) override
    {
        routes.push_back(route);
        capabilities.push_back(caps);
        next_generation = 1;
        return acquire_ok;
    }

    bool submitEvents(wsprrypi::Rp1GpclkProviderEventProgram& program,
        std::string&) override
    {
        if (!submit_ok) return false;
        program.generation = next_generation++;
        programs.push_back(program);
        current = wsprrypi::Rp1GpclkCompletionState::running;
        return true;
    }

    bool requestFiniteStop(std::uint64_t generation, std::string&) override
    {
        stops.push_back(generation);
        current = wsprrypi::Rp1GpclkCompletionState::draining;
        return true;
    }

    wsprrypi::Rp1GpclkCompletionState state(std::uint64_t) const noexcept override
    { return current; }

    wsprrypi::Rp1GpclkProviderEventState eventState(
        std::uint64_t) const noexcept override
    { return {current, 0, 0, cleanup_fault, 0, 0, cleanup_detail}; }

    bool release(std::string&) noexcept override
    {
        ++releases;
        return release_ok;
    }

    bool acquire_ok{true};
    bool submit_ok{true};
    bool release_ok{true};
    std::uint32_t cleanup_fault{};
    std::string cleanup_detail;
    int releases{};
    std::uint64_t next_generation{1};
    wsprrypi::Rp1GpclkCompletionState current{
        wsprrypi::Rp1GpclkCompletionState::idle};
    std::vector<std::uint32_t> routes;
    std::vector<std::uint64_t> capabilities;
    std::vector<std::uint64_t> stops;
    std::vector<wsprrypi::Rp1GpclkProviderEventProgram> programs;
};

bool prepare(wsprrypi::Rp1GpclkBackend& backend, std::uint32_t drive,
    std::string& error)
{
    return backend.prepare(drive, RP1_GPCLK_ROUTE_GPIO4,
        RP1_GPCLK_CAP_SUBMIT_EVENTS | RP1_GPCLK_CAP_BOUNDED_DMA_CHUNKS,
        error);
}

wsprrypi::Rp1GpclkProviderEventProgram program()
{
    wsprrypi::Rp1GpclkProviderEventProgram value;
    value.fractional_bits = 16;
    value.tick_divider = 511;
    value.total_duration_ns = 1000000000ULL;
    value.tones.push_back({1, 2, 1, 1});
    value.events.push_back({1000000000ULL, 0, true});
    return value;
}

void test_drive_profiles()
{
    for (auto drive : {2u, 4u, 8u, 12u})
    {
        Provider provider;
        wsprrypi::Rp1GpclkBackend backend(provider);
        std::string error;
        expect(prepare(backend, drive, error), "supported drive must prepare");
        expect(backend.cleanup(error), "idle backend must clean up");
    }
    for (auto drive : {0u, 6u, 10u, 16u})
    {
        Provider provider;
        wsprrypi::Rp1GpclkBackend backend(provider);
        std::string error;
        expect(!prepare(backend, drive, error) && provider.routes.empty(),
            "unsupported drive must be rejected before provider acquisition");
    }
}

void test_generic_program_and_stop()
{
    Provider provider;
    wsprrypi::Rp1GpclkBackend backend(provider);
    std::string error;
    expect(prepare(backend, 2, error), "prepare must acquire provider");
    expect(backend.emitEvents(program(), error), "generic event program must submit");
    expect(provider.programs.size() == 1 &&
        provider.programs[0].drive_ma == 2 && backend.generation() == 1,
        "submission must bind prepared drive and provider generation");
    expect(backend.cancel(error) && provider.stops.back() == 1,
        "cancel must bind the active generation");
    expect(!backend.cleanup(error),
        "cleanup must not release a draining descriptor");
    provider.current = wsprrypi::Rp1GpclkCompletionState::complete;
    expect(backend.cleanup(error),
        "cleanup must release after terminal completion");
    expect(provider.releases == 1, "lease must release exactly once");
}

void test_failures()
{
    Provider acquire_failure;
    acquire_failure.acquire_ok = false;
    wsprrypi::Rp1GpclkBackend first(acquire_failure);
    std::string error;
    expect(!prepare(first, 2, error), "acquisition failure must propagate");

    Provider submit_failure;
    submit_failure.submit_ok = false;
    wsprrypi::Rp1GpclkBackend second(submit_failure);
    expect(prepare(second, 2, error) &&
        !second.emitEvents(program(), error) &&
        second.cleanup(error),
        "submit failure must leave an acquired but releasable lease");

    Provider release_failure;
    release_failure.release_ok = false;
    wsprrypi::Rp1GpclkBackend third(release_failure);
    expect(prepare(third, 2, error) && !third.cleanup(error),
        "release failure must remain visible");
    release_failure.release_ok = true;
    expect(!third.cleanup(error) && !prepare(third, 2, error),
        "later cleanup or prepare must not clear a latched release failure");

    Provider terminal_fault;
    wsprrypi::Rp1GpclkBackend fourth(terminal_fault);
    expect(prepare(fourth, 2, error) && fourth.emitEvents(program(), error),
        "terminal cleanup fault fixture must submit");
    terminal_fault.current = wsprrypi::Rp1GpclkCompletionState::failed;
    terminal_fault.cleanup_fault = 1;
    terminal_fault.cleanup_detail = "final state poll EIO detail";
    expect(!fourth.cleanup(error) && terminal_fault.releases == 1 &&
        error.find("final state poll EIO detail") != std::string::npos,
        "cleanup's own terminal observation must retain and latch its fault");
    terminal_fault.cleanup_fault = 0;
    expect(!fourth.cleanup(error) && !prepare(fourth, 2, error),
        "a later clean poll must not erase a terminal cleanup fault");
}
}

int main()
{
    test_drive_profiles();
    test_generic_program_and_stop();
    test_failures();
    if (failures) return 1;
    std::cout << "RP1 GPCLK production backend contract tests passed\n";
}
