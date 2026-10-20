// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace wsprrypi::wtp {

enum class Operation {
    Hello,
    Caps,
    Claim,
    Renew,
    Release,
    Load,
    Arm,
    Abort,
    Status,
    GetClock,
    Ping
};
enum class State { Empty, Loaded, Armed, Running, Complete, Aborted, Missed, Failed };
enum class Mode { Wspr, Qrss, Fskcw, Dfcw, Cw, Tone };
enum class ClockState { Unsynchronized, Synchronized, Holdover };
enum class Leap { Normal, InsertPending, DeletePending, Unknown };
enum class EventKind {
    JobState,
    MissedStart,
    OwnerReleased,
    DeviceFault,
    InvalidFrame,
    SessionReplaced
};
enum class ReleaseReason { Released, LeaseExpired, Terminal };
enum class ErrorCode {
    InvalidFrame,
    InvalidMessage,
    UnsupportedVersion,
    HelloRequired,
    UnknownOperation,
    AuthenticationRequired,
    SessionReplaced,
    Busy,
    NotOwner,
    LeaseExpired,
    RequestIdReuse,
    InvalidState,
    JobNotFound,
    JobIdConflict,
    JobLimitExceeded,
    UnsupportedProfile,
    UnsupportedMode,
    FrequencyRejected,
    ArmConflict,
    ClockUnsynchronized,
    ClockUncertain,
    LeapUnsafe,
    ArmTooLate,
    ArmTooFar,
    MissedStart,
    OutputStateUnknown,
    DeviceFault,
    InternalError
};

struct Empty {};
struct HelloRequest {
    std::vector<std::string> versions;
    std::string client_name, client_version;
};
struct LeaseRequest {
    std::string owner_id;
    std::int32_t lease_ms{};
};
struct RfEvent {
    std::uint64_t offset_ns{}, duration_ns{};
    bool rf_on{};
    std::optional<std::uint64_t> frequency_nhz;
};
struct Job {
    std::string job_id;
    Mode mode{};
    std::uint64_t total_duration_ns{};
    std::vector<RfEvent> events;
    std::optional<bool> allow_frequency_adjustment;
};
struct ArmRequest {
    std::string job_id;
    std::uint64_t start_utc_ns{}, max_start_uncertainty_ns{};
};
struct AbortRequest {
    std::string job_id;
};
struct Ping {
    std::optional<std::string> token;
};
using RequestBody =
    std::variant<Empty, HelloRequest, LeaseRequest, Job, ArmRequest, AbortRequest, Ping>;
struct Request {
    std::string session_id, request_id;
    Operation op{};
    RequestBody body;
};

struct Clock {
    ClockState state{};
    std::uint64_t utc_now_ns{}, monotonic_now_ns{}, uncertainty_ns{}, sync_age_ns{};
    Leap leap{};
    std::optional<std::uint64_t> leap_transition_utc_ns;
};
struct HelloResponse {
    std::string device_id, boot_id, product, firmware_version;
};
struct FrequencyRange {
    std::uint64_t minimum_nhz{}, maximum_nhz{};
};
struct Capabilities {
    std::vector<std::string> profiles;
    std::vector<Mode> modes;
    std::string engine;
    std::vector<FrequencyRange> frequency_ranges;
    std::int32_t max_payload_bytes{}, max_events{};
    std::uint64_t max_job_duration_ns{}, minimum_arm_lead_ns{}, maximum_arm_ahead_ns{},
        maximum_arm_uncertainty_ns{}, maximum_holdover_age_ns{}, output_disable_timeout_ns{};
    std::int32_t minimum_lease_ms{}, maximum_lease_ms{}, response_cache_entries{},
        response_cache_ttl_seconds{}, terminal_record_entries{}, terminal_record_ttl_seconds{};
};
struct LeaseResponse {
    std::string owner_id;
    std::int32_t granted_lease_ms{};
    std::uint64_t expires_monotonic_ns{};
};
struct Adjustment {
    std::int32_t event_index{};
    std::uint64_t requested_frequency_nhz{}, realized_frequency_nhz{};
};
struct LoadResponse {
    std::string job_id;
    std::vector<Adjustment> adjustments;
};
struct ArmResponse {
    std::string job_id;
    std::uint64_t start_utc_ns{}, start_monotonic_ns{};
    Clock clock;
};
struct AbortResponse {
    std::string job_id;
}; // Only an aborted/inactive acknowledgment decodes.
struct Error {
    ErrorCode code{};
    std::string message;
    bool retryable{};
    // Owned, strict JSON object. Diagnostic only; never a state-machine input.
    std::optional<std::string> detail_json;
};
struct TerminalRecord {
    std::string job_id;
    State state{};
    std::uint64_t ended_monotonic_ns{};
    bool output_active{};
    std::optional<Error> error;
};
struct Status {
    std::string boot_id;
    State state{};
    bool output_active{};
    std::optional<std::string> owner_id, job_id;
    std::vector<TerminalRecord> terminal_records;
};
using ResponseBody = std::variant<Empty, HelloResponse, Capabilities, LeaseResponse, LoadResponse,
                                  ArmResponse, AbortResponse, Status, Clock, Ping, Error>;
struct Response {
    std::string session_id, request_id;
    // Errors may echo an unknown operation; successes only decode defined operations.
    std::string op;
    ResponseBody body;
    bool ok() const noexcept { return !std::holds_alternative<Error>(body); }
};
struct JobStateEvent {
    std::optional<std::string> job_id;
    State state{};
    bool output_active{};
    std::optional<Error> error;
};
struct OwnerReleasedEvent {
    std::string owner_id;
    ReleaseReason reason{};
    bool output_active{};
};
struct ErrorEvent {
    Error error;
};
using EventBody = std::variant<JobStateEvent, OwnerReleasedEvent, ErrorEvent>;
struct Event {
    std::string session_id, boot_id;
    std::uint64_t event_id{};
    EventKind event{};
    EventBody body;
};
using Message = std::variant<Request, Response, Event>;

// Owns all decoded data; no views into the input survive. Rejects schema and
// universal arithmetic violations, but does not implement session/job policy.
struct DecodeResult {
    std::optional<Message> message;
    std::string error;
    explicit operator bool() const noexcept { return message.has_value(); }
};
DecodeResult decode(std::string_view payload);
struct EncodeResult {
    std::optional<std::string> payload;
    std::string error;
    explicit operator bool() const noexcept { return payload.has_value(); }
};
// Validates type/operation agreement and wire limits before returning bytes.
// Retain these bytes unchanged for retries; this function creates no identities.
EncodeResult encode_request(const Request &request);
std::string_view operation_name(Operation op);

} // namespace wsprrypi::wtp
