// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "wtp/session.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

// Test-only snapshot of WTP-Client/tests/session_test.cpp's scripted peer.
// Kept outside the portable component to add parent lifecycle fault controls.
#pragma once
namespace backend_test {
using namespace wsprrypi::wtp;
static std::atomic<unsigned> checks{};
#define CHECK(x)                                                               \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(x))                                                                  \
      throw std::runtime_error(std::string("line ") +                          \
                               std::to_string(__LINE__) + ": " + #x);          \
  } while (false)
const std::string sid(32, '1'), owner_id(32, '2'), jid(32, '3'),
    device(32, '4'), boot(32, '5');
std::string quote_json(std::string_view s) {
  return '"' + std::string(s) + '"';
}
std::span<const std::uint8_t> bytes(std::string_view s) {
  return {reinterpret_cast<const std::uint8_t *>(s.data()), s.size()};
}
std::string state_name(State state) {
  const std::array names{"empty",    "loaded",  "armed",  "running",
                         "complete", "aborted", "missed", "failed"};
  return names.at(static_cast<std::size_t>(state));
}
const std::string caps_body =
    R"({"profiles":["rf-events/1"],"modes":["tone","wspr"],"engine":"scripted-test","frequency_ranges":[{"minimum_nhz":"1","maximum_nhz":"30000000000000000"}],"max_payload_bytes":65536,"max_events":162,"max_job_duration_ns":"110592000000","minimum_arm_lead_ns":"1000000","maximum_arm_ahead_ns":"30000000000","maximum_arm_uncertainty_ns":"1000000","maximum_holdover_age_ns":"1000000000","output_disable_timeout_ns":"1000000","minimum_lease_ms":5000,"maximum_lease_ms":60000,"response_cache_entries":8,"response_cache_ttl_seconds":300,"terminal_record_entries":8,"terminal_record_ttl_seconds":3600})";

// Independent small server model. It uses the wire codec to receive frames,
// but implements test lifecycle/cache effects and response expectations here.
struct Peer : ByteStream {
  std::uint64_t now{}, expiry{}, start{}, executions{}, prepares{};
  static constexpr std::uint64_t utc_base = 1'000'000'000'000ULL;
  std::string boot_id{boot}, device_id{device}, session;
  std::optional<std::string> owner;
  std::optional<Job> job;
  State state{State::Empty};
  bool active{}, closed{}, negotiated{}, stall_write{}, stall_read{},
      fail_abort{};
  bool write_throws{}, bad_write_count{}, bad_read_count{};
  std::size_t write_chunk{4096}, read_chunk{4096};
  std::optional<Operation> lose_reply;
  std::function<void(Operation, std::string &)> filter;
  std::function<void(Operation)> before_handle;
  std::vector<std::string> payloads;
  std::vector<Operation> operations;
  struct Cache {
    std::string id, payload, response;
    std::uint64_t completed;
  };
  std::deque<Cache> cache;
  struct Record {
    std::string id;
    State state;
    bool active;
    std::uint64_t ended;
  };
  std::deque<Record> records;
  FrameParser parser;
  std::deque<std::uint8_t> incoming;
  void open() {
    closed = negotiated = false;
    parser = FrameParser{};
    incoming.clear();
  }
  void close() noexcept override { closed = true; }
  std::uint64_t mono() const { return now * 1'000'000; }
  std::string clock() const {
    return R"({"state":"synchronized","utc_now_ns":)" +
           quote_json(std::to_string(utc_base + mono())) +
           R"(,"monotonic_now_ns":)" + quote_json(std::to_string(mono())) +
           R"(,"uncertainty_ns":"1","sync_age_ns":"0","leap":"normal"})";
  }
  void record(State s, bool output = false) {
    state = s;
    active = output;
    if (job)
      records.push_front({job->job_id, state, active, mono()});
    while (records.size() > 8)
      records.pop_back();
  }
  void advance(std::uint64_t time) {
    now = time;
    if (state == State::Armed && mono() >= start) {
      state = State::Running;
      active = true;
      ++executions;
    }
    if (state == State::Running && mono() >= start + job->total_duration_ns)
      record(State::Complete);
    if (owner && mono() >= expiry && state != State::Armed &&
        state != State::Running) {
      if (state == State::Loaded) {
        record(State::Aborted);
        job.reset();
        state = State::Empty;
      }
      owner.reset();
    }
    std::erase_if(cache,
                  [&](const Cache &c) { return now - c.completed >= 300000; });
    std::erase_if(records, [&](const Record &r) {
      return mono() - r.ended >= 3'600'000'000'000ULL;
    });
  }
  std::string status() const {
    std::string text = R"({"boot_id":)" + quote_json(boot_id) + R"(,"state":)" +
                       quote_json(state_name(state)) + R"(,"output_active":)" +
                       (active ? "true" : "false") + R"(,"owner_id":)" +
                       (owner ? quote_json(*owner) : "null") + R"(,"job_id":)" +
                       (job ? quote_json(job->job_id) : "null") +
                       R"(,"terminal_records":[)";
    for (std::size_t i = 0; i < records.size(); ++i) {
      if (i)
        text += ',';
      const auto &r = records[i];
      text += R"({"job_id":)" + quote_json(r.id) + R"(,"state":)" +
              quote_json(state_name(r.state)) + R"(,"ended_monotonic_ns":)" +
              quote_json(std::to_string(r.ended)) + R"(,"output_active":)" +
              (r.active ? "true" : "false");
      if (r.state == State::Failed || r.state == State::Missed)
        text +=
            R"(,"error":{"code":"OUTPUT_STATE_UNKNOWN","message":"unknown","retryable":false})";
      text += '}';
    }
    return text + "]}";
  }
  std::string envelope(const Request &r, std::string body,
                       bool ok = true) const {
    return R"({"type":"response","protocol":"WTP/1","session_id":)" +
           quote_json(r.session_id) + R"(,"request_id":)" +
           quote_json(r.request_id) + R"(,"op":)" +
           quote_json(operation_name(r.op)) + R"(,"ok":)" +
           (ok ? "true,\"body\":" : "false,\"error\":") + body + '}';
  }
  std::string reject(const Request &r, std::string_view code) const {
    return envelope(r,
                    R"({"code":)" + quote_json(code) +
                        R"(,"message":"rejected","retryable":false})",
                    false);
  }
  std::string handle(const Request &r) {
    if (!negotiated && r.op != Operation::Hello)
      return reject(r, "HELLO_REQUIRED");
    if (r.op == Operation::Hello) {
      negotiated = true;
      session = r.session_id;
      return envelope(r, R"({"selected_version":"WTP/1","device_id":)" +
                             quote_json(device_id) + R"(,"boot_id":)" +
                             quote_json(boot_id) +
                             R"(,"product":"Test","firmware_version":"1"})");
    }
    if (r.op == Operation::Status)
      return envelope(r, status());
    if (r.op == Operation::Caps)
      return envelope(r, caps_body);
    if (r.op == Operation::GetClock)
      return envelope(r, clock());
    if (r.op == Operation::Ping) {
      const auto &token = std::get<Ping>(r.body).token;
      return envelope(r,
                      token ? "{\"token\":" + quote_json(*token) + '}' : "{}");
    }
    if (r.op == Operation::Claim || r.op == Operation::Renew) {
      if (r.op == Operation::Claim && owner)
        return reject(r, "BUSY");
      if (r.op == Operation::Renew && !owner)
        return reject(r, "LEASE_EXPIRED");
      if (r.op == Operation::Renew && *owner != owner_id)
        return reject(r, "NOT_OWNER");
      const auto &claim = std::get<LeaseRequest>(r.body);
      owner = claim.owner_id;
      expiry = mono() + static_cast<std::uint64_t>(claim.lease_ms) * 1'000'000;
      return envelope(r, R"({"owner_id":)" + quote_json(*owner) +
                             R"(,"granted_lease_ms":)" +
                             std::to_string(claim.lease_ms) +
                             R"(,"expires_monotonic_ns":)" +
                             quote_json(std::to_string(expiry)) + '}');
    }
    if (!owner)
      return reject(r, "LEASE_EXPIRED");
    if (*owner != owner_id)
      return reject(r, "NOT_OWNER");
    if (r.op == Operation::Load) {
      const auto &candidate = std::get<Job>(r.body);
      if (!job || job->job_id != candidate.job_id) {
        job = candidate;
        ++prepares;
        state = State::Loaded;
      }
      return envelope(r, R"({"job_id":)" + quote_json(candidate.job_id) +
                             R"(,"state":"loaded","adjustments":[]})");
    }
    if (r.op == Operation::Arm) {
      const auto &arm = std::get<ArmRequest>(r.body);
      if (!job || job->job_id != arm.job_id)
        return reject(r, "JOB_NOT_FOUND");
      if (state != State::Loaded)
        return reject(r, "INVALID_STATE");
      if (arm.start_utc_ns < utc_base + mono() + 1'000'000)
        return reject(r, "ARM_TOO_LATE");
      start = arm.start_utc_ns - utc_base;
      state = State::Armed;
      return envelope(r, R"({"job_id":)" + quote_json(job->job_id) +
                             R"(,"state":"armed","start_utc_ns":)" +
                             quote_json(std::to_string(arm.start_utc_ns)) +
                             R"(,"start_monotonic_ns":)" +
                             quote_json(std::to_string(start)) +
                             R"(,"clock":)" + clock() + '}');
    }
    if (r.op == Operation::Abort) {
      if (!job || job->job_id != std::get<AbortRequest>(r.body).job_id)
        return reject(r, "JOB_NOT_FOUND");
      if (state == State::Complete || state == State::Missed)
        return reject(r, "INVALID_STATE");
      if (fail_abort) {
        record(State::Failed, true);
        return reject(r, "OUTPUT_STATE_UNKNOWN");
      }
      if (state != State::Aborted)
        record(State::Aborted);
      return envelope(r, R"({"job_id":)" + quote_json(job->job_id) +
                             R"(,"state":"aborted","output_active":false})");
    }
    CHECK(r.op == Operation::Release);
    if (state == State::Armed || state == State::Running ||
        state == State::Failed)
      return reject(r, "INVALID_STATE");
    owner.reset();
    job.reset();
    state = State::Empty;
    return envelope(r, "{}");
  }
  void enqueue(std::string_view text) {
    auto frame = encode_frame(bytes(text));
    incoming.insert(incoming.end(), frame.begin(), frame.end());
  }
  void advisory(std::uint64_t id, std::string_view kind = "JOB_STATE",
                std::string body = {}) {
    if (body.empty())
      body = R"({"job_id":)" + (job ? quote_json(job->job_id) : "null") +
             R"(,"state":)" + quote_json(state_name(state)) +
             R"(,"output_active":)" + (active ? "true" : "false") + '}';
    enqueue(R"({"type":"event","protocol":"WTP/1","session_id":)" +
            quote_json(session) + R"(,"boot_id":)" + quote_json(boot_id) +
            R"(,"event_id":)" + quote_json(std::to_string(id)) +
            R"(,"event":)" + quote_json(kind) + R"(,"body":)" + body + '}');
  }
  IoResult write(std::span<const std::uint8_t> input) override {
    if (write_throws)
      throw std::runtime_error("scripted write exception");
    if (bad_write_count)
      return {IoState::Progress, input.size() + 1};
    if (closed)
      return {IoState::Closed};
    if (stall_write)
      return {IoState::WouldBlock};
    const auto count = std::min(input.size(), write_chunk);
    auto read = parser.feed(input.first(count), now);
    CHECK(read.consumed == count);
    for (const auto &frame : read.events) {
      CHECK(frame.kind == FrameEventKind::Payload);
      std::string payload(frame.payload.begin(), frame.payload.end());
      auto message = decode(payload);
      CHECK(message);
      const auto &r = std::get<Request>(*message.message);
      operations.push_back(r.op);
      payloads.push_back(payload);
      advance(now);
      std::string reply;
      const auto cached =
          std::find_if(cache.begin(), cache.end(),
                       [&](const Cache &c) { return c.id == r.request_id; });
      if (cached != cache.end()) {
        reply = cached->payload == payload ? cached->response
                                           : reject(r, "REQUEST_ID_REUSE");
      } else {
        if (before_handle)
          before_handle(r.op);
        reply = handle(r);
        cache.push_back({r.request_id, payload, reply, now});
        if (cache.size() > 8)
          cache.pop_front();
      }
      if (lose_reply == r.op) {
        lose_reply.reset();
        closed = true;
        continue;
      }
      if (filter)
        filter(r.op, reply);
      enqueue(reply);
    }
    return {IoState::Progress, count};
  }
  IoResult read(std::span<std::uint8_t> output) override {
    if (bad_read_count)
      return {IoState::Progress, output.size() + 1};
    if (closed)
      return {IoState::Closed};
    if (stall_read || incoming.empty())
      return {IoState::WouldBlock};
    const auto count = std::min({incoming.size(), output.size(), read_chunk});
    for (std::size_t i = 0; i < count; ++i) {
      output[i] = incoming.front();
      incoming.pop_front();
    }
    return {IoState::Progress, count};
  }
};

} // namespace backend_test
