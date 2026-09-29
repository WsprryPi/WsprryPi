#include "wtp_endpoint/authority.hpp"

#include "WTP-Server/include/wtp_server/inhibited_rf_engine.hpp"
#include "WTP-Server/include/wtp_server/sha256.hpp"
#include "wtp_endpoint/revocation_journal.hpp"

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

using namespace wsprrypico::wtp;
using wsprrypi::WtpPiAuthority;

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

class TestClock final : public Clock {
public:
    ClockSnapshot now{ClockState::Synchronized, 1'000'000'000'000ULL,
                      10'000'000'000ULL, 1000, 0, LeapState::Normal,
                      std::nullopt};
    ClockSnapshot snapshot() const override { return now; }
    void advance(std::uint64_t ns) {
        now.utc_now_ns += ns;
        now.monotonic_now_ns += ns;
        now.sync_age_ns += ns;
    }
};

class TestIdentity final : public IdentitySource {
public:
    std::string new_boot_id() override { return std::string(32, 'a'); }
};

Request request(const char* op, RequestBody body, char id) {
    const std::string payload(1, id);
    const auto digest = sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()));
    return {"WTP/1", std::string(32, '1'), std::string(32, id),
            "local-network", op, digest, std::move(body)};
}

void own(WtpPiAuthority& authority) {
    check(authority.handle(request("HELLO", HelloBody{{"WTP/1"}}, 'a')).ok,
          "HELLO should succeed");
    check(authority.handle(request("CLAIM", ClaimBody{std::string(32, '2'), 10'000}, 'b')).ok,
          "CLAIM should succeed");
}

void test_local_priority_and_noninteractive_abort() {
    TestClock clock;
    TestIdentity ids;
    InhibitedRfEngine engine;
    JobService service(clock, engine, ids);
    WtpPiAuthority authority(service, true);
    check(authority.handle(request("HELLO", HelloBody{{"WTP/1"}}, 'a')).ok,
          "inspection should work while locally enabled");
    check(authority.handle(request("CLAIM", ClaimBody{std::string(32, '2'), 10'000}, 'b'))
              .error == ErrorCode::Busy,
          "local enable must reserve idle output");
    authority.disable_local();
    check(authority.begin_test_tone(), "Test Tone can start while schedule disabled");
    check(authority.handle(request("CLAIM", ClaimBody{std::string(32, '2'), 10'000}, 'c'))
              .error == ErrorCode::Busy,
          "Test Tone must block remote claim");
    authority.end_local_work(true);
    const auto claim = request("CLAIM", ClaimBody{std::string(32, '2'), 10'000}, 'd');
    check(authority.handle(claim).ok,
          "idle disabled output must admit claim");
    const auto result = authority.enable_noninteractive();
    check(result == WtpPiAuthority::EnableResult::Enabled,
          "noninteractive enable must abort remote ownership");
    const auto snapshot = authority.snapshot();
    check(snapshot.effective_local_enable && !snapshot.remote.owner_id &&
              snapshot.revocation_generation == 1,
          "local enable must take precedence and record revocation");
    check(authority.handle(claim).ok,
          "request replay must remain stable after local takeover");
    check(!authority.snapshot().remote.owner_id,
          "replayed success must not reacquire transmitter ownership");
}

void test_interactive_finish_and_end_now() {
    TestClock clock;
    TestIdentity ids;
    InhibitedRfEngine engine;
    JobService service(clock, engine, ids);
    WtpPiAuthority authority(service);
    own(authority);
    check(authority.enable_interactive(WtpPiAuthority::EnableChoice::EndNow, false) ==
              WtpPiAuthority::EnableResult::ConfirmationRequired,
          "unconfirmed browser enable must preserve remote ownership");
    check(!authority.snapshot().requested_local_enable,
          "unconfirmed browser enable must not change local setting");
    bool failed = false;
    try {
        authority.enable_interactive(WtpPiAuthority::EnableChoice::EndNow, true,
            [] { throw std::runtime_error("disk full"); });
    } catch (const std::runtime_error&) { failed = true; }
    check(failed && authority.snapshot().remote.owner_id &&
              !authority.snapshot().requested_local_enable,
          "failed persistence must preserve remote ownership and local setting");
    Job job{std::string(32, '3'), "rf-events/1", "tone", 1'000'000'000ULL,
            {{0, 1'000'000'000ULL, true, 14'097'100'000'000'000ULL}}, false};
    check(authority.handle(request("LOAD", job, 'c')).ok, "LOAD should succeed");
    check(authority.handle(request("ARM", ArmBody{job.job_id,
                clock.now.utc_now_ns + 1'000'000'000ULL, 1000}, 'd')).ok,
          "ARM should succeed");
    check(authority.enable_interactive(WtpPiAuthority::EnableChoice::FinishCurrent, true) ==
              WtpPiAuthority::EnableResult::PendingJob,
          "finish-current should wait for active job");
    auto replacement = job;
    replacement.job_id = std::string(32, '4');
    check(authority.handle(request("LOAD", replacement, 'e')).error == ErrorCode::Busy,
          "takeover must block replacement remote work");
    clock.advance(1'000'000'000ULL);
    authority.poll();
    check(!authority.snapshot().effective_local_enable,
          "local output must wait for job completion");
    clock.advance(1'000'000'000ULL);
    authority.poll();
    const auto complete = authority.snapshot();
    check(complete.effective_local_enable && !complete.remote.owner_id &&
              complete.revocation_generation == 1,
          "finish-current should release remote owner after output-off");

    authority.disable_local();
    check(!authority.handle(request("CLAIM", ClaimBody{std::string(32, '4'), 10'000}, 'f')).ok,
          "pre-takeover session must not claim after local Disable");
    auto hello = request("HELLO", HelloBody{{"WTP/1"}}, '7');
    hello.session_id = std::string(32, '7');
    check(authority.handle(hello).ok, "a new session can inspect after takeover");
    auto claim = request("CLAIM", ClaimBody{std::string(32, '4'), 10'000}, '8');
    claim.session_id = hello.session_id;
    check(authority.handle(claim).ok, "a freshly assigned session can claim after local Disable");
    check(authority.enable_interactive(WtpPiAuthority::EnableChoice::EndNow, true) ==
              WtpPiAuthority::EnableResult::Enabled,
          "end-now should immediately revoke a remote claim");
    authority.disable_local();
    check(authority.enable_noninteractive() == WtpPiAuthority::EnableResult::Enabled &&
              authority.snapshot().revocation_generation == 3,
          "enabling without an active claim must still revoke saved assignments");
}

void test_revocation_failure_still_aborts_remote_job() {
    TestClock clock;
    TestIdentity ids;
    InhibitedRfEngine engine;
    JobService service(clock, engine, ids);
    wsprrypi::WtpRevocationJournal journal(
        "/nonexistent-wtp-pi-journal-directory/revocation",
        std::string(32, '5'));
    WtpPiAuthority authority(service, false, &journal);
    own(authority);
    check(authority.enable_noninteractive() ==
              WtpPiAuthority::EnableResult::RevocationUnavailable,
          "journal failure must be reported after trusted local abort");
    const auto state = authority.snapshot();
    check(!state.remote.owner_id && !state.effective_local_enable &&
              state.remote_admission_closed && state.revocation_unavailable,
          "journal failure must not preserve remote ownership or admit RF");
    check(authority.handle(request("CLAIM", ClaimBody{std::string(32, '2'), 10'000}, '8'))
                  .error == ErrorCode::Busy,
          "journal failure must not reopen remote admission");
}

void test_local_cleanup_and_configuration_hold_admission() {
    TestClock clock;
    TestIdentity ids;
    InhibitedRfEngine engine;
    JobService service(clock, engine, ids);
    WtpPiAuthority authority(service, true);
    check(authority.handle(request("HELLO", HelloBody{{"WTP/1"}}, 'a')).ok,
          "HELLO should succeed");
    check(authority.begin_scheduled_work(), "enabled local work should reserve output");
    authority.disable_local();
    check(authority.handle(request("CLAIM", ClaimBody{std::string(32, '2'), 10'000}, 'b'))
              .error == ErrorCode::Busy,
          "disabling local control must wait for active local cleanup");
    authority.end_scheduled_work(true);
    check(authority.begin_configuration(), "idle local configuration should reserve output");
    check(!authority.begin_test_tone(), "Test Tone must not race local configuration");
    check(authority.handle(request("CLAIM", ClaimBody{std::string(32, '2'), 10'000}, 'c'))
              .error == ErrorCode::Busy,
          "remote claim must not race backend configuration");
    authority.end_configuration();
    check(authority.begin_test_tone(), "idle Test Tone should reserve output");
    authority.end_scheduled_work(true);
    check(authority.handle(request("CLAIM", ClaimBody{std::string(32, '2'), 10'000}, 'd'))
              .error == ErrorCode::Busy,
          "stopping the old scheduler must not release a Test Tone reservation");
    authority.end_local_work(false);
    check(!authority.begin_configuration(), "unknown local output must block configuration");
    check(authority.handle(request("CLAIM", ClaimBody{std::string(32, '2'), 10'000}, 'e'))
              .error == ErrorCode::Busy,
          "unconfirmed local cleanup must block remote claim");
    check(authority.enable_noninteractive() == WtpPiAuthority::EnableResult::OutputUnknown &&
              !authority.snapshot().effective_local_enable,
          "local Enable must remain ineffective after unconfirmed cleanup");
}
} // namespace

int main() {
    test_local_priority_and_noninteractive_abort();
    test_interactive_finish_and_end_now();
    test_revocation_failure_still_aborts_remote_job();
    test_local_cleanup_and_configuration_hold_admission();
}
