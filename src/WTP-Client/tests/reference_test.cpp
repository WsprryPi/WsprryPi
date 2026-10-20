// SPDX-License-Identifier: MIT
#include "reference_bridge.hpp"
#include "wtp/session.hpp"
#include <iostream>
#include <stdexcept>
using namespace wsprrypi::wtp;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " + #x); \
    } while (false)
struct Stream : ByteStream {
    std::unique_ptr<ReferenceEndpoint> peer{reference_endpoint()};
    bool drop_next_reply{};
    void close() noexcept override { peer->disconnect(); }
    IoResult write(std::span<const std::uint8_t> bytes) override {
        if (peer->closed())
            return {IoState::Closed};
        const auto count = peer->receive(bytes);
        return {count ? IoState::Progress : IoState::WouldBlock, count};
    }
    IoResult read(std::span<std::uint8_t> bytes) override {
        if (peer->closed())
            return {IoState::Closed};
        const auto count = peer->read(bytes);
        if (count && drop_next_reply) {
            drop_next_reply = false;
            close();
            return {IoState::Closed};
        }
        return {count ? IoState::Progress : IoState::WouldBlock, count};
    }
};
int main() {
    try {
        Stream stream;
        Session session{{std::string(32, '1'), std::string(32, '2'), std::string(32, '4')}};
        std::uint64_t now{};
        auto pump = [&] {
            for (unsigned i = 0; i < 10000; ++i) {
                session.poll(now);
                if (session.phase() == SessionPhase::Ready && !session.busy() &&
                    !session.needs_status())
                    return;
                if (session.phase() == SessionPhase::Disconnected)
                    return;
                if (session.phase() == SessionPhase::Fault ||
                    session.phase() == SessionPhase::IdentityChanged)
                    throw std::runtime_error(session.diagnostic());
            }
            throw std::runtime_error("Reference peer stalled");
        };
        auto connect = [&] {
            stream.peer->connect();
            CHECK(session.connect(stream, now));
            pump();
            CHECK(session.phase() == SessionPhase::Ready);
        };
        auto request = [&](Operation op, RequestBody body) {
            CHECK(session.request(op, std::move(body), now));
            pump();
            auto result = session.take_result();
            CHECK(result);
            return result->kind;
        };
        connect();
        CHECK(request(Operation::Claim, LeaseRequest{std::string(32, '2'), 5000}) ==
              ResultKind::Acknowledged);
        Job job{
            std::string(32, '3'), Mode::Tone, 2'000'000, {{0, 2'000'000, true, 1000000000}}, {}};
        stream.drop_next_reply = true;
        CHECK(request(Operation::Load, job) == ResultKind::Unknown);
        connect();
        CHECK(session.uncertain());
        CHECK(session.retry_uncertain(now));
        pump();
        CHECK(session.take_result()->kind == ResultKind::Acknowledged);
        CHECK(request(Operation::Arm, ArmRequest{job.job_id, 1'000'050'000'000ULL, 100}) ==
              ResultKind::Acknowledged);
        session.disconnect();
        stream.peer->advance(now = 50);
        CHECK(stream.peer->output_active());
        stream.peer->advance(now = 52);
        CHECK(!stream.peer->output_active() && stream.peer->executions() == 1);
        connect();
        CHECK(session.job_evidence()->completed());
        CHECK(request(Operation::Release, Empty{}) == ResultKind::Acknowledged);
        CHECK(request(Operation::Claim, LeaseRequest{std::string(32, '2'), 5000}) ==
              ResultKind::Acknowledged);
        job.job_id = std::string(32, '6');
        CHECK(request(Operation::Load, job) == ResultKind::Acknowledged);
        stream.drop_next_reply = true;
        CHECK(request(Operation::Abort, AbortRequest{job.job_id}) == ResultKind::Unknown);
        connect();
        CHECK(session.take_result()->kind == ResultKind::Reconciled);
        CHECK(session.job_evidence()->cancelled() && stream.peer->executions() == 1);
        CHECK(request(Operation::Release, Empty{}) == ResultKind::Acknowledged);
        session.disconnect();
        std::cout << "Pinned Pico endpoint interoperability passed: two jobs, one local execution, "
                     "exact LOAD replay, lost ABORT reconciliation\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
