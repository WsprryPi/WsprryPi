/* SPDX-License-Identifier: MIT */
#include "rp1_gpclk_linux_provider.hpp"
#include "rp1_gpclk_uapi.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <vector>

namespace
{
int failures;
void expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
constexpr std::uint64_t kRequired =
    RP1_GPCLK_CAP_SUBMIT_EVENTS | RP1_GPCLK_CAP_STOP_DRAIN |
    RP1_GPCLK_CAP_STABLE_STATE | RP1_GPCLK_CAP_ROUTE_IDENTITY |
    RP1_GPCLK_CAP_COMPAT_IDENTITY | RP1_GPCLK_CAP_CLEANUP_FAULT_LATCH |
    RP1_GPCLK_CAP_OUTPUT_INHIBIT | RP1_GPCLK_CAP_PASSIVE_SNAPSHOT |
    RP1_GPCLK_CAP_BOUNDED_DMA_CHUNKS;

struct Io final : wsprrypi::Rp1GpclkIo
{
    Io()
    {
        query.header.size = sizeof(query);
        query.route = RP1_GPCLK_ROUTE_GPIO4;
        query.compatibility_state = RP1_GPCLK_COMPAT_EXPERIMENTAL;
        query.capabilities = kRequired;
        query.max_tones = RP1_GPCLK_MAX_TONES;
        query.max_events = RP1_GPCLK_MAX_EVENTS;
        query.max_dither_period = RP1_GPCLK_DITHER_PERIOD_MAX;
        query.supported_drive_ma_mask = RP1_GPCLK_DRIVE_SUPPORT_ALLOWED_MASK;
        query.max_event_duration_ns = RP1_GPCLK_EVENT_DURATION_NS_MAX;
        query.max_request_duration_ns = RP1_GPCLK_REQUEST_DURATION_NS_MAX;
        query.dma_chunk_duration_ns = RP1_GPCLK_DMA_CHUNK_DURATION_NS;
        std::strcpy(query.module_id, "rp1-gpclk-dkms");
        std::strcpy(query.build_id, "1.0.0");
        std::strcpy(query.compatibility_id, "v1.0.0-rp1-gpio4");

        snapshot.header.size = sizeof(snapshot);
        snapshot.route = query.route;
        snapshot.compatibility_state = query.compatibility_state;
        snapshot.operation_state = RP1_GPCLK_STATE_IDLE;
        snapshot.terminal_reason = RP1_GPCLK_REASON_NONE;
        snapshot.cleanup_fault = RP1_GPCLK_OBSERVATION_FALSE;
        snapshot.owner_present = RP1_GPCLK_OBSERVATION_FALSE;
        snapshot.lease_present = RP1_GPCLK_OBSERVATION_FALSE;
        snapshot.output_inhibited = RP1_GPCLK_OBSERVATION_FALSE;
        snapshot.operational_ready = RP1_GPCLK_OBSERVATION_TRUE;
        snapshot.drain_state = RP1_GPCLK_DRAIN_NONE;
        snapshot.gpio_safe = RP1_GPCLK_OBSERVATION_TRUE;
        snapshot.clock_quiescent = RP1_GPCLK_OBSERVATION_TRUE;
        snapshot.dma_quiescent = RP1_GPCLK_OBSERVATION_TRUE;
        snapshot.stable = RP1_GPCLK_OBSERVATION_TRUE;
        snapshot.capabilities = query.capabilities;
        snapshot.dma_chunk_duration_ns = RP1_GPCLK_DMA_CHUNK_DURATION_NS;
        snapshot.max_request_duration_ns = RP1_GPCLK_REQUEST_DURATION_NS_MAX;
        std::strcpy(snapshot.module_id, query.module_id);
        std::strcpy(snapshot.build_id, query.build_id);
        std::strcpy(snapshot.compatibility_id, query.compatibility_id);
    }

    int openDevice(const char* value, int value_flags) noexcept override
    {
        path = value;
        flags.push_back(value_flags);
        ++opens;
        return open_result;
    }

    int control(int, unsigned long request, void* argument) noexcept override
    {
        requests.push_back(request);
        if (request == fail_request)
        {
            error = fail_error;
            return -1;
        }
        if (request == RP1_GPCLK_IOC_QUERY)
        {
            auto* value = static_cast<rp1_gpclk_query*>(argument);
            *value = query;
            if (malformed_query_size) value->header.size = 1;
        }
        else if (request == RP1_GPCLK_IOC_GET_SNAPSHOT)
        {
            auto* value = static_cast<rp1_gpclk_snapshot*>(argument);
            *value = snapshot;
        }
        else if (request == RP1_GPCLK_IOC_ACQUIRE)
        {
            acquire = *static_cast<rp1_gpclk_acquire*>(argument);
            static_cast<rp1_gpclk_acquire*>(argument)->lease_id = lease_id;
            if (nonzero_acquire_reserved)
                static_cast<rp1_gpclk_acquire*>(argument)->reserved[0] = 1;
        }
        else if (request == RP1_GPCLK_IOC_SUBMIT_EVENTS)
        {
            events = *static_cast<rp1_gpclk_submit_events*>(argument);
            static_cast<rp1_gpclk_submit_events*>(argument)->generation =
                returned_generation;
        }
        else if (request == RP1_GPCLK_IOC_GET_STATE)
        {
            auto* value = static_cast<rp1_gpclk_state_request*>(argument);
            value->state = state;
            value->terminal_reason = terminal_reason;
            value->current_event = current_event;
            value->cleanup_fault = cleanup_fault;
            value->elapsed_ns = elapsed_ns;
            value->remaining_ns = remaining_ns;
            if (stale_generation) ++value->generation;
            if (foreign_lease) ++value->lease_id;
            if (malformed_state) value->reserved[0] = 1;
        }
        else if (request == RP1_GPCLK_IOC_STOP)
            stop = *static_cast<rp1_gpclk_stop*>(argument);
        else if (request == RP1_GPCLK_IOC_RELEASE)
            release = *static_cast<rp1_gpclk_release*>(argument);
        return 0;
    }

    int closeDevice(int) noexcept override { ++closes; return close_result; }
    int lastError() const noexcept override { return error; }

    rp1_gpclk_query query{};
    rp1_gpclk_snapshot snapshot{};
    rp1_gpclk_acquire acquire{};
    rp1_gpclk_submit_events events{};
    rp1_gpclk_stop stop{};
    rp1_gpclk_release release{};
    std::string path;
    std::vector<int> flags;
    std::vector<unsigned long> requests;
    int open_result{7};
    int close_result{0};
    int error{ENOENT};
    int opens{};
    int closes{};
    unsigned long fail_request{};
    int fail_error{EINVAL};
    std::uint64_t lease_id{41};
    std::uint64_t returned_generation{9};
    std::uint32_t state{RP1_GPCLK_STATE_IDLE};
    std::uint32_t terminal_reason{RP1_GPCLK_REASON_NONE};
    std::uint32_t current_event{};
    std::uint32_t cleanup_fault{};
    std::uint64_t elapsed_ns{};
    std::uint64_t remaining_ns{};
    bool stale_generation{};
    bool foreign_lease{};
    bool malformed_query_size{};
    bool nonzero_acquire_reserved{};
    bool malformed_state{};
};

wsprrypi::Rp1GpclkProviderEventProgram oneSecondProgram()
{
    wsprrypi::Rp1GpclkProviderEventProgram program;
    program.fractional_bits = RP1_GPCLK_FRACTIONAL_BITS;
    program.tick_divider = RP1_GPCLK_TICK_DIVIDER;
    program.drive_ma = RP1_GPCLK_DRIVE_MA_2;
    program.total_duration_ns = 1000000000ULL;
    program.tones.push_back({100, 101, 1, 1});
    program.events.push_back({1000000000ULL, 0, true});
    return program;
}

void test_query_and_readiness()
{
    Io io;
    wsprrypi::Rp1GpclkLinuxProvider provider(io);
    wsprrypi::Rp1GpclkProviderIdentity identity;
    std::string error;
    expect(provider.query(RP1_GPCLK_ROUTE_GPIO4, kRequired, true, identity, error),
        "operationally ready canonical provider must query");
    expect(identity.max_event_duration_ns == RP1_GPCLK_EVENT_DURATION_NS_MAX &&
        identity.max_request_duration_ns == RP1_GPCLK_REQUEST_DURATION_NS_MAX &&
        identity.dma_chunk_duration_ns == RP1_GPCLK_DMA_CHUNK_DURATION_NS,
        "QUERY must preserve long-duration and bounded-chunk limits");
    expect(io.path == "/dev/rp1-gpclk", "only canonical endpoint may be opened");

    io.snapshot.operation_state = RP1_GPCLK_STATE_COMPLETE;
    io.snapshot.terminal_reason = RP1_GPCLK_REASON_OWNER_CLOSED;
    io.snapshot.drain_state = RP1_GPCLK_DRAIN_COMPLETE;
    expect(provider.query(RP1_GPCLK_ROUTE_GPIO4, kRequired, true, identity, error),
        "canonical completed owner-close must remain inspectable after bounded drain");
    io.snapshot.operation_state = RP1_GPCLK_STATE_IDLE;
    io.snapshot.terminal_reason = RP1_GPCLK_REASON_NONE;
    io.snapshot.drain_state = RP1_GPCLK_DRAIN_NONE;

    io.snapshot.output_inhibited = RP1_GPCLK_OBSERVATION_TRUE;
    expect(!provider.query(RP1_GPCLK_ROUTE_GPIO4, kRequired, true, identity, error),
        "output-inhibited provider must not satisfy production readiness");
    expect(provider.query(RP1_GPCLK_ROUTE_GPIO4, kRequired, false, identity, error),
        "administrative query may inspect an output-inhibited provider");
    io.snapshot.output_inhibited = RP1_GPCLK_OBSERVATION_FALSE;
    io.snapshot.operational_ready = RP1_GPCLK_OBSERVATION_FALSE;
    expect(!provider.query(RP1_GPCLK_ROUTE_GPIO4, kRequired, true, identity, error),
        "not-ready provider must fail closed");
    io.snapshot.operational_ready = RP1_GPCLK_OBSERVATION_TRUE;

    io.query.capabilities |= (1ULL << 63);
    expect(!provider.query(RP1_GPCLK_ROUTE_GPIO4, kRequired, false, identity, error),
        "unknown capability must fail closed");
    io.query.capabilities &= ~(1ULL << 63);
    io.snapshot.capabilities &= ~RP1_GPCLK_CAP_BOUNDED_DMA_CHUNKS;
    expect(!provider.query(RP1_GPCLK_ROUTE_GPIO4, kRequired, false, identity, error),
        "QUERY and passive capability identities must agree exactly");
    io.snapshot.capabilities = io.query.capabilities;
    io.malformed_query_size = true;
    expect(!provider.query(RP1_GPCLK_ROUTE_GPIO4, kRequired, false, identity, error),
        "malformed QUERY size must fail closed");
}

void test_acquire_submit_state_stop_release()
{
    Io io;
    wsprrypi::Rp1GpclkLinuxProvider provider(io);
    std::string error;
    expect(provider.acquire(RP1_GPCLK_ROUTE_GPIO4, kRequired, error),
        "ready exact-route provider must acquire without kernel policy credential");
    expect(io.acquire.expected_route == RP1_GPCLK_ROUTE_GPIO4 &&
        io.acquire.required_capabilities == kRequired &&
        io.acquire.reserved0 == 0,
        "ACQUIRE must bind route/capabilities and leave reserved policy fields zero");

    auto program = oneSecondProgram();
    auto invalid_program = program;
    invalid_program.total_duration_ns = 2;
    expect(!provider.submitEvents(invalid_program, error),
        "inconsistent event totals must fail before ioctl");
    expect(provider.submitEvents(program, error), "generic event program must submit");
    expect(program.generation == io.returned_generation &&
        io.events.total_duration_ns == 1000000000ULL &&
        io.events.tone_count == 1 && io.events.event_count == 1,
        "generic event submission must preserve bounded program identity");

    io.state = RP1_GPCLK_STATE_FAILED;
    io.terminal_reason = RP1_GPCLK_REASON_PINCTRL_FAILED;
    io.current_event = 1;
    io.cleanup_fault = 1;
    io.elapsed_ns = 250000000ULL;
    io.remaining_ns = 750000000ULL;
    const auto state = provider.eventState(program.generation);
    expect(state.completion == wsprrypi::Rp1GpclkCompletionState::failed &&
        state.terminal_reason == RP1_GPCLK_REASON_PINCTRL_FAILED &&
        state.cleanup_fault == 1 && state.elapsed_ns == 250000000ULL &&
        state.remaining_ns == 750000000ULL,
        "state must expose terminal reason, cleanup fault, and timing");

    io.cleanup_fault = 0;
    io.state = RP1_GPCLK_STATE_COMPLETE;
    io.terminal_reason = RP1_GPCLK_REASON_STOPPED;
    expect(provider.requestFiniteStop(program.generation, error),
        "STOP must bind the current generation");
    expect(io.stop.generation == program.generation,
        "STOP must preserve exact generation");
    expect(provider.release(error), "terminal generation must release");
    expect(io.release.generation == program.generation,
        "RELEASE must retain exact generation after terminal observation");
    expect(provider.endpointClosed() && provider.leaseId() == 0,
        "release must expose closed local endpoint and cleared local lease");
}

void test_exact_route_version_and_state_rejections()
{
    for (const auto route : {RP1_GPCLK_ROUTE_GPIO4, RP1_GPCLK_ROUTE_GPIO20}) {
        Io io;
        io.query.route = io.snapshot.route = route;
        const char* identity = route == RP1_GPCLK_ROUTE_GPIO4 ? "v1.0.0-rp1-gpio4" : "v1.0.0-rp1-gpio20";
        std::strcpy(io.query.compatibility_id, identity);
        std::strcpy(io.snapshot.compatibility_id, identity);
        wsprrypi::Rp1GpclkLinuxProvider provider(io);
        wsprrypi::Rp1GpclkProviderIdentity observed;
        std::string error;
        expect(provider.query(route, kRequired, true, observed, error), "each exact candidate route must query independently");
        for (const auto wrong : {"v0.9.0-pi5", "v1.0.0-rp1-gpio4", "v1.0.0-rp1-gpio20"}) {
            if (std::strcmp(wrong, identity) == 0) continue;
            std::strcpy(io.query.compatibility_id, wrong);
            expect(!provider.query(route, kRequired, true, observed, error), "legacy or substituted route identity must be rejected");
        }
        std::strcpy(io.query.compatibility_id, identity);
        std::strcpy(io.query.build_id, "0.9.0");
        expect(!provider.query(route, kRequired, true, observed, error), "stale module version must be rejected");
        std::strcpy(io.query.build_id, "1.0.0");
        expect(provider.acquire(route, kRequired, error), "exact version and route restore acquisition");
        auto program = oneSecondProgram();
        expect(provider.submitEvents(program, error), "state rejection fixture submits");
        io.fail_request = RP1_GPCLK_IOC_GET_STATE; io.fail_error = EIO;
        auto state = provider.eventState(program.generation);
        expect(state.completion == wsprrypi::Rp1GpclkCompletionState::failed &&
               state.error.find("Could not get") != std::string::npos,
               "GET_STATE failure must retain provider detail");
        io.fail_request = 0; io.malformed_state = true;
        state = provider.eventState(program.generation);
        expect(state.completion == wsprrypi::Rp1GpclkCompletionState::failed &&
               state.error.find("reserved") != std::string::npos,
               "malformed current state must retain its rejection detail");
    }
}

void test_fail_closed_acquire_and_snapshot()
{
    Io io;
    wsprrypi::Rp1GpclkLinuxProvider provider(io);
    std::string error;
    io.snapshot.cleanup_fault = RP1_GPCLK_OBSERVATION_TRUE;
    expect(!provider.acquire(RP1_GPCLK_ROUTE_GPIO4, kRequired, error),
        "latched cleanup fault must block acquisition");
    io.snapshot.cleanup_fault = RP1_GPCLK_OBSERVATION_FALSE;
    io.snapshot.dma_quiescent = RP1_GPCLK_OBSERVATION_FALSE;
    expect(!provider.acquire(RP1_GPCLK_ROUTE_GPIO4, kRequired, error),
        "non-quiescent DMA must block acquisition independently of readiness");
    io.snapshot.dma_quiescent = RP1_GPCLK_OBSERVATION_TRUE;
    io.nonzero_acquire_reserved = true;
    expect(!provider.acquire(RP1_GPCLK_ROUTE_GPIO4, kRequired, error),
        "nonzero ACQUIRE reserved data must fail closed");
    io.nonzero_acquire_reserved = false;
    io.snapshot.reserved[0] = 1;
    wsprrypi::Rp1GpclkPassiveSnapshot snapshot;
    expect(!provider.passiveSnapshot(snapshot, error),
        "nonzero passive reserved data must fail closed");
    io.snapshot.reserved[0] = 0;
    io.fail_request = RP1_GPCLK_IOC_GET_SNAPSHOT;
    io.fail_error = ENOTTY;
    expect(!provider.passiveSnapshot(snapshot, error) &&
        error.find("does not support") != std::string::npos,
        "unsupported snapshot must be explicit");
}
}

int main()
{
    test_query_and_readiness();
    test_acquire_submit_state_stop_release();
    test_exact_route_version_and_state_rejections();
    test_fail_closed_acquire_and_snapshot();
    if (failures) return 1;
    std::cout << "RP1 GPCLK canonical UAPI provider tests passed\n";
}
