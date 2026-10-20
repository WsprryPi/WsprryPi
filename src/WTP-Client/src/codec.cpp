// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "wtp/codec.hpp"
#include "detail/json.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace wsprrypi::wtp {
namespace {
using json::Value;
constexpr std::array operations{"HELLO", "CAPS",  "CLAIM",  "RENEW",     "RELEASE", "LOAD",
                                "ARM",   "ABORT", "STATUS", "GET_CLOCK", "PING"};
constexpr std::array states{"empty",    "loaded",  "armed",  "running",
                            "complete", "aborted", "missed", "failed"};
constexpr std::array modes{"wspr", "qrss", "fskcw", "dfcw", "cw", "tone"};
constexpr std::array clock_states{"unsynchronized", "synchronized", "holdover"};
constexpr std::array leaps{"normal", "insert_pending", "delete_pending", "unknown"};
constexpr std::array events{"JOB_STATE",    "MISSED_START",  "OWNER_RELEASED",
                            "DEVICE_FAULT", "INVALID_FRAME", "SESSION_REPLACED"};
constexpr std::array reasons{"released", "lease_expired", "terminal"};
constexpr std::array errors{"INVALID_FRAME",       "INVALID_MESSAGE",
                            "UNSUPPORTED_VERSION", "HELLO_REQUIRED",
                            "UNKNOWN_OPERATION",   "AUTHENTICATION_REQUIRED",
                            "SESSION_REPLACED",    "BUSY",
                            "NOT_OWNER",           "LEASE_EXPIRED",
                            "REQUEST_ID_REUSE",    "INVALID_STATE",
                            "JOB_NOT_FOUND",       "JOB_ID_CONFLICT",
                            "JOB_LIMIT_EXCEEDED",  "UNSUPPORTED_PROFILE",
                            "UNSUPPORTED_MODE",    "FREQUENCY_REJECTED",
                            "ARM_CONFLICT",        "CLOCK_UNSYNCHRONIZED",
                            "CLOCK_UNCERTAIN",     "LEAP_UNSAFE",
                            "ARM_TOO_LATE",        "ARM_TOO_FAR",
                            "MISSED_START",        "OUTPUT_STATE_UNKNOWN",
                            "DEVICE_FAULT",        "INTERNAL_ERROR"};
constexpr std::uint64_t max_job_ns = 86'400'000'000'000ULL;
constexpr std::uint64_t max_ahead_ns = 604'800'000'000'000ULL;
constexpr std::size_t max_payload = 65536;
void require(bool condition, const char *why) {
    if (!condition)
        throw std::invalid_argument(why);
}
Value get(Value value, std::string_view key) {
    auto found = value.get(key);
    require(found.has_value(), "Missing required member");
    return *found;
}
void require_fields(Value v, std::initializer_list<std::string_view> required,
                    std::initializer_list<std::string_view> optional = {}) {
    require(json::fields(v, required, optional), "Object members do not match WTP/1");
}
std::string string(Value v, std::size_t minimum = 0, std::size_t maximum = max_payload) {
    require(v.type() == '"', "Expected string");
    auto s = v.string();
    require(s.size() >= minimum && s.size() <= maximum, "String byte limit exceeded");
    return s;
}
std::string id(Value v) {
    require(json::identifier(v), "Invalid identifier");
    return v.string();
}
std::optional<std::string> nullable_id(Value v) {
    if (v.raw == "null")
        return {};
    return id(v);
}
bool boolean(Value v) {
    require(v.raw == "true" || v.raw == "false", "Expected boolean");
    return v.boolean();
}
std::uint64_t decimal(Value v, bool nonzero = false) {
    std::uint64_t n{};
    require(json::decimal(v, n, nonzero), "Invalid unsigned decimal string");
    return n;
}
std::int32_t integer(Value v, std::int32_t minimum, std::int32_t maximum) {
    require(v.type() == '-' || (v.type() >= '0' && v.type() <= '9'), "Expected JSON integer");
    auto n = v.integer();
    require(n >= minimum && n <= maximum, "Integer outside permitted range");
    return n;
}
std::vector<Value> array(Value v, std::size_t minimum, std::size_t maximum) {
    require(v.type() == '[', "Expected array");
    auto values = v.elements(maximum);
    require(values.size() >= minimum && values.size() <= maximum, "Array limit exceeded");
    return values;
}
template <class Enum, std::size_t N>
Enum choice(Value v, const std::array<const char *, N> &names) {
    const auto s = string(v);
    const auto it = std::find(names.begin(), names.end(), s);
    require(it != names.end(), "Unknown enumeration value");
    return static_cast<Enum>(it - names.begin());
}
template <class T> void unique(const std::vector<T> &values) {
    for (std::size_t i = 0; i < values.size(); ++i)
        require(std::find(values.begin(), values.begin() + i, values[i]) == values.begin() + i,
                "Duplicate array value");
}
void constant(Value v, std::string_view expected) {
    require(string(v) == expected, "Unexpected constant");
}
bool valid_version(std::string_view v) {
    return v.size() >= 5 && v.starts_with("WTP/") && v[4] >= '1' && v[4] <= '9' &&
           std::all_of(v.begin() + 5, v.end(), [](char c) { return c >= '0' && c <= '9'; });
}
Clock clock(Value v) {
    require_fields(
        v, {"state", "utc_now_ns", "monotonic_now_ns", "uncertainty_ns", "sync_age_ns", "leap"},
        {"leap_transition_utc_ns"});
    Clock c;
    c.state = choice<ClockState>(get(v, "state"), clock_states);
    c.utc_now_ns = decimal(get(v, "utc_now_ns"));
    c.monotonic_now_ns = decimal(get(v, "monotonic_now_ns"));
    c.uncertainty_ns = decimal(get(v, "uncertainty_ns"));
    c.sync_age_ns = decimal(get(v, "sync_age_ns"));
    c.leap = choice<Leap>(get(v, "leap"), leaps);
    if (auto transition = v.get("leap_transition_utc_ns"))
        c.leap_transition_utc_ns = decimal(*transition);
    require(c.leap_transition_utc_ns.has_value() ==
                (c.leap == Leap::InsertPending || c.leap == Leap::DeletePending),
            "Inconsistent leap transition");
    return c;
}
Error error(Value v) {
    require_fields(v, {"code", "message", "retryable"}, {"detail"});
    Error e;
    e.code = choice<ErrorCode>(get(v, "code"), errors);
    e.message = string(get(v, "message"), 1, 256);
    e.retryable = boolean(get(v, "retryable"));
    if (auto detail = v.get("detail")) {
        require(detail->type() == '{', "Error detail must be an object");
        e.detail_json = std::string(detail->raw);
    }
    return e;
}
Job job(Value v) {
    require_fields(v, {"job_id", "profile", "mode", "total_duration_ns", "events"},
                   {"allow_frequency_adjustment"});
    Job j;
    j.job_id = id(get(v, "job_id"));
    constant(get(v, "profile"), "rf-events/1");
    j.mode = choice<Mode>(get(v, "mode"), modes);
    j.total_duration_ns = decimal(get(v, "total_duration_ns"), true);
    require(j.total_duration_ns <= max_job_ns, "Job exceeds WTP/1 duration bound");
    if (auto allow = v.get("allow_frequency_adjustment"))
        j.allow_frequency_adjustment = boolean(*allow);
    std::uint64_t end = 0;
    for (auto item : array(get(v, "events"), 1, 512)) {
        require_fields(item, {"offset_ns", "duration_ns", "rf_on"}, {"frequency_nhz"});
        RfEvent e;
        e.offset_ns = decimal(get(item, "offset_ns"));
        e.duration_ns = decimal(get(item, "duration_ns"), true);
        e.rf_on = boolean(get(item, "rf_on"));
        if (auto f = item.get("frequency_nhz"))
            e.frequency_nhz = decimal(*f, true);
        require(e.rf_on == e.frequency_nhz.has_value(), "Frequency presence must match RF state");
        require(e.offset_ns == end && e.duration_ns <= j.total_duration_ns - end,
                "Discontinuous or overflowing event timeline");
        end += e.duration_ns;
        j.events.push_back(e);
    }
    require(end == j.total_duration_ns, "Events do not fill job duration");
    return j;
}
Ping ping(Value v) {
    require_fields(v, {}, {"token"});
    Ping p;
    if (auto token = v.get("token"))
        p.token = string(*token, 0, 64);
    return p;
}
RequestBody request_body(Operation op, Value b) {
    switch (op) {
    case Operation::Hello: {
        require_fields(b, {"versions", "client_name", "client_version"});
        HelloRequest h;
        for (auto v : array(get(b, "versions"), 1, 16)) {
            auto s = string(v);
            require(valid_version(s), "Invalid protocol version");
            h.versions.push_back(std::move(s));
        }
        unique(h.versions);
        h.client_name = string(get(b, "client_name"), 1, 64);
        h.client_version = string(get(b, "client_version"), 1, 64);
        return h;
    }
    case Operation::Claim:
    case Operation::Renew:
        require_fields(b, {"owner_id", "lease_ms"});
        return LeaseRequest{id(get(b, "owner_id")), integer(get(b, "lease_ms"), 5000, 60000)};
    case Operation::Load:
        return job(b);
    case Operation::Arm:
        require_fields(b, {"job_id", "start_utc_ns", "max_start_uncertainty_ns"});
        return ArmRequest{id(get(b, "job_id")), decimal(get(b, "start_utc_ns")),
                          decimal(get(b, "max_start_uncertainty_ns"))};
    case Operation::Abort:
        require_fields(b, {"job_id"});
        return AbortRequest{id(get(b, "job_id"))};
    case Operation::Ping:
        return ping(b);
    default:
        require_fields(b, {});
        return Empty{};
    }
}
Capabilities caps(Value b) {
    require_fields(b, {"profiles", "modes", "engine", "frequency_ranges", "max_payload_bytes",
                       "max_events", "max_job_duration_ns", "minimum_arm_lead_ns",
                       "maximum_arm_ahead_ns", "maximum_arm_uncertainty_ns",
                       "maximum_holdover_age_ns", "output_disable_timeout_ns", "minimum_lease_ms",
                       "maximum_lease_ms", "response_cache_entries", "response_cache_ttl_seconds",
                       "terminal_record_entries", "terminal_record_ttl_seconds"});
    Capabilities c;
    for (auto p : array(get(b, "profiles"), 1, 1)) {
        constant(p, "rf-events/1");
        c.profiles.push_back(p.string());
    }
    for (auto m : array(get(b, "modes"), 1, modes.size()))
        c.modes.push_back(choice<Mode>(m, modes));
    unique(c.modes);
    c.engine = string(get(b, "engine"), 1, 64);
    for (auto range : array(get(b, "frequency_ranges"), 1, 32)) {
        require_fields(range, {"minimum_nhz", "maximum_nhz"});
        FrequencyRange r{decimal(get(range, "minimum_nhz"), true),
                         decimal(get(range, "maximum_nhz"), true)};
        require(r.minimum_nhz <= r.maximum_nhz, "Reversed frequency range");
        c.frequency_ranges.push_back(r);
    }
    c.max_payload_bytes = integer(get(b, "max_payload_bytes"), 65536, 65536);
    c.max_events = integer(get(b, "max_events"), 1, 512);
    c.max_job_duration_ns = decimal(get(b, "max_job_duration_ns"), true);
    c.minimum_arm_lead_ns = decimal(get(b, "minimum_arm_lead_ns"));
    c.maximum_arm_ahead_ns = decimal(get(b, "maximum_arm_ahead_ns"), true);
    c.maximum_arm_uncertainty_ns = decimal(get(b, "maximum_arm_uncertainty_ns"));
    c.maximum_holdover_age_ns = decimal(get(b, "maximum_holdover_age_ns"));
    c.output_disable_timeout_ns = decimal(get(b, "output_disable_timeout_ns"), true);
    require(c.max_job_duration_ns <= max_job_ns && c.maximum_arm_ahead_ns <= max_ahead_ns &&
                c.minimum_arm_lead_ns <= c.maximum_arm_ahead_ns &&
                c.output_disable_timeout_ns <= 5'000'000'000ULL,
            "Invalid capability bounds");
    constexpr auto lo = std::numeric_limits<std::int32_t>::min();
    constexpr auto hi = std::numeric_limits<std::int32_t>::max();
    c.minimum_lease_ms = integer(get(b, "minimum_lease_ms"), lo, 5000);
    c.maximum_lease_ms = integer(get(b, "maximum_lease_ms"), 60000, hi);
    c.response_cache_entries = integer(get(b, "response_cache_entries"), 8, hi);
    c.response_cache_ttl_seconds = integer(get(b, "response_cache_ttl_seconds"), 300, hi);
    c.terminal_record_entries = integer(get(b, "terminal_record_entries"), 8, hi);
    c.terminal_record_ttl_seconds = integer(get(b, "terminal_record_ttl_seconds"), 3600, hi);
    return c;
}
Status status(Value b) {
    require_fields(b,
                   {"boot_id", "state", "output_active", "owner_id", "job_id", "terminal_records"});
    Status s;
    s.boot_id = id(get(b, "boot_id"));
    s.state = choice<State>(get(b, "state"), states);
    s.output_active = boolean(get(b, "output_active"));
    s.owner_id = nullable_id(get(b, "owner_id"));
    s.job_id = nullable_id(get(b, "job_id"));
    require(s.job_id.has_value() == (s.state != State::Empty), "Status job/state mismatch");
    // Schema has no independent record-count ceiling. The frame bounds the
    // number of nonempty records; do not impose the device's minimum of eight.
    for (auto v : array(get(b, "terminal_records"), 0, max_payload)) {
        require_fields(v, {"job_id", "state", "ended_monotonic_ns", "output_active"}, {"error"});
        TerminalRecord t;
        t.job_id = id(get(v, "job_id"));
        t.state = choice<State>(get(v, "state"), states);
        require(t.state == State::Complete || t.state == State::Aborted ||
                    t.state == State::Missed || t.state == State::Failed,
                "Nonterminal retained record");
        t.ended_monotonic_ns = decimal(get(v, "ended_monotonic_ns"));
        t.output_active = boolean(get(v, "output_active"));
        if (auto e = v.get("error"))
            t.error = error(*e);
        require(!(t.state == State::Complete && t.error) &&
                    !((t.state == State::Missed || t.state == State::Failed) && !t.error),
                "Terminal error/state mismatch");
        s.terminal_records.push_back(std::move(t));
    }
    return s;
}
ResponseBody response_body(Operation op, Value b) {
    switch (op) {
    case Operation::Hello:
        require_fields(b,
                       {"selected_version", "device_id", "boot_id", "product", "firmware_version"});
        constant(get(b, "selected_version"), "WTP/1");
        return HelloResponse{id(get(b, "device_id")), id(get(b, "boot_id")),
                             string(get(b, "product"), 1, 64),
                             string(get(b, "firmware_version"), 1, 64)};
    case Operation::Caps:
        return caps(b);
    case Operation::Claim:
    case Operation::Renew:
        require_fields(b, {"owner_id", "granted_lease_ms", "expires_monotonic_ns"});
        return LeaseResponse{id(get(b, "owner_id")),
                             integer(get(b, "granted_lease_ms"), 5000, 60000),
                             decimal(get(b, "expires_monotonic_ns"))};
    case Operation::Load: {
        require_fields(b, {"job_id", "state", "adjustments"});
        constant(get(b, "state"), "loaded");
        LoadResponse l{id(get(b, "job_id")), {}};
        for (auto a : array(get(b, "adjustments"), 0, 512)) {
            require_fields(a, {"event_index", "requested_frequency_nhz", "realized_frequency_nhz"});
            l.adjustments.push_back({integer(get(a, "event_index"), 0, 511),
                                     decimal(get(a, "requested_frequency_nhz"), true),
                                     decimal(get(a, "realized_frequency_nhz"), true)});
        }
        return l;
    }
    case Operation::Arm:
        require_fields(b, {"job_id", "state", "start_utc_ns", "start_monotonic_ns", "clock"});
        constant(get(b, "state"), "armed");
        return ArmResponse{id(get(b, "job_id")), decimal(get(b, "start_utc_ns")),
                           decimal(get(b, "start_monotonic_ns")), clock(get(b, "clock"))};
    case Operation::Abort:
        require_fields(b, {"job_id", "state", "output_active"});
        constant(get(b, "state"), "aborted");
        require(!boolean(get(b, "output_active")), "Abort acknowledgment has active output");
        return AbortResponse{id(get(b, "job_id"))};
    case Operation::Status:
        return status(b);
    case Operation::GetClock:
        return clock(b);
    case Operation::Ping:
        return ping(b);
    case Operation::Release:
        require_fields(b, {});
        return Empty{};
    }
    throw std::invalid_argument("Unknown operation");
}
EventBody event_body(EventKind kind, Value b) {
    if (kind == EventKind::InvalidFrame || kind == EventKind::SessionReplaced) {
        require_fields(b, {"error"});
        return ErrorEvent{error(get(b, "error"))};
    }
    if (kind == EventKind::OwnerReleased) {
        require_fields(b, {"owner_id", "reason", "output_active"});
        return OwnerReleasedEvent{id(get(b, "owner_id")),
                                  choice<ReleaseReason>(get(b, "reason"), reasons),
                                  boolean(get(b, "output_active"))};
    }
    JobStateEvent e;
    if (kind == EventKind::DeviceFault)
        require_fields(b, {"state", "output_active", "error"}, {"job_id"});
    else if (kind == EventKind::MissedStart)
        require_fields(b, {"job_id", "state", "output_active", "error"});
    else
        require_fields(b, {"job_id", "state", "output_active"}, {"error"});
    if (auto j = b.get("job_id")) {
        if (kind == EventKind::JobState)
            e.job_id = nullable_id(*j);
        else
            e.job_id = id(*j);
    }
    e.state = choice<State>(get(b, "state"), states);
    e.output_active = boolean(get(b, "output_active"));
    if (auto failure = b.get("error"))
        e.error = error(*failure);
    if (kind == EventKind::JobState)
        require(e.job_id.has_value() == (e.state != State::Empty), "Event job/state mismatch");
    if (kind == EventKind::MissedStart)
        require(e.state == State::Missed && !e.output_active &&
                    e.error->code == ErrorCode::MissedStart,
                "Invalid missed-start event");
    if (kind == EventKind::DeviceFault)
        require(e.state == State::Failed, "Invalid device-fault event");
    return e;
}
// Keep serialization allocation bounded even for malformed caller-owned data.

struct Builder {
    std::string value;
    void add(std::string_view s) {
        require(s.size() <= max_payload - value.size(), "Encoded payload exceeds WTP/1 bound");
        value += s;
    }
    void quoted(std::string_view s) {
        require(s.size() <= max_payload, "String exceeds payload bound");
        add(json::quote(s));
    }
    void number(std::uint64_t n) { quoted(std::to_string(n)); }
};
void request_json(Builder &out, const Request &r) {
    require(!operation_name(r.op).empty(), "Unknown operation");
    out.add("{\"type\":\"request\",\"protocol\":\"WTP/1\",\"session_id\":");
    out.quoted(r.session_id);
    out.add(",\"request_id\":");
    out.quoted(r.request_id);
    out.add(",\"op\":");
    out.quoted(operation_name(r.op));
    out.add(",\"body\":{");
    std::visit(
        [&](const auto &b) {
            using T = std::decay_t<decltype(b)>;
            if constexpr (std::is_same_v<T, Empty>) {
                require(r.op == Operation::Caps || r.op == Operation::Release ||
                            r.op == Operation::Status || r.op == Operation::GetClock,
                        "Operation/body mismatch");
            } else if constexpr (std::is_same_v<T, HelloRequest>) {
                require(r.op == Operation::Hello && b.versions.size() <= 16,
                        "Operation/body or version-count mismatch");
                out.add("\"versions\":[");
                for (std::size_t i = 0; i < b.versions.size(); ++i) {
                    if (i)
                        out.add(",");
                    out.quoted(b.versions[i]);
                }
                out.add("],\"client_name\":");
                out.quoted(b.client_name);
                out.add(",\"client_version\":");
                out.quoted(b.client_version);
            } else if constexpr (std::is_same_v<T, LeaseRequest>) {
                require(r.op == Operation::Claim || r.op == Operation::Renew,
                        "Operation/body mismatch");
                out.add("\"owner_id\":");
                out.quoted(b.owner_id);
                out.add(",\"lease_ms\":");
                out.add(std::to_string(b.lease_ms));
            } else if constexpr (std::is_same_v<T, Job>) {
                require(r.op == Operation::Load && b.events.size() <= 512,
                        "Operation/body or event-count mismatch");
                const auto index = static_cast<std::size_t>(b.mode);
                require(index < modes.size(), "Unknown mode");
                out.add("\"job_id\":");
                out.quoted(b.job_id);
                out.add(",\"profile\":\"rf-events/1\",\"mode\":");
                out.quoted(modes[index]);
                out.add(",\"total_duration_ns\":");
                out.number(b.total_duration_ns);
                if (b.allow_frequency_adjustment) {
                    out.add(",\"allow_frequency_adjustment\":");
                    out.add(*b.allow_frequency_adjustment ? "true" : "false");
                }
                out.add(",\"events\":[");
                for (std::size_t i = 0; i < b.events.size(); ++i) {
                    if (i)
                        out.add(",");
                    const auto &e = b.events[i];
                    out.add("{\"offset_ns\":");
                    out.number(e.offset_ns);
                    out.add(",\"duration_ns\":");
                    out.number(e.duration_ns);
                    out.add(",\"rf_on\":");
                    out.add(e.rf_on ? "true" : "false");
                    if (e.frequency_nhz) {
                        out.add(",\"frequency_nhz\":");
                        out.number(*e.frequency_nhz);
                    }
                    out.add("}");
                }
                out.add("]");
            } else if constexpr (std::is_same_v<T, ArmRequest>) {
                require(r.op == Operation::Arm, "Operation/body mismatch");
                out.add("\"job_id\":");
                out.quoted(b.job_id);
                out.add(",\"start_utc_ns\":");
                out.number(b.start_utc_ns);
                out.add(",\"max_start_uncertainty_ns\":");
                out.number(b.max_start_uncertainty_ns);
            } else if constexpr (std::is_same_v<T, AbortRequest>) {
                require(r.op == Operation::Abort, "Operation/body mismatch");
                out.add("\"job_id\":");
                out.quoted(b.job_id);
            } else if constexpr (std::is_same_v<T, Ping>) {
                require(r.op == Operation::Ping, "Operation/body mismatch");
                if (b.token) {
                    out.add("\"token\":");
                    out.quoted(*b.token);
                }
            }
        },
        r.body);
    out.add("}}");
}

} // namespace

std::string_view operation_name(Operation op) {
    const auto index = static_cast<std::size_t>(op);
    return index < operations.size() ? operations[index] : std::string_view{};
}
DecodeResult decode(std::string_view payload) {
    try {
        auto root = json::parse(payload);
        require(root.has_value(), "Invalid bounded UTF-8 JSON");
        const auto r = *root;
        const auto type = string(get(r, "type"));
        constant(get(r, "protocol"), "WTP/1");
        auto session = id(get(r, "session_id"));

        if (type == "response" && !boolean(get(r, "ok"))) {
            require_fields(r,
                           {"type", "protocol", "session_id", "request_id", "op", "ok", "error"});
            auto op = string(get(r, "op"), 1, 64);
            require(op[0] >= 'A' && op[0] <= 'Z' &&
                        std::all_of(op.begin() + 1, op.end(),
                                    [](char c) {
                                        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                                               c == '_';
                                    }),
                    "Invalid echoed operation");
            return {Message{Response{std::move(session), id(get(r, "request_id")), std::move(op),
                                     error(get(r, "error"))}},
                    {}};
        }
        auto b = get(r, "body");
        if (type == "request") {
            require_fields(r, {"type", "protocol", "session_id", "request_id", "op", "body"});
            auto op = choice<Operation>(get(r, "op"), operations);
            return {Message{Request{std::move(session), id(get(r, "request_id")), op,
                                    request_body(op, b)}},
                    {}};
        }
        if (type == "response") {
            // Failure envelopes have error instead of body (handled above).
            require_fields(r, {"type", "protocol", "session_id", "request_id", "op", "ok", "body"});
            require(boolean(get(r, "ok")), "Failure response must contain error instead of body");
            auto op = choice<Operation>(get(r, "op"), operations);
            return {Message{Response{std::move(session), id(get(r, "request_id")),
                                     string(get(r, "op")), response_body(op, b)}},
                    {}};
        }
        require(type == "event", "Unknown envelope type");
        require_fields(r,
                       {"type", "protocol", "session_id", "boot_id", "event_id", "event", "body"});
        auto kind = choice<EventKind>(get(r, "event"), events);
        return {Message{Event{std::move(session), id(get(r, "boot_id")),
                              decimal(get(r, "event_id")), kind, event_body(kind, b)}},
                {}};
    } catch (const std::invalid_argument &e) {
        return {{}, e.what()};
    }
}

EncodeResult encode_request(const Request &request) {
    try {
        Builder out;
        request_json(out, request);
        auto checked = decode(out.value);
        require(static_cast<bool>(checked), "Request violates WTP/1 schema or arithmetic");
        return {std::move(out.value), {}};
    } catch (const std::invalid_argument &e) {
        return {{}, e.what()};
    }
}

} // namespace wsprrypi::wtp
