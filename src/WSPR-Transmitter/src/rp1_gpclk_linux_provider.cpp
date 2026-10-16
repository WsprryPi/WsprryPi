#include "rp1_gpclk_linux_provider.hpp"
#include "rp1_gpclk_uapi.h"
#include "rp1_gpclk_consumer_contract.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace wsprrypi
{
namespace
{
constexpr std::uint64_t kKnownCapabilities =
    RP1_GPCLK_CAP_SUBMIT_EVENTS |
    RP1_GPCLK_CAP_STOP_DRAIN | RP1_GPCLK_CAP_STABLE_STATE |
    RP1_GPCLK_CAP_ROUTE_IDENTITY | RP1_GPCLK_CAP_COMPAT_IDENTITY |
    RP1_GPCLK_CAP_CLEANUP_FAULT_LATCH | RP1_GPCLK_CAP_OUTPUT_INHIBIT |
    RP1_GPCLK_CAP_PASSIVE_SNAPSHOT | RP1_GPCLK_CAP_BOUNDED_DMA_CHUNKS;

std::uint32_t driveMask(std::uint32_t drive_ma)
{
    switch (drive_ma)
    {
    case RP1_GPCLK_DRIVE_MA_2: return RP1_GPCLK_DRIVE_SUPPORT_2_MA;
    case RP1_GPCLK_DRIVE_MA_4: return RP1_GPCLK_DRIVE_SUPPORT_4_MA;
    case RP1_GPCLK_DRIVE_MA_8: return RP1_GPCLK_DRIVE_SUPPORT_8_MA;
    case RP1_GPCLK_DRIVE_MA_12: return RP1_GPCLK_DRIVE_SUPPORT_12_MA;
    default: return 0;
    }
}

template <typename Request> void initializeHeader(Request& request)
{
    request.header.size = sizeof(Request);
}

bool boundedIdentity(const char* value, std::size_t size, std::string& output)
{
    const void* terminator = std::memchr(value, '\0', size);
    if (terminator == nullptr) return false;
    output.assign(value, static_cast<const char*>(terminator));
    return !output.empty();
}

bool knownCompatibility(std::uint32_t value)
{ return value >= RP1_GPCLK_COMPAT_QUALIFIED && value <= RP1_GPCLK_COMPAT_REJECTED; }
bool knownReason(std::uint32_t value)
{ return value <= RP1_GPCLK_COMPAT_REASON_CLEANUP_LATCHED; }
bool knownRoute(std::uint32_t value)
{ return value == RP1_GPCLK_ROUTE_GPIO4 || value == RP1_GPCLK_ROUTE_GPIO20; }
bool knownState(std::uint32_t value) { return value <= RP1_GPCLK_STATE_DEAD; }
bool knownTerminalReason(std::uint32_t value)
{ return value <= RP1_GPCLK_REASON_INTERNAL_ERROR; }
bool knownObservation(std::uint32_t value)
{ return value <= RP1_GPCLK_OBSERVATION_TRUE; }
bool knownDrainState(std::uint32_t value)
{ return value <= RP1_GPCLK_DRAIN_COMPLETE; }
bool validStateReason(std::uint32_t state, std::uint32_t reason)
{
    if (state == RP1_GPCLK_STATE_IDLE || state == RP1_GPCLK_STATE_RUNNING ||
        state == RP1_GPCLK_STATE_DRAINING)
        return reason == RP1_GPCLK_REASON_NONE;
    if (state == RP1_GPCLK_STATE_COMPLETE)
        return reason == RP1_GPCLK_REASON_COMPLETE || reason == RP1_GPCLK_REASON_STOPPED ||
            reason == RP1_GPCLK_REASON_OWNER_CLOSED;
    return reason != RP1_GPCLK_REASON_NONE && reason != RP1_GPCLK_REASON_COMPLETE;
}
}

int Rp1GpclkPosixIo::openDevice(const char* path, int flags) noexcept
{ return ::open(path, flags | O_CLOEXEC); }
int Rp1GpclkPosixIo::control(int fd, unsigned long request, void* argument) noexcept
{ return ::ioctl(fd, request, argument); }
int Rp1GpclkPosixIo::closeDevice(int fd) noexcept { return ::close(fd); }
int Rp1GpclkPosixIo::lastError() const noexcept { return errno; }

Rp1GpclkLinuxProvider::Rp1GpclkLinuxProvider(
    Rp1GpclkIo& io, std::string device) noexcept
    : io_(io), device_(std::move(device)) {}
Rp1GpclkLinuxProvider::~Rp1GpclkLinuxProvider()
{
    std::string ignored;
    (void)release(ignored);
}

bool Rp1GpclkLinuxProvider::failed(const char* operation, std::string& error) const
{
    error = std::string(operation) + ": " + std::strerror(io_.lastError());
    return false;
}

bool Rp1GpclkLinuxProvider::queryOpen(
    std::uint32_t expected_route, std::uint64_t required_capabilities,
    Rp1GpclkProviderIdentity& identity,
    std::string& error)
{
    rp1_gpclk_query request{};
    initializeHeader(request);
    if (io_.control(fd_, RP1_GPCLK_IOC_QUERY, &request) < 0)
    {
        failed("Could not query RP1 GPCLK provider", error);
        return false;
    }
    if (request.header.size != sizeof(request) ||
        request.header.reserved != 0 || request.header.flags != 0)
    {
        error = "RP1 GPCLK provider UAPI identity mismatch."; return false;
    }
    if (!knownRoute(request.route) ||
        (expected_route != RP1_GPCLK_ROUTE_INVALID && request.route != expected_route))
    {
        error = "RP1 GPCLK provider reported an unexpected administrative route."; return false;
    }
    if (!knownCompatibility(request.compatibility_state) ||
        !knownReason(request.compatibility_reason))
    {
        error = "RP1 GPCLK provider reported an unknown compatibility state or reason."; return false;
    }
    if ((request.capabilities & ~kKnownCapabilities) != 0 ||
        (request.capabilities & required_capabilities) != required_capabilities)
    {
        error = "RP1 GPCLK provider is missing required capabilities or reported unknown capabilities."; return false;
    }
    if (request.compatibility_state == RP1_GPCLK_COMPAT_UNAVAILABLE ||
        request.compatibility_state == RP1_GPCLK_COMPAT_REJECTED)
    {
        error = "RP1 GPCLK provider compatibility is unavailable or rejected."; return false;
    }
    if (request.max_tones != RP1_GPCLK_MAX_TONES ||
        request.max_events != RP1_GPCLK_MAX_EVENTS ||
        request.max_dither_period != RP1_GPCLK_DITHER_PERIOD_MAX ||
        (request.supported_drive_ma_mask & ~RP1_GPCLK_DRIVE_SUPPORT_ALLOWED_MASK) != 0 ||
        request.max_event_duration_ns != RP1_GPCLK_EVENT_DURATION_NS_MAX ||
        request.max_request_duration_ns != RP1_GPCLK_REQUEST_DURATION_NS_MAX ||
        request.dma_chunk_duration_ns != RP1_GPCLK_DMA_CHUNK_DURATION_NS ||
        request.reserved0 != 0)
    {
        error = "RP1 GPCLK provider reported an incompatible limit or reserved value."; return false;
    }
    for (const auto value : request.reserved)
        if (value != 0) { error = "RP1 GPCLK provider reported nonzero reserved QUERY data."; return false; }

    identity = {};
    identity.route = request.route;
    identity.compatibility_state = request.compatibility_state;
    identity.compatibility_reason = request.compatibility_reason;
    identity.capabilities = request.capabilities;
    identity.supported_drive_ma_mask = request.supported_drive_ma_mask;
    identity.max_event_duration_ns = request.max_event_duration_ns;
    identity.max_request_duration_ns = request.max_request_duration_ns;
    identity.dma_chunk_duration_ns = request.dma_chunk_duration_ns;
    if (!boundedIdentity(request.module_id, sizeof(request.module_id), identity.module_id) ||
        !boundedIdentity(request.build_id, sizeof(request.build_id), identity.build_id) ||
        !boundedIdentity(request.compatibility_id, sizeof(request.compatibility_id), identity.compatibility_id))
    {
        error = "RP1 GPCLK provider returned an empty or unterminated identity."; return false;
    }
    if (identity.module_id != Rp1GpclkConsumerContract::module ||
        identity.build_id != Rp1GpclkConsumerContract::version ||
        identity.compatibility_id != Rp1GpclkConsumerContract::compatibility(identity.route)) {
        error = "RP1 GPCLK module/version/route compatibility identity is not accepted.";
        return false;
    }
    return true;
}

bool Rp1GpclkLinuxProvider::query(
    std::uint32_t expected_route, std::uint64_t required_capabilities,
    bool require_operational_ready, Rp1GpclkProviderIdentity& identity,
    std::string& error)
{
    if (fd_ >= 0) { error = "RP1 GPCLK provider query requires no active lease."; return false; }
    fd_ = io_.openDevice(device_.c_str(), O_RDWR);
    if (fd_ < 0) return failed("Could not open canonical RP1 GPCLK provider", error);
    const bool ok = queryOpen(expected_route, required_capabilities, identity, error);
    const int close_result = io_.closeDevice(fd_);
    fd_ = -1;
    if (close_result < 0) { error = "Could not close RP1 GPCLK provider after QUERY."; return false; }
    if (!ok) return false;
    Rp1GpclkPassiveSnapshot snapshot;
    if (!passiveSnapshot(snapshot, error)) return false;
    if (snapshot.route != identity.route ||
        snapshot.compatibility_state != identity.compatibility_state ||
        snapshot.compatibility_reason != identity.compatibility_reason ||
        snapshot.capabilities != identity.capabilities ||
        snapshot.module_id != identity.module_id || snapshot.build_id != identity.build_id ||
        snapshot.compatibility_id != identity.compatibility_id)
    {
        error = "RP1 GPCLK QUERY and passive snapshot identities disagree: query=" +
            std::to_string(identity.route) + "/" +
            std::to_string(identity.compatibility_state) + "/" +
            std::to_string(identity.compatibility_reason) + "/" + identity.module_id +
            "/" + identity.build_id + "/" + identity.compatibility_id +
            "; snapshot=" + std::to_string(snapshot.route) + "/" +
            std::to_string(snapshot.compatibility_state) + "/" +
            std::to_string(snapshot.compatibility_reason) + "/" + snapshot.module_id +
            "/" + snapshot.build_id + "/" + snapshot.compatibility_id;
        return false;
    }
    if ((identity.capabilities & required_capabilities) != required_capabilities)
    {
        error = "RP1 GPCLK provider is missing a required passive capability.";
        return false;
    }
    if (require_operational_ready &&
        (snapshot.output_inhibited != RP1_GPCLK_OBSERVATION_FALSE ||
         snapshot.operational_ready != RP1_GPCLK_OBSERVATION_TRUE ||
         snapshot.cleanup_fault != RP1_GPCLK_OBSERVATION_FALSE ||
         snapshot.owner_present != RP1_GPCLK_OBSERVATION_FALSE ||
         snapshot.lease_present != RP1_GPCLK_OBSERVATION_FALSE ||
         snapshot.gpio_safe != RP1_GPCLK_OBSERVATION_TRUE ||
         snapshot.clock_quiescent != RP1_GPCLK_OBSERVATION_TRUE ||
         snapshot.dma_quiescent != RP1_GPCLK_OBSERVATION_TRUE ||
         snapshot.stable != RP1_GPCLK_OBSERVATION_TRUE))
    {
        error = "RP1 GPCLK provider is not operationally ready for production output.";
        return false;
    }
    return true;
}

bool Rp1GpclkLinuxProvider::passiveSnapshot(
    Rp1GpclkPassiveSnapshot& result, std::string& error) const
{
    if (fd_ >= 0)
    {
        error = "Passive RP1 GPCLK inspection requires no owned provider descriptor.";
        return false;
    }
    const int descriptor = io_.openDevice(device_.c_str(), O_RDONLY | O_NONBLOCK);
    if (descriptor < 0) return failed("Could not open canonical RP1 GPCLK provider read-only", error);
    rp1_gpclk_snapshot request{};
    initializeHeader(request);
    const int control_result = io_.control(
        descriptor, RP1_GPCLK_IOC_GET_SNAPSHOT, &request);
    const int saved_error = io_.lastError();
    const int close_result = io_.closeDevice(descriptor);
    if (control_result < 0)
    {
        error = saved_error == EOPNOTSUPP || saved_error == ENOTTY
            ? "RP1 GPCLK provider does not support the passive snapshot."
            : "Could not read passive RP1 GPCLK snapshot: " +
                std::string(std::strerror(saved_error));
        return false;
    }
    if (close_result < 0)
    {
        error = "Could not close RP1 GPCLK passive inspector descriptor.";
        return false;
    }
    if (request.header.size != sizeof(request) ||
        request.header.reserved != 0 || request.header.flags != 0 ||
        !knownRoute(request.route) ||
        !knownCompatibility(request.compatibility_state) ||
        !knownReason(request.compatibility_reason) ||
        !knownState(request.operation_state) ||
        !knownTerminalReason(request.terminal_reason) ||
        !validStateReason(request.operation_state, request.terminal_reason) ||
        !knownDrainState(request.drain_state) ||
        request.snapshot_flags & ~RP1_GPCLK_SNAPSHOT_F_ALLOWED_MASK ||
        request.capabilities & ~kKnownCapabilities ||
        (request.capabilities & RP1_GPCLK_CAP_PASSIVE_SNAPSHOT) == 0 ||
        request.dma_chunk_duration_ns != RP1_GPCLK_DMA_CHUNK_DURATION_NS ||
        request.max_request_duration_ns != RP1_GPCLK_REQUEST_DURATION_NS_MAX ||
        request.reserved0 != 0)
    {
        error = "RP1 GPCLK provider returned malformed or unknown passive snapshot data.";
        return false;
    }
    for (const auto value : request.reserved)
        if (value != 0)
        {
            error = "RP1 GPCLK provider returned nonzero passive snapshot reserved data.";
            return false;
        }
    for (const auto value : {request.cleanup_fault, request.owner_present,
            request.lease_present, request.output_inhibited,
            request.operational_ready,
            request.gpio_safe, request.clock_quiescent, request.dma_quiescent,
            request.stable})
        if (!knownObservation(value))
        {
            error = "RP1 GPCLK provider returned an unknown passive safety observation.";
            return false;
        }
    if ((request.snapshot_flags & RP1_GPCLK_SNAPSHOT_F_REMAINING_VALID) &&
        request.remaining_ns > RP1_GPCLK_REQUEST_DURATION_NS_MAX)
    {
        error = "RP1 GPCLK provider returned an out-of-bounds remaining duration.";
        return false;
    }
    result = {};
    result.route = request.route;
    result.compatibility_state = request.compatibility_state;
    result.compatibility_reason = request.compatibility_reason;
    result.operation_state = request.operation_state;
    result.terminal_reason = request.terminal_reason;
    result.current_event = request.current_event;
    result.validity_flags = request.snapshot_flags;
    result.cleanup_fault = request.cleanup_fault;
    result.owner_present = request.owner_present;
    result.lease_present = request.lease_present;
    result.output_inhibited = request.output_inhibited;
    result.operational_ready = request.operational_ready;
    result.drain_state = request.drain_state;
    result.gpio_safe = request.gpio_safe;
    result.clock_quiescent = request.clock_quiescent;
    result.dma_quiescent = request.dma_quiescent;
    result.stable = request.stable;
    result.capabilities = request.capabilities;
    result.generation = request.generation;
    result.elapsed_ns = request.elapsed_ns;
    result.remaining_ns = request.remaining_ns;
    result.dma_chunk_duration_ns = request.dma_chunk_duration_ns;
    result.max_request_duration_ns = request.max_request_duration_ns;
    if (!boundedIdentity(request.module_id, sizeof(request.module_id), result.module_id) ||
        !boundedIdentity(request.build_id, sizeof(request.build_id), result.build_id) ||
        !boundedIdentity(request.compatibility_id, sizeof(request.compatibility_id),
            result.compatibility_id))
    {
        error = "RP1 GPCLK provider returned an empty or unterminated passive identity.";
        return false;
    }
    return true;
}

bool Rp1GpclkLinuxProvider::acquire(
    std::uint32_t expected_route, std::uint64_t required_capabilities,
    std::string& error)
{
    if (fd_ >= 0) { error = "RP1 GPCLK provider is already acquired."; return false; }
    Rp1GpclkPassiveSnapshot snapshot;
    if (!passiveSnapshot(snapshot, error)) return false;
    if (snapshot.route != expected_route ||
        snapshot.compatibility_state == RP1_GPCLK_COMPAT_UNAVAILABLE ||
        snapshot.compatibility_state == RP1_GPCLK_COMPAT_REJECTED ||
        snapshot.output_inhibited != RP1_GPCLK_OBSERVATION_FALSE ||
        snapshot.operational_ready != RP1_GPCLK_OBSERVATION_TRUE ||
        snapshot.cleanup_fault != RP1_GPCLK_OBSERVATION_FALSE ||
        snapshot.owner_present != RP1_GPCLK_OBSERVATION_FALSE ||
        snapshot.lease_present != RP1_GPCLK_OBSERVATION_FALSE ||
        snapshot.gpio_safe != RP1_GPCLK_OBSERVATION_TRUE ||
        snapshot.clock_quiescent != RP1_GPCLK_OBSERVATION_TRUE ||
        snapshot.dma_quiescent != RP1_GPCLK_OBSERVATION_TRUE ||
        snapshot.stable != RP1_GPCLK_OBSERVATION_TRUE ||
        (snapshot.capabilities & required_capabilities) != required_capabilities)
    {
        error = "RP1 GPCLK provider is not operationally ready for acquisition.";
        return false;
    }
    fd_ = io_.openDevice(device_.c_str(), O_RDWR);
    if (fd_ < 0) return failed("Could not open canonical RP1 GPCLK provider", error);
    Rp1GpclkProviderIdentity identity;
    if (!queryOpen(expected_route, required_capabilities, identity, error))
    {
        (void)io_.closeDevice(fd_); fd_ = -1; return false;
    }
    rp1_gpclk_acquire request{};
    initializeHeader(request);
    request.expected_route = expected_route;
    request.required_capabilities = required_capabilities;
    if (io_.control(fd_, RP1_GPCLK_IOC_ACQUIRE, &request) < 0)
    {
        failed("Could not acquire RP1 GPCLK operation lease", error);
        (void)io_.closeDevice(fd_); fd_ = -1; return false;
    }
    if (request.header.size != sizeof(request) ||
        request.header.reserved != 0 || request.header.flags != 0 ||
        request.reserved0 != 0 || request.lease_id == 0)
    {
        error = "RP1 GPCLK provider returned an invalid lease identity.";
        (void)io_.closeDevice(fd_); fd_ = -1; return false;
    }
    for (const auto value : request.reserved)
        if (value != 0)
        {
            error = "RP1 GPCLK provider returned nonzero reserved ACQUIRE data.";
            (void)io_.closeDevice(fd_); fd_ = -1; return false;
        }
    lease_id_ = request.lease_id;
    supported_drive_ma_mask_ = identity.supported_drive_ma_mask;
    return true;
}

bool Rp1GpclkLinuxProvider::submitEvents(
    Rp1GpclkProviderEventProgram& source, std::string& error)
{
    if (fd_ < 0 || lease_id_ == 0) { error = "RP1 GPCLK provider is not acquired."; return false; }
    const auto required_drive = driveMask(source.drive_ma);
    if (required_drive == 0 || (supported_drive_ma_mask_ & required_drive) == 0)
    { error = "RP1 GPCLK provider does not support the requested drive strength."; return false; }
    if (source.tones.empty() || source.tones.size() > RP1_GPCLK_MAX_TONES ||
        source.events.empty() || source.events.size() > RP1_GPCLK_MAX_EVENTS)
    { error = "RP1 GPCLK event program exceeds the UAPI bounds."; return false; }
    std::uint64_t total_duration_ns = 0;
    for (const auto& event : source.events)
    {
        if (event.duration_ns < RP1_GPCLK_EVENT_DURATION_NS_MIN ||
            event.duration_ns > RP1_GPCLK_EVENT_DURATION_NS_MAX ||
            (event.rf_on && event.tone_index >= source.tones.size()) ||
            event.duration_ns > RP1_GPCLK_REQUEST_DURATION_NS_MAX -
                total_duration_ns)
        {
            error = "RP1 GPCLK event program contains an invalid event or duration.";
            return false;
        }
        total_duration_ns += event.duration_ns;
    }
    if (total_duration_ns != source.total_duration_ns)
    {
        error = "RP1 GPCLK event program total duration is inconsistent.";
        return false;
    }
    std::vector<rp1_gpclk_tone> tones(source.tones.size());
    for (std::size_t i = 0; i < source.tones.size(); ++i)
        tones[i] = {source.tones[i].lower_divider_word, source.tones[i].upper_divider_word,
            source.tones[i].lower_count, source.tones[i].upper_count};
    std::vector<rp1_gpclk_event> events(source.events.size());
    for (std::size_t i = 0; i < source.events.size(); ++i)
    {
        events[i].duration_ns = source.events[i].duration_ns;
        events[i].tone_index = source.events[i].tone_index;
        events[i].flags = source.events[i].rf_on ? RP1_GPCLK_EVENT_F_OUTPUT_ENABLED : 0;
    }
    rp1_gpclk_submit_events request{};
    initializeHeader(request);
    request.lease_id = lease_id_; request.generation = 0;
    request.tones_ptr = reinterpret_cast<std::uintptr_t>(tones.data());
    request.events_ptr = reinterpret_cast<std::uintptr_t>(events.data());
    request.fractional_bits = source.fractional_bits;
    request.tick_divider = source.tick_divider; request.tone_count = tones.size();
    request.event_count = events.size(); request.drive_ma = source.drive_ma;
    request.total_duration_ns = source.total_duration_ns;
    if (io_.control(fd_, RP1_GPCLK_IOC_SUBMIT_EVENTS, &request) < 0)
        return failed("Could not submit RP1 GPCLK event program", error);
    if (request.generation == 0)
    { error = "RP1 GPCLK provider returned an invalid event generation identity."; return false; }
    source.generation = request.generation;
    active_generation_ = request.generation;
    return true;
}

bool Rp1GpclkLinuxProvider::requestFiniteStop(std::uint64_t generation, std::string& error)
{
    rp1_gpclk_stop request{}; initializeHeader(request);
    request.lease_id = lease_id_; request.generation = generation;
    if (fd_ < 0 || lease_id_ == 0)
        return failed("Could not request RP1 GPCLK finite stop", error);
    if (io_.control(fd_, RP1_GPCLK_IOC_STOP, &request) < 0)
    {
        if (io_.lastError() != EALREADY)
            return failed("Could not request RP1 GPCLK finite stop", error);
        Rp1GpclkProviderEventState state_result;
        if (!getState(generation, state_result, error) ||
            (state_result.completion != Rp1GpclkCompletionState::complete &&
             state_result.completion != Rp1GpclkCompletionState::failed &&
             state_result.completion != Rp1GpclkCompletionState::dead))
        {
            if (error.empty())
                error = "RP1 GPCLK finite stop reported an already-finished operation without authenticated terminal state.";
            return false;
        }
    }
    return true;
}

bool Rp1GpclkLinuxProvider::getState(
    std::uint64_t generation,
    Rp1GpclkProviderEventState& state_result,
    std::string& error) const
{
    rp1_gpclk_state_request request{}; initializeHeader(request);
    request.lease_id = lease_id_; request.generation = generation;
    if (fd_ < 0 || lease_id_ == 0)
    {
        error = "RP1 GPCLK state query requires an owned lease.";
        return false;
    }
    if (io_.control(fd_, RP1_GPCLK_IOC_GET_STATE, &request) < 0)
        return failed("Could not get RP1 GPCLK generation state", error);
    if (request.lease_id != lease_id_ || request.generation != generation ||
        request.header.size != sizeof(request) || request.header.flags != 0 ||
        request.header.reserved != 0 ||
        !knownState(request.state) || !knownTerminalReason(request.terminal_reason) ||
        !validStateReason(request.state, request.terminal_reason) ||
        request.cleanup_fault > 1)
    {
        error = "RP1 GPCLK provider returned stale or unknown state data.";
        return false;
    }
    for (const auto value : request.reserved)
        if (value != 0) {
            error = "RP1 GPCLK provider returned nonzero reserved state data.";
            return false;
        }
    Rp1GpclkCompletionState completion = Rp1GpclkCompletionState::failed;
    switch (request.state)
    {
    case RP1_GPCLK_STATE_IDLE: completion = Rp1GpclkCompletionState::idle; break;
    case RP1_GPCLK_STATE_RUNNING: completion = Rp1GpclkCompletionState::running; break;
    case RP1_GPCLK_STATE_DRAINING: completion = Rp1GpclkCompletionState::draining; break;
    case RP1_GPCLK_STATE_COMPLETE: completion = Rp1GpclkCompletionState::complete; break;
    case RP1_GPCLK_STATE_DEAD: completion = Rp1GpclkCompletionState::failed; break;
    default: break;
    }
    if (request.cleanup_fault != 0) completion = Rp1GpclkCompletionState::failed;
    state_result = {completion, request.current_event, request.terminal_reason,
        request.cleanup_fault, request.elapsed_ns, request.remaining_ns};
    return true;
}

Rp1GpclkProviderEventState Rp1GpclkLinuxProvider::eventState(std::uint64_t generation) const noexcept
{
    Rp1GpclkProviderEventState result{
        Rp1GpclkCompletionState::failed, 0, RP1_GPCLK_REASON_INTERNAL_ERROR,
        1, 0, 0};
    try {
        std::string detail;
        if (!getState(generation, result, detail)) result.error = std::move(detail);
    } catch (const std::exception& error) {
        result.error = error.what();
    }
    return result;
}

Rp1GpclkCompletionState Rp1GpclkLinuxProvider::state(std::uint64_t generation) const noexcept
{ return eventState(generation).completion; }

bool Rp1GpclkLinuxProvider::release(std::string& error) noexcept
{
    if (fd_ < 0) return true;
    bool ok = true;
    if (lease_id_ != 0)
    {
        rp1_gpclk_release request{};
        initializeHeader(request);
        request.lease_id = lease_id_;
        request.generation = active_generation_;
        const int release_result =
            io_.control(fd_, RP1_GPCLK_IOC_RELEASE, &request);
        if (release_result < 0)
        {
            error = "Could not release the owned RP1 GPCLK lease: " +
                std::string(std::strerror(io_.lastError()));
            ok = false;
        }
    }
    if (io_.closeDevice(fd_) < 0)
    {
        if (!error.empty()) error += " ";
        error += "Could not close the RP1 GPCLK endpoint.";
        ok = false;
    }
    lease_id_ = 0; active_generation_ = 0;
    supported_drive_ma_mask_ = 0; fd_ = -1;
    return ok;
}
} // namespace wsprrypi
