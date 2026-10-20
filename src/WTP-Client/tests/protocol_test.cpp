// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "wtp/wire.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace wsprrypi::wtp;
static unsigned checks;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " + #x); \
    } while (false)
std::span<const std::uint8_t> bytes(std::string_view s) {
    return {reinterpret_cast<const std::uint8_t *>(s.data()), s.size()};
}
const std::string session(32, '1'), request_id(32, 'a'), job_id(32, '3');
const std::string status_request =
    "{\"type\":\"request\",\"protocol\":\"WTP/1\",\"session_id\":\"" + session +
    "\",\"request_id\":\"" + request_id + "\",\"op\":\"STATUS\",\"body\":{}}";
std::vector<FrameEvent> feed_all(FrameParser &parser, std::span<const std::uint8_t> input,
                                 std::uint64_t now = 0) {
    std::vector<FrameEvent> result;
    while (!input.empty() && !parser.closed()) {
        auto r = parser.feed(input, now);
        CHECK(r.consumed > 0 && r.consumed <= kInputChunkBytes);
        CHECK(parser.buffered_bytes() <=
              kMaximumPayloadBytes + kFrameHeaderBytes + kInputChunkBytes);
        CHECK(r.events.size() <= kInputChunkBytes / 17 + 4);
        input = input.subspan(r.consumed);
        for (auto &e : r.events)
            result.push_back(std::move(e));
    }
    return result;
}
void framing() {
    CHECK(crc32c(bytes("123456789")) == 0xe3069283U);
    CHECK(crc32c({}) == 0U);
    CHECK(crc32c(bytes(status_request)) == 0x59dba00cU);
    const auto frame = encode_frame(bytes(status_request));
    const std::vector<std::uint8_t> header{0x57, 0x54, 0x50, 0x46, 1,    1,    0,    0,
                                           0,    0,    0,    0x9d, 0x59, 0xdb, 0xa0, 0x0c};
    CHECK(std::equal(header.begin(), header.end(), frame.begin()));
    CHECK(encode_frame({}).empty());
    CHECK(encode_frame(std::vector<std::uint8_t>(65537)).empty());
    for (std::size_t split = 0; split <= frame.size(); ++split) {
        FrameParser p;
        auto a = feed_all(p, std::span(frame).first(split));
        auto b = feed_all(p, std::span(frame).subspan(split), 1);
        a.insert(a.end(), b.begin(), b.end());
        CHECK(a.size() == 1 && a[0].kind == FrameEventKind::Payload);
        CHECK(a[0].payload ==
              std::vector<std::uint8_t>(status_request.begin(), status_request.end()));
    }
    FrameParser single;
    unsigned messages = 0;
    for (auto b : frame) {
        auto r = single.feed({&b, 1}, 0);
        for (const auto &e : r.events)
            messages += e.kind == FrameEventKind::Payload;
    }
    CHECK(messages == 1);
    std::vector<std::uint8_t> burst;
    const auto small = encode_frame(bytes("x"));
    for (int i = 0; i < 10000; ++i)
        burst.insert(burst.end(), small.begin(), small.end());
    FrameParser many;
    auto first = many.feed(burst, 0);
    CHECK(first.consumed == kInputChunkBytes);
    auto rest = feed_all(many, std::span(burst).subspan(first.consumed));
    CHECK(first.events.size() + rest.size() == 10000);
    std::vector<std::uint8_t> maximum(65536, 'x');
    FrameParser large;
    auto all = feed_all(large, encode_frame(maximum));
    CHECK(all.size() == 1 && all.front().payload == maximum);
    for (auto offset : {4U, 5U, 6U, 7U, 8U, 12U}) {
        auto bad = frame;
        bad[offset] ^= 0xff;
        FrameParser p;
        feed_all(p, bad);
        auto good = feed_all(p, frame);
        CHECK(!p.closed());
        CHECK(std::any_of(good.begin(), good.end(),
                          [](auto &e) { return e.kind == FrameEventKind::Payload; }));
    }
    auto bad = small;
    bad.back() ^= 1;
    FrameParser invalid;
    for (int i = 0; i < 2; ++i) {
        feed_all(invalid, bad);
        CHECK(!invalid.closed());
    }
    auto third = feed_all(invalid, bad);
    CHECK(invalid.closed() && third.back().kind == FrameEventKind::Closed);
    FrameParser reset;
    for (int i = 0; i < 4; ++i) {
        feed_all(reset, bad);
        feed_all(reset, bad);
        feed_all(reset, small);
    }
    CHECK(!reset.closed());
    FrameParser noise;
    feed_all(noise, std::vector<std::uint8_t>(131071 + 3, 'z'));
    CHECK(!noise.closed());
    auto n = noise.feed(bytes("z"), 0);
    CHECK(noise.closed() && n.events.back().kind == FrameEventKind::Closed);
    FrameParser noise_reset;
    feed_all(noise_reset, std::vector<std::uint8_t>(100000, 'z'));
    feed_all(noise_reset, small);
    feed_all(noise_reset, std::vector<std::uint8_t>(100000, 'z'));
    CHECK(!noise_reset.closed());
    FrameParser timeout;
    feed_all(timeout, std::span(frame).first(10), 100);
    CHECK(timeout.check_timeout(5099).empty());
    CHECK(timeout.feed(std::span(frame).subspan(10), 5100).consumed == 0);
    CHECK(timeout.closed());
    FrameParser progress;
    feed_all(progress, std::span(frame).first(5), 0);
    feed_all(progress, std::span(frame).subspan(5, 5), 4999);
    CHECK(progress.check_timeout(9998).empty());
    CHECK(!progress.check_timeout(9999).empty());
    FrameParser reversed;
    reversed.feed({}, 50);
    CHECK(!reversed.feed(frame, 49).events.empty() && reversed.closed());
    FrameParser eof;
    feed_all(eof, std::span(frame).first(20));
    eof.end_of_stream();
    CHECK(eof.closed() && eof.buffered_bytes() == 0);
    CHECK(eof.feed(frame, 1).consumed == 0);
    FrameParser idle;
    CHECK(idle.check_timeout(1000000).empty() && !idle.closed());
}
void writes() {
    FrameWriter w;
    CHECK(w.start(status_request, 100, 1000, 100));
    CHECK(!w.start("replace", 100, 1000, 100));
    auto expected = encode_frame(bytes(status_request));
    std::vector<std::uint8_t> sent;
    for (std::uint64_t now = 100; w.state() == WriteState::Pending; ++now) {
        auto pending = w.remaining(now);
        CHECK(!pending.empty());
        const auto count = std::min<std::size_t>(3, pending.size());
        sent.insert(sent.end(), pending.begin(), pending.begin() + count);
        CHECK(w.consume(count, now));
    }
    CHECK(sent == expected && w.state() == WriteState::Complete);
    CHECK(w.remaining(10000).empty());
    CHECK(w.start(status_request, 0, 1000, 5));
    CHECK(w.consume(0, 4));
    CHECK(w.remaining(5).empty() && w.state() == WriteState::Failed);
    CHECK(w.start(status_request, 0, 10, 5));
    CHECK(w.consume(1, 4) && w.consume(1, 8));
    CHECK(!w.consume(1, 10)); // Absolute timeout despite byte progress.
    CHECK(w.start(status_request, 0, 100, 100));
    CHECK(!w.consume(100000, 0) && w.state() == WriteState::Failed);
    CHECK(w.start(status_request, 100, 100, 100));
    CHECK(!w.consume(1, 99));
    CHECK(w.start(status_request, 0, 100, 100));
    w.cancel();
    CHECK(w.state() == WriteState::Cancelled && w.remaining(0).empty());
    CHECK(!w.start("", 0, 100, 100));
    CHECK(!w.start("x", 0, 0, 100));
    ProgressBudget near_limit(std::numeric_limits<std::uint64_t>::max() - 20, 20, 20);
    CHECK(!near_limit.expired(std::numeric_limits<std::uint64_t>::max() - 1));
    CHECK(near_limit.expired(std::numeric_limits<std::uint64_t>::max()));
    ProgressBudget request_budget(0, 20, 10);
    CHECK(request_budget.progress(9) && request_budget.progress(18));
    CHECK(request_budget.expired(20));
}
void typed() {
    Request r{session, request_id, Operation::Status, Empty{}};
    auto packet = RequestPacket::create(r);
    CHECK(packet && packet->payload() == status_request);
    r.session_id = "changed";
    CHECK(packet->payload() == status_request);
    CHECK(!RequestPacket::create(r));
    r = Request{session, request_id, Operation::Arm,
                ArmRequest{job_id, 1234567890123456789ULL, 500000000}};
    auto encoded = encode_request(r);
    CHECK(encoded);
    auto decoded = decode(*encoded.payload);
    CHECK(decoded);
    encoded.payload.reset();
    auto &arm = std::get<ArmRequest>(std::get<Request>(*decoded.message).body);
    CHECK(arm.job_id == job_id && arm.start_utc_ns == 1234567890123456789ULL &&
          arm.max_start_uncertainty_ns == 500000000);
    r.body = Empty{};
    CHECK(!encode_request(r));
    r.op = static_cast<Operation>(99);
    CHECK(!encode_request(r));
    Job j{job_id, Mode::Tone, 1, {{0, 1, true, 1000000000}}, {}};
    r = Request{session, request_id, Operation::Load, j};
    CHECK(encode_request(r));
    j.events[0].offset_ns = std::numeric_limits<std::uint64_t>::max();
    r.body = j;
    CHECK(!encode_request(r));
    j.events[0].offset_ns = 0;
    j.events[0].duration_ns = std::numeric_limits<std::uint64_t>::max();
    r.body = j;
    CHECK(!encode_request(r));
    j.events[0].duration_ns = 1;
    j.mode = static_cast<Mode>(99);
    r.body = j;
    CHECK(!encode_request(r));
    r = Request{session, request_id, Operation::Hello,
                HelloRequest{{"WTP/1"}, std::string(65537, 'a'), "1"}};
    CHECK(!encode_request(r));
    const auto response =
        "{\"type\":\"response\",\"protocol\":\"WTP/1\",\"session_id\":\"" + session +
        "\",\"request_id\":\"" + request_id +
        "\",\"op\":\"START\",\"ok\":false,\"error\":{\"code\":\"UNKNOWN_OPERATION\",\"message\":"
        "\"unknown\",\"retryable\":false,\"detail\":{\"a\":[-2147483648,2147483647]}}}";
    auto result = decode(response);
    CHECK(result);
    auto &failure = std::get<Response>(*result.message);
    CHECK(!failure.ok() && failure.op == "START");
    auto &err = std::get<Error>(failure.body);
    CHECK(err.code == ErrorCode::UnknownOperation && !err.retryable && err.detail_json.has_value());
    CHECK(!decode(std::string(65537, ' ')));
}
Response success(std::string_view op, std::string_view body) {
    auto input = "{\"type\":\"response\",\"protocol\":\"WTP/1\",\"session_id\":\"" + session +
                 "\",\"request_id\":\"" + request_id + "\",\"op\":\"" + std::string(op) +
                 "\",\"ok\":true,\"body\":" + std::string(body) + "}";
    auto decoded = decode(input);
    CHECK(decoded);
    input.assign(input.size(), 'x'); // Every returned field must own its data.
    auto result = std::get<Response>(std::move(*decoded.message));
    CHECK(result.ok() && result.op == op && result.session_id == session &&
          result.request_id == request_id);
    return result;
}
void response_values() {
    const auto hello = std::get<HelloResponse>(
        success(
            "HELLO",
            R"({"selected_version":"WTP/1","device_id":"44444444444444444444444444444444","boot_id":"55555555555555555555555555555555","product":"Pico","firmware_version":"test"})")
            .body);
    CHECK(hello.device_id == std::string(32, '4') && hello.boot_id == std::string(32, '5') &&
          hello.product == "Pico" && hello.firmware_version == "test");
    const auto caps = std::get<Capabilities>(
        success(
            "CAPS",
            R"({"profiles":["rf-events/1"],"modes":["wspr","qrss","fskcw","dfcw","cw","tone"],"engine":"mock","frequency_ranges":[{"minimum_nhz":"2","maximum_nhz":"3"}],"max_payload_bytes":65536,"max_events":162,"max_job_duration_ns":"110592000000","minimum_arm_lead_ns":"7","maximum_arm_ahead_ns":"100","maximum_arm_uncertainty_ns":"11","maximum_holdover_age_ns":"13","output_disable_timeout_ns":"17","minimum_lease_ms":5000,"maximum_lease_ms":60000,"response_cache_entries":9,"response_cache_ttl_seconds":301,"terminal_record_entries":10,"terminal_record_ttl_seconds":3601})")
            .body);
    CHECK(caps.profiles == std::vector<std::string>{"rf-events/1"} && caps.engine == "mock");
    CHECK(caps.modes == std::vector<Mode>({Mode::Wspr, Mode::Qrss, Mode::Fskcw, Mode::Dfcw,
                                           Mode::Cw, Mode::Tone}));
    CHECK(caps.frequency_ranges.size() == 1 && caps.frequency_ranges[0].minimum_nhz == 2 &&
          caps.frequency_ranges[0].maximum_nhz == 3);
    CHECK(caps.max_payload_bytes == 65536 && caps.max_events == 162 &&
          caps.max_job_duration_ns == 110592000000ULL);
    CHECK(caps.minimum_arm_lead_ns == 7 && caps.maximum_arm_ahead_ns == 100 &&
          caps.maximum_arm_uncertainty_ns == 11);
    CHECK(caps.maximum_holdover_age_ns == 13 && caps.output_disable_timeout_ns == 17);
    CHECK(caps.minimum_lease_ms == 5000 && caps.maximum_lease_ms == 60000 &&
          caps.response_cache_entries == 9);
    CHECK(caps.response_cache_ttl_seconds == 301 && caps.terminal_record_entries == 10 &&
          caps.terminal_record_ttl_seconds == 3601);
    for (auto op : {"CLAIM", "RENEW"}) {
        auto lease = std::get<LeaseResponse>(
            success(
                op,
                R"({"owner_id":"22222222222222222222222222222222","granted_lease_ms":5000,"expires_monotonic_ns":"7001"})")
                .body);
        CHECK(lease.owner_id == std::string(32, '2') && lease.granted_lease_ms == 5000 &&
              lease.expires_monotonic_ns == 7001);
    }
    CHECK(std::holds_alternative<Empty>(success("RELEASE", "{}").body));
    auto loaded = std::get<LoadResponse>(
        success(
            "LOAD",
            R"({"job_id":"33333333333333333333333333333333","state":"loaded","adjustments":[{"event_index":1,"requested_frequency_nhz":"3","realized_frequency_nhz":"5"}]})")
            .body);
    CHECK(loaded.job_id == job_id && loaded.adjustments.size() == 1 &&
          loaded.adjustments[0].event_index == 1);
    CHECK(loaded.adjustments[0].requested_frequency_nhz == 3 &&
          loaded.adjustments[0].realized_frequency_nhz == 5);
    const std::string clock_json =
        R"({"state":"holdover","utc_now_ns":"1001","monotonic_now_ns":"17","uncertainty_ns":"3","sync_age_ns":"5","leap":"delete_pending","leap_transition_utc_ns":"2000"})";
    auto check_clock = [](const Clock &c) {
        CHECK(c.state == ClockState::Holdover && c.utc_now_ns == 1001 && c.monotonic_now_ns == 17);
        CHECK(c.uncertainty_ns == 3 && c.sync_age_ns == 5 && c.leap == Leap::DeletePending &&
              c.leap_transition_utc_ns == 2000);
    };
    check_clock(std::get<Clock>(success("GET_CLOCK", clock_json).body));
    auto armed = std::get<ArmResponse>(
        success(
            "ARM",
            "{\"job_id\":\"" + job_id +
                R"(","state":"armed","start_utc_ns":"1101","start_monotonic_ns":"117","clock":)" +
                clock_json + "}")
            .body);
    CHECK(armed.job_id == job_id && armed.start_utc_ns == 1101 && armed.start_monotonic_ns == 117);
    check_clock(armed.clock);
    auto aborted = std::get<AbortResponse>(
        success("ABORT",
                "{\"job_id\":\"" + job_id + R"(","state":"aborted","output_active":false})")
            .body);
    CHECK(aborted.job_id == job_id);
    auto status = std::get<Status>(
        success(
            "STATUS",
            R"({"boot_id":"55555555555555555555555555555555","state":"failed","output_active":true,"owner_id":null,"job_id":"33333333333333333333333333333333","terminal_records":[{"job_id":"33333333333333333333333333333333","state":"failed","ended_monotonic_ns":"123","output_active":true,"error":{"code":"OUTPUT_STATE_UNKNOWN","message":"unknown","retryable":false,"detail":{"x":17}}}]})")
            .body);
    CHECK(status.boot_id == std::string(32, '5') && status.state == State::Failed &&
          status.output_active);
    CHECK(!status.owner_id && status.job_id == job_id && status.terminal_records.size() == 1);
    const auto &terminal = status.terminal_records[0];
    CHECK(terminal.job_id == job_id && terminal.state == State::Failed && terminal.output_active &&
          terminal.ended_monotonic_ns == 123);
    CHECK(terminal.error && terminal.error->code == ErrorCode::OutputStateUnknown &&
          terminal.error->message == "unknown" && !terminal.error->retryable);
    CHECK(terminal.error->detail_json == R"({"x":17})");
    CHECK(std::get<Ping>(success("PING", R"({"token":"a\u0000b"})").body).token ==
          std::string("a\0b", 3));
    auto input =
        "{\"type\":\"event\",\"protocol\":\"WTP/1\",\"session_id\":\"" + session +
        R"(","boot_id":"55555555555555555555555555555555","event_id":"18446744073709551615","event":"DEVICE_FAULT","body":{"state":"failed","output_active":true,"error":{"code":"DEVICE_FAULT","message":"fault","retryable":false}}})";
    auto decoded = decode(input);
    CHECK(decoded);
    input.assign(input.size(), 'x');
    const auto &event = std::get<Event>(*decoded.message);
    CHECK(event.session_id == session && event.boot_id == std::string(32, '5') &&
          event.event_id == std::numeric_limits<std::uint64_t>::max() &&
          event.event == EventKind::DeviceFault);
    const auto &fault = std::get<JobStateEvent>(event.body);
    CHECK(!fault.job_id && fault.state == State::Failed && fault.output_active && fault.error &&
          fault.error->code == ErrorCode::DeviceFault);
}
void malformed_input_stress() {
    // Fixed PRNG for reproducibility, including valid-JSON mutations rather than
    // only random bytes that a UTF-8 validator would reject immediately.
    std::uint32_t seed = 0x12345678;
    auto next = [&] {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    };
    for (unsigned i = 0; i < 2048; ++i) {
        std::string input = i % 2 ? status_request : std::string(next() % 4097, ' ');
        if (i % 2)
            input[next() % input.size()] = static_cast<char>(next() & 255);
        else
            for (auto &c : input)
                c = static_cast<char>(next() & 255);
        auto decoded = decode(input);
        if (decoded) {
            const auto *request = std::get_if<Request>(&*decoded.message);
            CHECK(request && encode_request(*request));
        }
        FrameParser parser;
        feed_all(parser, bytes(input));
        parser.end_of_stream();
        CHECK(parser.closed() && parser.buffered_bytes() == 0);
    }
}
int main() {
    try {
        framing();
        writes();
        typed();
        response_values();
        malformed_input_stress();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "WTP protocol checks passed: " << checks << '\n';
}
