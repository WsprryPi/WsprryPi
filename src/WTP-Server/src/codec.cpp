#include "wtp_server/codec.hpp"

#include "wtp_server/frame_parser.hpp"
#include "wtp_server/memory_budget.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace wsprrypico::wtp {
namespace {
using json::quote;
using json::Value;
Value get(Value v, std::string_view key) {
    return v.get(key).value_or(Value{});
}
bool text(Value v, std::size_t min, std::size_t max) {
    return v.type() == '"' && v.raw.size() <= max * 6 + 2 && v.string().size() >= min &&
           v.string().size() <= max;
}
bool version(std::string_view v) {
    return v.starts_with("WTP/") && v.size() >= 5 && v[4] >= '1' && v[4] <= '9' &&
           std::all_of(v.begin() + 5, v.end(), [](char c) { return c >= '0' && c <= '9'; });
}
bool operation(std::string_view v) {
    return !v.empty() && v.size() <= 64 && v[0] >= 'A' && v[0] <= 'Z' &&
           std::all_of(v.begin() + 1, v.end(), [](char c) {
               return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
           });
}
std::size_t decimal_width(std::uint64_t value) {
    std::size_t width = 1;
    while (value >= 10) {
        value /= 10;
        ++width;
    }
    return width;
}
std::string ns(std::uint64_t n) {
    return quote(std::to_string(n));
}
std::string nullable(const std::optional<std::string>& s) {
    return s ? quote(*s) : "null";
}
std::string boolean(bool b) {
    return b ? "true" : "false";
}
std::string response_prefix(const Request& r, const Response& s) {
    return "{\"type\":\"response\",\"protocol\":\"WTP/1\",\"session_id\":" + quote(r.session_id) +
           ",\"request_id\":" + quote(r.request_id) + ",\"op\":" + quote(r.operation) +
           ",\"ok\":" + boolean(s.ok);
}
std::string load_fields(const Response& s) {
    return "{\"job_id\":" + quote(s.job_id) + ",\"state\":\"loaded\",\"adjustments\":[";
}
std::string load_body_prefix(const Response& s) {
    return ",\"body\":" + load_fields(s);
}
std::size_t load_response_bytes(std::size_t prefix, const Response& s) {
    constexpr auto punctuation = std::string_view(
        "{\"event_index\":,\"requested_frequency_nhz\":\"\",\"realized_frequency_nhz\":\"\"}");
    auto bytes = prefix + 3;
    bool first = true;
    for (const auto& a : s.adjustments) {
        bytes += (first ? 0 : 1) + punctuation.size() + decimal_width(a.event_index) +
                 decimal_width(a.requested_frequency_nhz) + decimal_width(a.realized_frequency_nhz);
        first = false;
    }
    return bytes;
}
template <typename Append> void append_load_adjustments(const Response& s, Append append) {
    bool first = true;
    for (const auto& a : s.adjustments) {
        if (!first)
            append(",");
        first = false;
        append("{\"event_index\":" + std::to_string(a.event_index) +
               ",\"requested_frequency_nhz\":" + ns(a.requested_frequency_nhz) +
               ",\"realized_frequency_nhz\":" + ns(a.realized_frequency_nhz) + '}');
    }
    append("]}}");
}
std::string clock_json(const ClockSnapshot& c) {
    constexpr std::array states{"unsynchronized", "synchronized", "holdover"};
    constexpr std::array leaps{"normal", "insert_pending", "delete_pending", "unknown"};
    std::string out =
        "{\"state\":" + quote(states[static_cast<unsigned>(c.state)]) +
        ",\"utc_now_ns\":" + ns(c.utc_now_ns) + ",\"monotonic_now_ns\":" + ns(c.monotonic_now_ns) +
        ",\"uncertainty_ns\":" + ns(c.uncertainty_ns) + ",\"sync_age_ns\":" + ns(c.sync_age_ns) +
        ",\"leap\":" + quote(leaps[static_cast<unsigned>(c.leap)]);
    if (c.leap_transition_utc_ns)
        out += ",\"leap_transition_utc_ns\":" + ns(*c.leap_transition_utc_ns);
    return out + '}';
}
bool body(Request& r, Value b, std::string_view active_replay_id) {
    const auto& op = r.operation;
    if (op == "HELLO") {
        HelloBody h;
        auto versions = get(b, "versions");
        bool valid = versions.type() == '[';
        for (auto v : versions.elements(16)) {
            if (v.type() != '"' || !version(v.string()))
                valid = false;
            h.versions.push_back(v.string());
        }
        valid = valid && !h.versions.empty() && h.versions.size() <= 16;
        for (std::size_t i = 0; i < h.versions.size(); ++i)
            if (std::find(h.versions.begin(), h.versions.begin() + i, h.versions[i]) !=
                h.versions.begin() + i)
                valid = false;
        r.body = std::move(h);
        return valid && json::fields(b, {"versions", "client_name", "client_version"}) &&
               text(get(b, "client_name"), 1, 64) && text(get(b, "client_version"), 1, 64);
    }
    if (op == "CLAIM" || op == "RENEW") {
        auto owner = get(b, "owner_id"), lease = get(b, "lease_ms");
        if (!json::fields(b, {"owner_id", "lease_ms"}) || !json::identifier(owner) ||
            lease.type() < '0' || lease.type() > '9' || lease.integer() < 5000 ||
            lease.integer() > 60000)
            return false;
        if (op == "CLAIM")
            r.body = ClaimBody{owner.string(), static_cast<std::uint32_t>(lease.integer())};
        else
            r.body = RenewBody{owner.string(), static_cast<std::uint32_t>(lease.integer())};
        return true;
    }
    if (op == "ARM") {
        ArmBody a;
        if (!json::fields(b, {"job_id", "start_utc_ns", "max_start_uncertainty_ns"}) ||
            !json::identifier(get(b, "job_id")) ||
            !json::decimal(get(b, "start_utc_ns"), a.start_utc_ns) ||
            !json::decimal(get(b, "max_start_uncertainty_ns"), a.max_start_uncertainty_ns))
            return false;
        a.job_id = get(b, "job_id").string();
        r.body = std::move(a);
        return true;
    }
    if (op == "ABORT") {
        if (!json::fields(b, {"job_id"}) || !json::identifier(get(b, "job_id")))
            return false;
        r.body = AbortBody{get(b, "job_id").string()};
        return true;
    }
    if (op == "PING") {
        if (!json::fields(b, {}, {"token"}))
            return false;
        PingBody p;
        if (auto token = b.get("token")) {
            if (!text(*token, 0, 64))
                return false;
            p.token = token->string();
        }
        r.body = std::move(p);
        return true;
    }
    if (op == "LOAD") {
        Job j;
        if (!json::fields(b, {"job_id", "profile", "mode", "total_duration_ns", "events"},
                          {"allow_frequency_adjustment"}) ||
            !json::identifier(get(b, "job_id")) || !text(get(b, "profile"), 11, 11) ||
            get(b, "profile").string() != "rf-events/1" || !text(get(b, "mode"), 2, 5) ||
            !json::decimal(get(b, "total_duration_ns"), j.total_duration_ns, true) ||
            get(b, "events").type() != '[')
            return false;
        j.job_id = get(b, "job_id").string();
        j.mode = get(b, "mode").string();
        constexpr std::array modes{"wspr", "qrss", "fskcw", "dfcw", "cw", "tone"};
        if (std::find(modes.begin(), modes.end(), j.mode) == modes.end())
            return false;
        if (auto allow = b.get("allow_frequency_adjustment")) {
            if (allow->raw != "true" && allow->raw != "false")
                return false;
            j.allow_frequency_adjustment = allow->boolean();
        }
        const auto events = get(b, "events");
        std::size_t cursor = 0, count = 0;
        while (events.next_element(cursor)) {
            if (++count > 512)
                return false;
        }
        if (!count)
            return false;
        // Replay still validates every field and hashes every typed event, but
        // does not allocate a second copy of the active event array.
        const bool replay = !active_replay_id.empty() && j.job_id == active_replay_id;
        JobDigestBuilder digest(j, count);
        if (!replay && !j.events.reserve(count)) {
            r.body = std::move(j);
            return true;
        }
        cursor = 0;
        while (auto element = events.next_element(cursor)) {
            const auto e = *element;
            RfEvent event;
            if (!json::fields(e, {"offset_ns", "duration_ns", "rf_on"}, {"frequency_nhz"}) ||
                !json::decimal(get(e, "offset_ns"), event.offset_ns) ||
                !json::decimal(get(e, "duration_ns"), event.duration_ns, true) ||
                (get(e, "rf_on").raw != "true" && get(e, "rf_on").raw != "false"))
                return false;
            event.rf_on = get(e, "rf_on").boolean();
            auto f = e.get("frequency_nhz");
            if (event.rf_on != f.has_value())
                return false;
            if (f) {
                std::uint64_t frequency;
                if (!json::decimal(*f, frequency, true))
                    return false;
                event.frequency_nhz = frequency;
            }
            if (replay)
                digest.append(event);
            else if (!j.events.push_back(event)) {
                r.body = std::move(j);
                return true;
            }
        }
        if (replay)
            r.body = LoadReplayBody{j.job_id, digest.finish()};
        else
            r.body = std::move(j);
        return true;
    }
    return (op == "CAPS" || op == "STATUS" || op == "GET_CLOCK" || op == "RELEASE") &&
           json::fields(b, {});
}
std::string caps(const ServiceConfig& c) {
    std::string modes;
    for (const auto& mode : c.supported_modes) {
        if (!modes.empty())
            modes += ',';
        modes += "\"" + mode + "\"";
    }
    return "{\"profiles\":[\"rf-events/"
           "1\"],\"modes\":[" +
           modes + "],\"engine\":\"" + c.capability_engine +
           "\",\"frequency_ranges\":[{\"minimum_nhz\":" + ns(c.minimum_frequency_nhz) +
           ",\"maximum_nhz\":" + ns(c.maximum_frequency_nhz) +
           "}],"
           "\"max_payload_bytes\":65536,\"max_events\":" +
           std::to_string(c.max_events) + ",\"max_job_duration_ns\":" + ns(c.max_job_duration_ns) +
           ",\"minimum_arm_lead_ns\":" + ns(c.minimum_arm_lead_ns) +
           ",\"maximum_arm_ahead_ns\":" + ns(c.maximum_arm_ahead_ns) +
           ",\"maximum_arm_uncertainty_ns\":" + ns(c.maximum_arm_uncertainty_ns) +
           ",\"maximum_holdover_age_ns\":" + ns(c.maximum_holdover_age_ns) +
           ",\"output_disable_timeout_ns\":" + ns(c.output_disable_timeout_ns) +
           ",\"minimum_lease_ms\":5000,\"maximum_lease_ms\":60000,\"response_cache_entries\":" +
           std::to_string(c.response_cache_entries) + ",\"response_cache_ttl_seconds\":" +
           std::to_string(c.response_cache_ttl_ns / 1'000'000'000ULL) +
           ",\"terminal_record_entries\":" + std::to_string(c.terminal_record_entries) +
           ",\"terminal_record_ttl_seconds\":" +
           std::to_string(c.terminal_record_ttl_ns / 1'000'000'000ULL) + '}';
}
} // namespace
bool is_small_read_request(Value root) {
    const auto op = get(root, "op");
    // Classify only bounded read envelopes. Full protocol/schema validation
    // still follows; whitespace padding never requires a second event array.
    if (get(root, "protocol").raw.size() > 128 || get(root, "type").raw.size() > 44 ||
        op.raw.size() > 56 || !json::fields(get(root, "body"), {}))
        return false;
    const auto name = op.string();
    return name == "STATUS" || name == "CAPS" || name == "GET_CLOCK";
}
bool is_active_load_replay(Value root, std::string_view id) {
    if (id.empty())
        return false;
    const auto op = get(root, "op"), job = get(get(root, "body"), "job_id");
    // Bound strings before decoding them; escaped identifiers remain supported.
    return get(root, "protocol").raw.size() <= 128 && get(root, "type").raw.size() <= 44 &&
           op.raw.size() <= 26 && job.raw.size() <= 194 && op.string() == "LOAD" &&
           job.string() == id;
}
std::optional<Request> decode_request(Value root, std::string_view principal, InputView payload,
                                      std::string_view active_replay_id) {
    if (!json::fields(root, {"type", "protocol", "session_id", "request_id", "op", "body"}) ||
        get(root, "type").string() != "request" || !json::identifier(get(root, "session_id")) ||
        !json::identifier(get(root, "request_id")) || get(root, "protocol").type() != '"' ||
        !version(get(root, "protocol").string()) || get(root, "op").type() != '"' ||
        !operation(get(root, "op").string()) || get(root, "body").type() != '{')
        return std::nullopt;
    Request r;
    r.protocol = get(root, "protocol").string();
    r.session_id = get(root, "session_id").string();
    r.request_id = get(root, "request_id").string();
    r.operation = get(root, "op").string();
    r.principal = principal;
    Sha256 digest;
    for (std::size_t offset = 0; offset < payload.size();) {
        const auto part = payload.at(offset);
        digest.update(part);
        offset += part.size();
    }
    r.payload_digest = digest.finish();
    r.body_valid = body(r, get(root, "body"), active_replay_id);
    return r;
}
std::string state_name(State s) {
    constexpr std::array names{"empty",    "loaded",  "armed",  "running",
                               "complete", "aborted", "missed", "failed"};
    return names[static_cast<unsigned>(s)];
}
std::string error_json(ErrorCode code) {
    constexpr std::array names{"INTERNAL_ERROR",
                               "INVALID_FRAME",
                               "INVALID_MESSAGE",
                               "UNSUPPORTED_VERSION",
                               "HELLO_REQUIRED",
                               "UNKNOWN_OPERATION",
                               "AUTHENTICATION_REQUIRED",
                               "SESSION_REPLACED",
                               "BUSY",
                               "NOT_OWNER",
                               "LEASE_EXPIRED",
                               "REQUEST_ID_REUSE",
                               "INVALID_STATE",
                               "JOB_NOT_FOUND",
                               "JOB_ID_CONFLICT",
                               "JOB_LIMIT_EXCEEDED",
                               "UNSUPPORTED_PROFILE",
                               "UNSUPPORTED_MODE",
                               "FREQUENCY_REJECTED",
                               "ARM_CONFLICT",
                               "CLOCK_UNSYNCHRONIZED",
                               "CLOCK_UNCERTAIN",
                               "LEAP_UNSAFE",
                               "ARM_TOO_LATE",
                               "ARM_TOO_FAR",
                               "MISSED_START",
                               "OUTPUT_STATE_UNKNOWN",
                               "DEVICE_FAULT",
                               "INTERNAL_ERROR"};
    auto name = names[static_cast<unsigned>(code)];
    const bool retry = code == ErrorCode::Busy || code == ErrorCode::ClockUnsynchronized ||
                       code == ErrorCode::ClockUncertain;
    return "{\"code\":" + quote(name) + ",\"message\":" + quote(name) +
           ",\"retryable\":" + boolean(retry) + '}';
}
std::string status_json(const ServiceStatus& s) {
    std::string out =
        "{\"boot_id\":" + quote(s.boot_id) + ",\"state\":" + quote(state_name(s.state)) +
        ",\"output_active\":" + boolean(s.output_active) + ",\"owner_id\":" + nullable(s.owner_id) +
        ",\"job_id\":" + nullable(s.job_id) + ",\"terminal_records\":[";
    bool first = true;
    for (const auto& t : s.terminal_records) {
        if (!first)
            out += ',';
        first = false;
        out += "{\"job_id\":" + quote(t.job_id) + ",\"state\":" + quote(state_name(t.state)) +
               ",\"ended_monotonic_ns\":" + ns(t.ended_monotonic_ns) +
               ",\"output_active\":" + boolean(t.output_active);
        if (t.error != ErrorCode::None)
            out += ",\"error\":" + error_json(t.error);
        out += '}';
    }
    return out + "]}";
}
std::string encode_response(const Request& r, const Response& s, const ServiceConfig& config,
                            std::string_view device, std::string_view firmware,
                            std::string_view product) {
    std::string out = response_prefix(r, s);
    if (!s.ok)
        return out + ",\"error\":" + error_json(s.error) + '}';
    std::string b = "{}";
    if (r.operation == "HELLO")
        b = "{\"selected_version\":\"WTP/1\",\"device_id\":" + quote(device) +
            ",\"boot_id\":" + quote(s.boot_id) +
            ",\"product\":" + quote(product) + ",\"firmware_version\":" + quote(firmware) + '}';
    else if (r.operation == "CAPS")
        b = caps(config);
    else if (r.operation == "STATUS" && s.status_snapshot)
        b = status_json(*s.status_snapshot);
    else if (r.operation == "GET_CLOCK" && s.clock_snapshot)
        b = clock_json(*s.clock_snapshot);
    else if (r.operation == "PING" && s.ping_token)
        b = "{\"token\":" + quote(*s.ping_token) + '}';
    else if (r.operation == "CLAIM" || r.operation == "RENEW")
        b = "{\"owner_id\":" + quote(s.owner_id) +
            ",\"granted_lease_ms\":" + std::to_string(s.granted_lease_ms) +
            ",\"expires_monotonic_ns\":" + ns(s.expires_monotonic_ns) + '}';
    else if (r.operation == "LOAD") {
        out += load_body_prefix(s);
        // LOAD can return hundreds of adjustments. Size the final response
        // once, without a second full body or geometric string growth.
        out.reserve(load_response_bytes(out.size(), s));
        append_load_adjustments(s, [&](std::string_view part) { out += part; });
        return out;
    } else if (r.operation == "ARM" && s.clock_snapshot)
        b = "{\"job_id\":" + quote(s.job_id) +
            ",\"state\":\"armed\",\"start_utc_ns\":" + ns(s.start_utc_ns) +
            ",\"start_monotonic_ns\":" + ns(s.start_monotonic_ns) +
            ",\"clock\":" + clock_json(*s.clock_snapshot) + '}';
    else if (r.operation == "ABORT")
        b = "{\"job_id\":" + quote(s.job_id) + ",\"state\":\"aborted\",\"output_active\":false}";
    return out + ",\"body\":" + b + '}';
}
OutputBuffer encode_load_response_buffer(const Request& r, const Response& s, bool browser) {
    OutputBuffer result;
    if (!s.ok || r.operation != "LOAD" || s.adjustments.size() > 512)
        return result;
    const auto fields = load_fields(s);
    const auto prefix =
        browser ? "{\"ok\":true,\"request_id\":" + quote(r.request_id) + ",\"result\":" + fields
                : response_prefix(r, s) + ",\"body\":" + fields;
    const auto bytes = load_response_bytes(prefix.size(), s);
    if (bytes > 65536 || !memory_admitted(bytes + 1024) || !result.reserve(bytes))
        return result;
    bool complete = true;
    auto append = [&](std::string_view part) {
        complete = complete &&
                   result.append({reinterpret_cast<const std::uint8_t*>(part.data()), part.size()});
    };
    append(prefix);
    append_load_adjustments(s, append);
    if (!complete || result.size() != bytes)
        return {};
    return result;
}
LoadResponseStream::LoadResponseStream(const Request& r, const Response& s) {
    if (!s.ok || r.operation != "LOAD" || s.adjustments.size() > 512 || !memory_admitted(6144))
        return;
    piece_ = response_prefix(r, s) + ",\"body\":" + load_fields(s);
    const auto bytes = load_response_bytes(piece_.size(), s);
    if (bytes > 65536 || !page_.reserve(4096)) {
        piece_.clear();
        return;
    }
    piece_.reserve(512);
    adjustments_ = s.adjustments;
    auto checksum = [&](std::string_view part) {
        checksum_ =
            crc32c({reinterpret_cast<const std::uint8_t*>(part.data()), part.size()}, checksum_);
    };
    checksum(piece_);
    append_load_adjustments(s, checksum);
    size_ = bytes;
}
std::span<const std::uint8_t> LoadResponseStream::at(std::size_t offset) const {
    if (offset >= size_ || offset < page_start_)
        return {};
    if (offset >= page_start_ + page_.size()) {
        if (offset != page_start_ + page_.size())
            return {}; // The transport may retry or advance, never skip bytes.
        page_start_ = offset;
        page_.clear();
        while (page_.size() < 4096) {
            if (piece_offset_ == piece_.size()) {
                piece_.clear();
                piece_offset_ = 0;
                if (next_adjustment_ < adjustments_.size()) {
                    const auto& a = adjustments_[next_adjustment_];
                    if (next_adjustment_++)
                        piece_ += ',';
                    piece_ += "{\"event_index\":" + std::to_string(a.event_index) +
                              ",\"requested_frequency_nhz\":" + ns(a.requested_frequency_nhz) +
                              ",\"realized_frequency_nhz\":" + ns(a.realized_frequency_nhz) + '}';
                } else if (!closing_) {
                    piece_ = "]}}";
                    closing_ = true;
                } else
                    break;
            }
            const auto count = std::min(4096 - page_.size(), piece_.size() - piece_offset_);
            page_.append(
                {reinterpret_cast<const std::uint8_t*>(piece_.data() + piece_offset_), count});
            piece_offset_ += count;
        }
    }
    return std::span<const std::uint8_t>(page_).subspan(offset - page_start_);
}
} // namespace wsprrypico::wtp
