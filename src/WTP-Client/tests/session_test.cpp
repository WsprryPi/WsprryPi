// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "wtp/session.hpp"
#include <algorithm>
#include <array>
#include <deque>
#include <functional>
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
const std::string sid(32, '1'), owner_id(32, '2'), jid(32, '3'), device(32, '4'), boot(32, '5');
std::string quoted(std::string_view s) { return '"' + std::string(s) + '"'; }
std::span<const std::uint8_t> bytes(std::string_view s) {
    return {reinterpret_cast<const std::uint8_t *>(s.data()), s.size()};
}
std::string state_name(State state) {
    const std::array names{"empty",    "loaded",  "armed",  "running",
                           "complete", "aborted", "missed", "failed"};
    return names.at(static_cast<std::size_t>(state));
}
const std::string caps_body =
    R"({"profiles":["rf-events/1"],"modes":["tone","wspr"],"engine":"scripted-test","frequency_ranges":[{"minimum_nhz":"1","maximum_nhz":"30000000000000000"}],"max_payload_bytes":65536,"max_events":162,"max_job_duration_ns":"110592000000","minimum_arm_lead_ns":"1000000","maximum_arm_ahead_ns":"10000000000","maximum_arm_uncertainty_ns":"1000000","maximum_holdover_age_ns":"1000000000","output_disable_timeout_ns":"1000000","minimum_lease_ms":5000,"maximum_lease_ms":60000,"response_cache_entries":8,"response_cache_ttl_seconds":300,"terminal_record_entries":8,"terminal_record_ttl_seconds":3600})";

// Independent small server model. It uses the wire codec to receive frames,
// but implements test lifecycle/cache effects and response expectations here.
struct Peer : ByteStream {
    std::uint64_t now{}, expiry{}, start{}, executions{}, prepares{};
    static constexpr std::uint64_t utc_base = 1'000'000'000'000ULL;
    std::string boot_id{boot}, device_id{device}, session;
    std::optional<std::string> owner;
    std::optional<Job> job;
    State state{State::Empty};
    bool active{}, closed{}, negotiated{}, stall_write{}, stall_read{}, fail_abort{};
    bool write_throws{}, bad_write_count{}, bad_read_count{};
    std::size_t write_chunk{4096}, read_chunk{4096};
    std::optional<Operation> lose_reply;
    std::function<void(Operation, std::string &)> filter;
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
               quoted(std::to_string(utc_base + mono())) + R"(,"monotonic_now_ns":)" +
               quoted(std::to_string(mono())) +
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
        if (owner && mono() >= expiry && state != State::Armed && state != State::Running) {
            if (state == State::Loaded) {
                record(State::Aborted);
                job.reset();
                state = State::Empty;
            }
            owner.reset();
        }
        std::erase_if(cache, [&](const Cache &c) { return now - c.completed >= 300000; });
        std::erase_if(records,
                      [&](const Record &r) { return mono() - r.ended >= 3'600'000'000'000ULL; });
    }
    std::string status() const {
        std::string text = R"({"boot_id":)" + quoted(boot_id) + R"(,"state":)" +
                           quoted(state_name(state)) + R"(,"output_active":)" +
                           (active ? "true" : "false") + R"(,"owner_id":)" +
                           (owner ? quoted(*owner) : "null") + R"(,"job_id":)" +
                           (job ? quoted(job->job_id) : "null") + R"(,"terminal_records":[)";
        for (std::size_t i = 0; i < records.size(); ++i) {
            if (i)
                text += ',';
            const auto &r = records[i];
            text += R"({"job_id":)" + quoted(r.id) + R"(,"state":)" + quoted(state_name(r.state)) +
                    R"(,"ended_monotonic_ns":)" + quoted(std::to_string(r.ended)) +
                    R"(,"output_active":)" + (r.active ? "true" : "false");
            if (r.state == State::Failed || r.state == State::Missed)
                text +=
                    R"(,"error":{"code":"OUTPUT_STATE_UNKNOWN","message":"unknown","retryable":false})";
            text += '}';
        }
        return text + "]}";
    }
    std::string envelope(const Request &r, std::string body, bool ok = true) const {
        return R"({"type":"response","protocol":"WTP/1","session_id":)" + quoted(r.session_id) +
               R"(,"request_id":)" + quoted(r.request_id) + R"(,"op":)" +
               quoted(operation_name(r.op)) + R"(,"ok":)" +
               (ok ? "true,\"body\":" : "false,\"error\":") + body + '}';
    }
    std::string reject(const Request &r, std::string_view code) const {
        return envelope(
            r, R"({"code":)" + quoted(code) + R"(,"message":"rejected","retryable":false})", false);
    }
    std::string handle(const Request &r) {
        if (!negotiated && r.op != Operation::Hello)
            return reject(r, "HELLO_REQUIRED");
        if (r.op == Operation::Hello) {
            negotiated = true;
            session = r.session_id;
            return envelope(r, R"({"selected_version":"WTP/1","device_id":)" + quoted(device_id) +
                                   R"(,"boot_id":)" + quoted(boot_id) +
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
            return envelope(r, token ? "{\"token\":" + quoted(*token) + '}' : "{}");
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
            return envelope(r, R"({"owner_id":)" + quoted(*owner) + R"(,"granted_lease_ms":)" +
                                   std::to_string(claim.lease_ms) + R"(,"expires_monotonic_ns":)" +
                                   quoted(std::to_string(expiry)) + '}');
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
            return envelope(r, R"({"job_id":)" + quoted(candidate.job_id) +
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
            return envelope(
                r, R"({"job_id":)" + quoted(job->job_id) + R"(,"state":"armed","start_utc_ns":)" +
                       quoted(std::to_string(arm.start_utc_ns)) + R"(,"start_monotonic_ns":)" +
                       quoted(std::to_string(start)) + R"(,"clock":)" + clock() + '}');
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
            return envelope(r, R"({"job_id":)" + quoted(job->job_id) +
                                   R"(,"state":"aborted","output_active":false})");
        }
        CHECK(r.op == Operation::Release);
        if (state == State::Armed || state == State::Running || state == State::Failed)
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
    void advisory(std::uint64_t id, std::string_view kind = "JOB_STATE", std::string body = {}) {
        if (body.empty())
            body = R"({"job_id":)" + (job ? quoted(job->job_id) : "null") + R"(,"state":)" +
                   quoted(state_name(state)) + R"(,"output_active":)" +
                   (active ? "true" : "false") + '}';
        enqueue(R"({"type":"event","protocol":"WTP/1","session_id":)" + quoted(session) +
                R"(,"boot_id":)" + quoted(boot_id) + R"(,"event_id":)" +
                quoted(std::to_string(id)) + R"(,"event":)" + quoted(kind) + R"(,"body":)" + body +
                '}');
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
            const auto cached = std::find_if(cache.begin(), cache.end(),
                                             [&](const Cache &c) { return c.id == r.request_id; });
            if (cached != cache.end()) {
                reply =
                    cached->payload == payload ? cached->response : reject(r, "REQUEST_ID_REUSE");
            } else {
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
struct Fixture {
    Peer peer;
    Session client{{sid, owner_id, device}};
    void pump() {
        for (int i = 0; i < 10000; ++i) {
            client.poll(peer.now);
            if (client.phase() == SessionPhase::Ready && !client.busy() && !client.needs_status())
                return;
            if (client.phase() == SessionPhase::Disconnected ||
                client.phase() == SessionPhase::Fault ||
                client.phase() == SessionPhase::IdentityChanged)
                return;
        }
        throw std::runtime_error("Session did not become idle: " + client.diagnostic());
    }
    void connect() {
        peer.open();
        CHECK(client.connect(peer, peer.now));
        pump();
    }
    TransactionResult send(Operation op, RequestBody body) {
        CHECK(client.request(op, std::move(body), peer.now));
        pump();
        auto result = client.take_result();
        CHECK(result && result->operation == op);
        return *result;
    }
    void claim() {
        CHECK(send(Operation::Claim, LeaseRequest{owner_id, 5000}).kind ==
              ResultKind::Acknowledged);
        CHECK(client.owns());
    }
    void load(std::string id = jid) {
        CHECK(
            send(Operation::Load,
                 Job{std::move(id), Mode::Tone, 1'000'000, {{0, 1'000'000, true, 1000000000}}, {}})
                .kind == ResultKind::Acknowledged);
    }
    void arm() {
        CHECK(send(Operation::Arm, ArmRequest{jid, Peer::utc_base + peer.mono() + 50'000'000, 100})
                  .kind == ResultKind::Acknowledged);
    }
    void lost(Operation op, RequestBody body) {
        peer.lose_reply = op;
        CHECK(send(op, std::move(body)).kind == ResultKind::Unknown);
        CHECK(client.uncertain());
    }
    void refresh() { CHECK(send(Operation::Status, Empty{}).kind == ResultKind::Acknowledged); }
};
Job test_job(std::string id = jid) {
    return {std::move(id), Mode::Tone, 1'000'000, {{0, 1'000'000, true, 1000000000}}, {}};
}

void successful_jobs() {
    Fixture f;
    f.peer.write_chunk = 3;
    f.peer.read_chunk = 7;
    f.connect();
    CHECK(f.peer.operations ==
          std::vector<Operation>({Operation::Hello, Operation::Status, Operation::Caps}));
    CHECK(!f.client.request(Operation::Arm, ArmRequest{jid, 1, 1}, 0));
    f.claim();
    f.load();
    f.arm();
    CHECK(!f.client.job_evidence()->completed());
    f.client.disconnect();
    const auto traffic = f.peer.operations.size();
    f.peer.advance(51); // Software engine completes with no transport input.
    CHECK(f.peer.executions == 1 && !f.peer.active && f.peer.operations.size() == traffic);
    f.connect();
    CHECK(f.client.job_evidence()->completed());
    CHECK(f.send(Operation::Release, Empty{}).kind == ResultKind::Acknowledged);
    f.claim();
    f.load(std::string(32, '6'));
    CHECK(f.send(Operation::Abort, AbortRequest{std::string(32, '6')}).kind ==
          ResultKind::Acknowledged);
    CHECK(f.client.job_evidence()->cancelled() && f.peer.executions == 1 && f.peer.prepares == 2);
}
void ownership_and_lease() {
    Fixture foreign;
    foreign.peer.owner = std::string(32, '9');
    foreign.peer.expiry = 100000000000ULL;
    foreign.connect();
    CHECK(!foreign.client.owns());
    CHECK(!foreign.client.request(Operation::Claim, LeaseRequest{owner_id, 5000}, 0));
    CHECK(!foreign.client.request(Operation::Release, Empty{}, 0));
    CHECK(!foreign.client.request(Operation::Abort, AbortRequest{jid}, 0));
    CHECK(foreign.peer.operations.size() == 3);
    Fixture collision;
    collision.peer.owner = owner_id;
    collision.peer.expiry = 100000000000ULL;
    collision.connect();
    CHECK(!collision.client.owns()); // Owner ID alone cannot establish our session's authority.
    Fixture f;
    f.connect();
    f.claim();
    CHECK(f.client.lease_valid(4999) && !f.client.lease_valid(5000));
    CHECK(!f.client.renewal_due(2499) && f.client.renewal_due(2500));
    f.peer.advance(2500);
    CHECK(f.send(Operation::Renew, LeaseRequest{owner_id, 5000}).kind == ResultKind::Acknowledged);
    CHECK(f.client.lease_valid(7499) && !f.client.lease_valid(7500));
    f.peer.advance(7500);
    CHECK(!f.client.request(Operation::Load, test_job(), 7500));
    CHECK(!f.client.request(Operation::Renew, LeaseRequest{owner_id, 5000}, 7500));
    Fixture lost;
    lost.connect();
    lost.lost(Operation::Claim, LeaseRequest{owner_id, 5000});
    lost.connect();
    auto resolution = lost.client.take_result();
    CHECK(resolution && resolution->kind == ResultKind::Reconciled);
    CHECK(lost.client.owns() && !lost.client.lease_valid(0));
    CHECK(!lost.client.retry_uncertain(0));
    CHECK(lost.send(Operation::Renew, LeaseRequest{owner_id, 5000}).kind ==
          ResultKind::Acknowledged);
    CHECK(lost.client.lease_valid(0));
    lost.lost(Operation::Renew, LeaseRequest{owner_id, 5000});
    lost.connect();
    CHECK(lost.client.take_result()->kind == ResultKind::Reconciled);
    CHECK(!lost.client.retry_uncertain(0));
    lost.lost(Operation::Release, Empty{});
    lost.connect();
    CHECK(lost.client.take_result()->kind == ResultKind::Reconciled && !lost.client.owns());
}
void uncertain_jobs() {
    Fixture f;
    f.connect();
    f.claim();
    f.lost(Operation::Load, test_job());
    auto original = f.peer.payloads.back();
    f.peer.cache.clear();
    f.connect();
    CHECK(f.client.uncertain() && f.peer.prepares == 1);
    CHECK(!f.client.request(Operation::Load, test_job(std::string(32, '7')), 0));
    CHECK(f.client.retry_uncertain(0));
    f.pump();
    CHECK(f.client.take_result()->kind == ResultKind::Acknowledged && !f.client.uncertain());
    CHECK(std::count(f.peer.payloads.begin(), f.peer.payloads.end(), original) == 2 &&
          f.peer.prepares == 1);
    f.lost(Operation::Arm, ArmRequest{jid, Peer::utc_base + 50'000'000, 100});
    f.connect();
    CHECK(f.client.take_result()->kind == ResultKind::Reconciled && !f.client.uncertain());
    CHECK(f.peer.executions == 0 && f.peer.state == State::Armed);
    f.lost(Operation::Abort, AbortRequest{jid});
    f.connect();
    CHECK(f.client.take_result()->kind == ResultKind::Reconciled &&
          f.client.job_evidence()->cancelled());

    Fixture evicted;
    evicted.connect();
    evicted.claim();
    evicted.lost(Operation::Load, test_job());
    evicted.peer.advance(6000);
    evicted.peer.records.clear();
    evicted.peer.cache.clear();
    evicted.connect();
    CHECK(evicted.client.uncertain() && !evicted.client.retry_uncertain(6000));
    CHECK(!evicted.client.request(Operation::Load, test_job(), 6000));
    CHECK(!evicted.client.job_evidence()->completed() && evicted.peer.prepares == 1);

    Fixture expired;
    expired.connect();
    expired.claim();
    expired.lost(Operation::Load, test_job());
    expired.peer.advance(8000);
    expired.connect();
    CHECK(expired.client.take_result()->kind == ResultKind::Reconciled); // Retained aborted result.
    CHECK(expired.client.job_evidence()->cancelled() && !expired.client.retry_uncertain(8000));
}
void identity_and_events() {
    Fixture f;
    f.connect();
    f.claim();
    f.load();
    f.peer.advisory(0);
    f.peer.advisory(2);
    f.peer.advisory(2);
    f.pump();
    CHECK(f.client.event_gaps() == 1 && f.client.stale_messages() == 1 && !f.client.needs_status());
    const auto traffic = f.peer.operations.size();
    f.peer.advisory(
        3, "JOB_STATE",
        R"({"job_id":"33333333333333333333333333333333","state":"complete","output_active":false})");
    f.pump();
    CHECK(!f.client.job_evidence()->completed() && f.peer.operations.size() > traffic);
    f.peer.advisory(
        4, "SESSION_REPLACED",
        R"({"error":{"code":"SESSION_REPLACED","message":"replaced","retryable":false}})");
    f.pump();
    CHECK(f.client.phase() == SessionPhase::Disconnected &&
          !f.client.job_evidence()->authoritative);
    f.connect();
    f.client.disconnect();
    f.peer.boot_id = std::string(32, '8');
    f.connect();
    CHECK(f.client.phase() == SessionPhase::IdentityChanged && !f.client.owns());
    CHECK(f.peer.operations.back() == Operation::Hello);
    Fixture device_change;
    device_change.peer.device_id = std::string(32, '9');
    device_change.connect();
    CHECK(device_change.client.phase() == SessionPhase::IdentityChanged &&
          device_change.peer.operations.size() == 1);
}
void learned_plain_identity() {
    SessionOptions options{sid, owner_id, ""};
    options.learn_device_identity = true;
    Session client{options};
    Peer peer;
    peer.open();
    CHECK(client.connect(peer, peer.now));
    for (unsigned i = 0; i < 1000 && client.phase() != SessionPhase::Ready; ++i)
        client.poll(peer.now);
    CHECK(client.phase() == SessionPhase::Ready);
    CHECK(client.identity() && client.identity()->device_id == device);
    client.disconnect();
    peer.device_id = std::string(32, '9');
    peer.open();
    CHECK(client.connect(peer, peer.now));
    for (unsigned i = 0; i < 1000 && client.phase() != SessionPhase::IdentityChanged; ++i)
        client.poll(peer.now);
    CHECK(client.phase() == SessionPhase::IdentityChanged);
    bool rejected = false;
    try { Session invalid{{sid, owner_id, ""}}; }
    catch (const std::invalid_argument &) { rejected = true; }
    CHECK(rejected);
}
void stalls_and_faults() {
    Fixture f;
    f.connect();
    f.claim();
    f.peer.stall_write = true;
    CHECK(f.client.request(Operation::Load, test_job(), 0));
    f.client.poll(0);
    f.client.poll(5000);
    CHECK(f.client.take_result()->kind == ResultKind::NotSent && !f.client.uncertain() &&
          f.peer.closed);
    Fixture partial;
    partial.connect();
    partial.claim();
    partial.peer.write_chunk = 1;
    CHECK(partial.client.request(Operation::Load, test_job(), 0));
    partial.client.poll(0);
    partial.client.disconnect();
    CHECK(partial.client.take_result()->kind == ResultKind::Unknown && partial.client.uncertain());
    Fixture cancel;
    cancel.connect();
    cancel.claim();
    CHECK(cancel.client.request(Operation::Load, test_job(), 0));
    cancel.client.disconnect();
    CHECK(cancel.client.take_result()->kind == ResultKind::NotSent && !cancel.client.uncertain());
    Fixture throwing;
    throwing.connect();
    throwing.claim();
    throwing.peer.write_throws = true;
    CHECK(throwing.client.request(Operation::Load, test_job(), 0));
    throwing.pump();
    CHECK(throwing.client.take_result()->kind == ResultKind::Unknown &&
          throwing.client.phase() == SessionPhase::Fault);
    Fixture count;
    count.connect();
    count.peer.bad_read_count = true;
    count.pump();
    CHECK(count.client.phase() == SessionPhase::Fault);
    Fixture regression;
    regression.peer.now = 100;
    regression.connect();
    regression.client.poll(99);
    CHECK(regression.client.phase() == SessionPhase::Fault);
    Fixture fault;
    fault.connect();
    fault.claim();
    fault.load();
    fault.peer.fail_abort = true;
    CHECK(fault.send(Operation::Abort, AbortRequest{jid}).kind == ResultKind::Rejected);
    CHECK(fault.client.safety_fault() && !fault.client.job_evidence()->cancelled());
    fault.peer.active = false;
    fault.peer.owner.reset();
    fault.peer.job.reset();
    fault.peer.state = State::Empty;
    fault.peer.records.clear();
    fault.refresh();
    CHECK(fault.client.safety_fault() &&
          !fault.client.request(Operation::Claim, LeaseRequest{owner_id, 5000}, 0));
}
void correlation() {
    Fixture f;
    f.connect();
    f.peer.filter = [](Operation op, std::string &reply) {
        if (op == Operation::Ping)
            reply.replace(reply.find("PING"), 4, "CAPS");
    };
    CHECK(f.client.request(Operation::Ping, Ping{}, 0));
    f.pump();
    CHECK(f.client.phase() == SessionPhase::Fault);
    Fixture owner;
    owner.connect();
    owner.peer.filter = [](Operation op, std::string &reply) {
        if (op == Operation::Claim)
            reply.replace(reply.find(owner_id), 32, std::string(32, '9'));
    };
    CHECK(owner.send(Operation::Claim, LeaseRequest{owner_id, 5000}).kind == ResultKind::Unknown);
    CHECK(owner.client.phase() == SessionPhase::Fault && !owner.client.owns());
    Fixture arm;
    arm.connect();
    arm.claim();
    arm.load();
    arm.peer.filter = [](Operation op, std::string &reply) {
        if (op == Operation::Arm) {
            const auto pos = reply.find("\"start_monotonic_ns\":\"") + 22;
            reply.insert(pos, "1");
        }
    };
    CHECK(arm.send(Operation::Arm, ArmRequest{jid, Peer::utc_base + 50'000'000, 100}).kind ==
          ResultKind::Unknown);
    CHECK(arm.client.phase() == SessionPhase::Fault);
}
void replace_string(std::string &message, std::string_view field, std::string_view value) {
    const auto prefix = quoted(field) + ":\"";
    const auto start = message.find(prefix);
    CHECK(start != std::string::npos);
    const auto end = message.find('"', start + prefix.size());
    message.replace(start + prefix.size(), end - start - prefix.size(), value);
}
void adversarial_recovery() {
    // A stale response or continuing traffic cannot renew the absolute budget.
    Fixture deadline;
    deadline.connect();
    std::string stale;
    deadline.peer.filter = [&](Operation op, std::string &reply) {
        if (op == Operation::Ping) {
            replace_string(reply, "request_id", std::string(32, '9'));
            stale = reply;
        }
    };
    CHECK(deadline.client.request(Operation::Ping, Ping{}, 0));
    deadline.client.poll(0);
    for (auto now : {4000ULL, 7999ULL}) {
        deadline.peer.now = now;
        deadline.peer.enqueue(stale);
        deadline.client.poll(now);
    }
    deadline.client.poll(8000);
    CHECK(deadline.client.phase() == SessionPhase::Disconnected &&
          deadline.client.stale_messages() == 3);
    CHECK(deadline.client.take_result()->kind == ResultKind::Unknown &&
          !deadline.client.uncertain());

    // A failed partial ARM has no complete request at the server. The retained
    // exact transaction can still ARM once after same-boot STATUS reconciliation.
    Fixture partial;
    partial.connect();
    partial.claim();
    partial.load();
    partial.peer.write_chunk = 1;
    CHECK(partial.client.request(Operation::Arm, ArmRequest{jid, Peer::utc_base + 50'000'000, 100},
                                 0));
    partial.client.poll(0);
    partial.client.disconnect();
    CHECK(partial.client.take_result()->kind == ResultKind::Unknown);
    partial.peer.write_chunk = 4096;
    partial.connect();
    CHECK(partial.client.uncertain());
    CHECK(partial.client.retry_uncertain(0));
    partial.pump();
    CHECK(partial.client.take_result()->kind == ResultKind::Acknowledged);
    partial.peer.advance(50);
    CHECK(partial.peer.executions == 1);

    // An expired transaction does not acquire a fresh deadline on reconnect.
    // Explicit safety cleanup is a separate bounded transaction.
    Fixture budget;
    budget.connect();
    CHECK(budget.send(Operation::Claim, LeaseRequest{owner_id, 60000}).kind ==
          ResultKind::Acknowledged);
    budget.lost(Operation::Load, test_job());
    budget.peer.advance(8000);
    budget.connect();
    CHECK(budget.client.uncertain() && budget.client.lease_valid(8000) &&
          !budget.client.retry_uncertain(8000));
    CHECK(budget.send(Operation::Abort, AbortRequest{jid}).kind == ResultKind::Acknowledged);
    CHECK(!budget.client.uncertain() && budget.client.job_evidence()->cancelled() &&
          budget.peer.executions == 0);

    // ABORT can race with completion. The terminal result remains completion.
    Fixture race;
    race.connect();
    race.claim();
    race.load();
    race.arm();
    race.peer.now = 51; // Server advances when it receives ABORT, after local admission.
    CHECK(race.send(Operation::Abort, AbortRequest{jid}).kind == ResultKind::Rejected);
    CHECK(race.client.job_evidence()->completed() && !race.client.job_evidence()->cancelled());
    CHECK(race.peer.executions == 1);

    // An owner label returning after ownership was lost is not session proof.
    Fixture label;
    label.connect();
    label.claim();
    label.peer.advance(5000);
    label.refresh();
    CHECK(!label.client.owns());
    label.peer.owner = owner_id;
    label.peer.expiry = 20'000'000'000;
    label.refresh();
    CHECK(!label.client.owns());
    CHECK(!label.client.request(Operation::Release, Empty{}, 5000));

    // An error from a retry does not establish that its earlier execution failed.
    Fixture missing;
    missing.connect();
    missing.claim();
    missing.lost(Operation::Load, test_job());
    missing.connect();
    missing.peer.filter = [&](Operation op, std::string &reply) {
        if (op == Operation::Load) {
            const auto decoded = decode(missing.peer.payloads.back());
            reply = missing.peer.reject(std::get<Request>(*decoded.message), "JOB_NOT_FOUND");
        }
    };
    CHECK(missing.client.retry_uncertain(0));
    missing.pump();
    CHECK(missing.client.take_result()->kind == ResultKind::Rejected && missing.client.uncertain());
    CHECK(!missing.client.request(Operation::Load, test_job(std::string(32, '8')), 0));

    // Events cannot clear a fault even if a later STATUS reports recovered idle.
    Fixture advisory;
    advisory.connect();
    advisory.claim();
    advisory.load();
    advisory.peer.advisory(
        0, "JOB_STATE",
        R"({"job_id":"33333333333333333333333333333333","state":"failed","output_active":true,"error":{"code":"OUTPUT_STATE_UNKNOWN","message":"fault","retryable":false}})");
    advisory.pump();
    CHECK(advisory.client.safety_fault());
    CHECK(advisory.send(Operation::Abort, AbortRequest{jid}).kind == ResultKind::Acknowledged);
    CHECK(advisory.client.safety_fault() && advisory.client.job_evidence()->cancelled());

    // A terminal fault record retains historical output, distinct from the
    // current failed state's newly inactive output. Preserve both safely.
    Fixture historical;
    historical.connect();
    historical.claim();
    historical.load();
    historical.peer.record(State::Failed, true);
    historical.peer.active = false;
    historical.refresh();
    CHECK(historical.client.phase() == SessionPhase::Ready && historical.client.safety_fault());
    CHECK(!historical.client.job_evidence()->completed() &&
          !historical.client.job_evidence()->cancelled());
    CHECK(historical.client.status()->terminal_records.front().output_active);

    Fixture records;
    records.connect();
    records.claim();
    records.load();
    records.peer.record(State::Aborted);
    records.peer.records.push_back(records.peer.records.front());
    CHECK(records.client.request(Operation::Status, Empty{}, 0));
    records.pump();
    CHECK(records.client.phase() == SessionPhase::Fault &&
          records.client.take_result()->kind == ResultKind::Unknown);
}
void acknowledgment_validation() {
    for (const auto &[field, value] : std::vector<std::pair<std::string, std::string>>{
             {"job_id", std::string(32, '9')},
             {"uncertainty_ns", "101"},
             {"leap", "unknown"},
             {"monotonic_now_ns", "18446744073709551615"},
             {"start_utc_ns", "1000050000001"}}) {
        Fixture f;
        f.connect();
        f.claim();
        f.load();
        f.peer.filter = [&](Operation op, std::string &reply) {
            if (op == Operation::Arm)
                replace_string(reply, field, value);
        };
        CHECK(f.send(Operation::Arm, ArmRequest{jid, Peer::utc_base + 50'000'000, 100}).kind ==
              ResultKind::Unknown);
        CHECK(f.client.phase() == SessionPhase::Fault && f.client.uncertain());
    }
    Fixture adjustments;
    adjustments.connect();
    adjustments.claim();
    adjustments.peer.filter = [](Operation op, std::string &reply) {
        if (op == Operation::Load)
            reply.replace(
                reply.find("[]"), 2,
                R"([{"event_index":0,"requested_frequency_nhz":"1000000000","realized_frequency_nhz":"1000000001"}])");
    };
    CHECK(adjustments.send(Operation::Load, test_job()).kind ==
          ResultKind::Unknown); // Adjustment was not allowed.
    Fixture ping;
    ping.connect();
    ping.peer.filter = [](Operation op, std::string &reply) {
        if (op == Operation::Ping)
            replace_string(reply, "token", "wrong");
    };
    CHECK(ping.send(Operation::Ping, Ping{std::string("expected")}).kind == ResultKind::Unknown);
    CHECK(ping.client.phase() == SessionPhase::Fault);
    Fixture device_status;
    device_status.connect();
    device_status.peer.filter = [](Operation op, std::string &reply) {
        if (op == Operation::Status)
            replace_string(reply, "boot_id", std::string(32, '9'));
    };
    CHECK(device_status.send(Operation::Status, Empty{}).kind == ResultKind::Unknown);
    CHECK(device_status.client.phase() == SessionPhase::IdentityChanged);
    Fixture wrong_session;
    wrong_session.connect();
    wrong_session.peer.filter = [](Operation op, std::string &reply) {
        if (op == Operation::Ping)
            replace_string(reply, "session_id", std::string(32, '9'));
    };
    CHECK(wrong_session.client.request(Operation::Ping, Ping{}, 0));
    wrong_session.client.poll(0);
    CHECK(wrong_session.client.busy() && wrong_session.client.stale_messages() == 1);
    wrong_session.client.poll(5000);
    CHECK(wrong_session.client.take_result()->kind == ResultKind::Unknown);

    Fixture limits;
    limits.connect();
    limits.claim();
    auto job = test_job();
    job.mode = Mode::Cw;
    CHECK(!limits.client.request(Operation::Load, job, 0));
    job.mode = Mode::Tone;
    job.events[0].frequency_nhz = 30000000000000001ULL;
    CHECK(!limits.client.request(Operation::Load, job, 0));
    CHECK(!limits.client.request(Operation::Claim, LeaseRequest{std::string(32, '9'), 5000}, 0));
}
void observation_ordering() {
    Fixture storm;
    std::uint64_t event_id{};
    storm.peer.filter = [&](Operation op, std::string &) {
        if (op == Operation::Status)
            storm.peer.advisory(event_id++);
    };
    storm.peer.open();
    CHECK(storm.client.connect(storm.peer, 0));
    for (std::uint64_t now = 0; now <= 8000; now += 1000) {
        storm.peer.now = now;
        storm.client.poll(now);
    }
    CHECK(storm.client.phase() == SessionPhase::Disconnected && storm.peer.operations.size() <= 8);
    CHECK(storm.client.diagnostic() == "Negotiation deadline expired");

    Fixture f;
    f.connect();
    f.claim();
    f.load();
    unsigned observations = 0;
    f.peer.filter = [&](Operation op, std::string &) {
        if (op != Operation::Status)
            return;
        if (++observations == 1) {
            // The response was sampled loaded, but an asynchronous state event
            // is queued ahead of it. The first snapshot cannot resolve the gap.
            f.peer.record(State::Aborted);
            f.peer.advisory(7);
        }
    };
    f.refresh();
    CHECK(observations == 2 && f.client.event_gaps() == 1 && f.client.job_evidence()->cancelled());
    CHECK(!f.client.request(Operation::Arm, ArmRequest{jid, Peer::utc_base + 50'000'000, 100}, 0));
    Fixture reset;
    reset.connect();
    reset.claim();
    reset.lost(Operation::Load, test_job());
    const auto before = reset.peer.operations.size();
    reset.peer.boot_id = std::string(32, '8');
    reset.peer.owner.reset();
    reset.peer.job.reset();
    reset.peer.state = State::Empty;
    reset.peer.cache.clear();
    reset.connect();
    CHECK(reset.client.phase() == SessionPhase::IdentityChanged && reset.client.uncertain());
    CHECK(reset.peer.operations.size() == before + 1 &&
          reset.peer.operations.back() == Operation::Hello);

    Fixture pending;
    pending.connect();
    pending.claim();
    pending.load();
    CHECK(pending.client.job_evidence()->authoritative);
    CHECK(pending.client.request(Operation::Arm, ArmRequest{jid, Peer::utc_base + 50'000'000, 100},
                                 0));
    CHECK(pending.client.needs_status() && !pending.client.job_evidence()->authoritative &&
          !pending.client.job_evidence()->output_active.has_value());
    pending.client.disconnect();

    Fixture foreign;
    foreign.connect();
    foreign.claim();
    foreign.load();
    foreign.arm();
    foreign.peer.advance(51);
    foreign.refresh();
    CHECK(foreign.client.job_evidence()->completed());
    foreign.peer.owner = std::string(32, '9');
    foreign.peer.expiry = 100'000'000'000;
    foreign.peer.job = test_job(std::string(32, '8'));
    foreign.peer.state = State::Running;
    foreign.peer.start = foreign.peer.mono();
    foreign.peer.active = true;
    foreign.refresh();
    CHECK(!foreign.client.owns() && !foreign.client.safety_fault() &&
          !foreign.client.job_evidence()->completed());
    CHECK(foreign.client.job_evidence()->output_active == false &&
          foreign.client.job_evidence()->device_output_active == true);
}
int main() {
    try {
        successful_jobs();
        ownership_and_lease();
        uncertain_jobs();
        identity_and_events();
        learned_plain_identity();
        stalls_and_faults();
        correlation();
        adversarial_recovery();
        acknowledgment_validation();
        observation_ordering();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    std::cout << "WTP session checks passed: " << checks << '\n';
}
