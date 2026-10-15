#include "scheduling.hpp"
#include "scheduling_internal.hpp"
#include "config_handler.hpp"
#include "arg_parser.hpp"
#include "test_tone_response.hpp"
#include "transmitter_runtime_bridge.hpp"
#include "web_server_config_http.hpp"
#include "web_socket.hpp"
#include "wtp_pi_control.hpp"
#include <cassert>
#include <condition_variable>
#include <future>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <sys/socket.h>
#ifdef __APPLE__
#include <fcntl.h>
#endif
#include <unistd.h>
using namespace std::chrono_literals;

namespace {
struct Gate {
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false, released = false;
    void arrive() {
        std::unique_lock lock(mutex);
        entered = true; cv.notify_all();
        assert(cv.wait_for(lock, 3s, [&] { return released; }));
    }
    void wait() {
        std::unique_lock lock(mutex);
        assert(cv.wait_for(lock, 3s, [&] { return entered; }));
    }
    void release() { std::lock_guard lock(mutex); released = true; cv.notify_all(); }
};
template<class T> T done(std::future<T> &future) {
    assert(future.wait_for(3s) == std::future_status::ready);
    return future.get();
}
TestToneRequest request() {
    TestToneRequest r; r.source = TestToneRequestSource::CustomRf;
    r.frequency_hz = 14097100; return r;
}
void setup(ModeType mode = ModeType::WSPR) {
    set_test_tone_lifecycle_hook_for_test({});
    (void)stop_transmission_by_user_request(false);
    clear_wtp_pi_control_hooks();
    init_default_config();
    reset_managed_reload_runtime_for_test();
    exiting_wspr.store(false);
    set_scheduler_execution_suppressed_for_test(true);
    set_band_gpio_selector_for_test(true, false);
    config.use_ini = true;
    config.ini_filename = "/tmp/test-tone-lifecycle.ini";
    config.mode = mode; config.transmit = false;
    config.callsign = "AA0NT"; config.grid_square = "EM18";
    config.frequencies = "20m"; config.power_dbm = 20;
    config.qrss.message = config.fskcw.message = config.dfcw.message = "E";
    config.qrss.frequency_hz = config.fskcw.mark_frequency_hz = config.dfcw.dot_frequency_hz = 14097100;
    config.fskcw.mark_frequency_hz = config.dfcw.dash_frequency_hz = 14097105;
    config.fskcw.space_frequency_hz = 14097100;
    config.qrss.dot_seconds = config.fskcw.dot_seconds = config.dfcw.dot_seconds = 3;
    config.modulation_fsk_offset_hz = 5;
    config.transmit_backend = TransmitBackendKind::SIMULATED;
    config.simulated_backend_override = true;
    config.use_system_clock_frequency_estimate = false;
    config.gpio_use_system_clock_frequency_estimate = false;
    assert(set_frequencies(config));
    std::ofstream(config.ini_filename).close();
    config_to_json(); iniFile.set_filename(config.ini_filename); json_to_ini();
    ini_reload_pending.store(false);
}
void ordered_transactions() {
    setup();
    Gate first;
    set_test_tone_lifecycle_hook_for_test([&](std::string_view point) {
        if (point == "start_locked") first.arrive();
    });
    auto a = std::async(std::launch::async, [] { return start_test_tone(request()); });
    first.wait();
    std::promise<void> attempted; auto attempting = attempted.get_future();
    auto b = std::async(std::launch::async, [&] {
        attempted.set_value(); return start_test_tone(request());
    });
    done(attempting); first.release();
    const auto winner = done(a); const auto rejected = done(b);
    assert(winner.started && rejected.already_active && !rejected.started);
    assert(winner.lifecycle.generation == rejected.lifecycle.generation);
    assert(current_transmission_request_for_test().actual_rf_frequency_hz == 14097100);
    set_test_tone_lifecycle_hook_for_test({});
    assert(end_test_tone().stopped); assert(!end_test_tone().tone_was_active);

    // Start owns the transaction before End; End must consume that owner.
    setup(); Gate start;
    set_test_tone_lifecycle_hook_for_test([&](std::string_view p) { if (p == "start_locked") start.arrive(); });
    auto starting = std::async(std::launch::async, [] { return start_test_tone(request()); });
    start.wait(); auto ending = std::async(std::launch::async, [] { return end_test_tone(); });
    start.release(); assert(done(starting).started); assert(done(ending).stopped);
    assert(!current_test_tone_status().owned);
    set_test_tone_lifecycle_hook_for_test({});

    // End owns an empty transaction before a later explicit Start.
    setup(); Gate end;
    set_test_tone_lifecycle_hook_for_test([&](std::string_view p) { if (p == "end_locked") end.arrive(); });
    auto empty_end = std::async(std::launch::async, [] { return end_test_tone(); });
    end.wait(); auto successor = std::async(std::launch::async, [] { return start_test_tone(request()); });
    end.release(); assert(!done(empty_end).tone_was_active); assert(done(successor).started);
    assert(current_test_tone_status().owned);
    set_test_tone_lifecycle_hook_for_test({});
}
void stop_and_stale_owners() {
    setup();
    const auto pending_launch = non_wspr_schedule_generation.load();
    const auto old = start_test_tone(request()); assert(old.started);
    assert(!launch_scheduled_non_wspr_if_current(pending_launch));
    assert(stop_transmission_by_user_request(false).transmit_disabled);
    const auto newer = start_test_tone(request()); assert(newer.started);
    assert(newer.lifecycle.generation > old.lifecycle.generation);
    record_test_tone_event(old.lifecycle.generation, WsprTransmissionCallbackEvent::FAILED, "stale failure");
    const auto stale = end_test_tone(old.lifecycle.generation);
    assert(!stale.stopped && current_test_tone_status().owned);
    assert(current_test_tone_status().generation == newer.lifecycle.generation);

    // Force HTTP Stop after Start is admitted but before it commits.
    setup(); Gate start;
    set_test_tone_lifecycle_hook_for_test([&](std::string_view p) { if (p == "start_locked") start.arrive(); });
    auto starting = std::async(std::launch::async, [] { return start_test_tone(request()); });
    start.wait(); auto stopping = std::async(std::launch::async, [] {
        return web_server_routes::stop_transmission("{\"command\":\"stop\"}");
    });
    start.release(); assert(done(starting).started); assert(done(stopping).status == 200);
    assert(!config.transmit && !current_test_tone_status().owned && config.mode == ModeType::WSPR);
    set_test_tone_lifecycle_hook_for_test({});

    // Stop ordered after End restoration fences the restored owner too.
    setup(); assert(start_test_tone(request()).started); Gate restore;
    set_test_tone_lifecycle_hook_for_test([&](std::string_view p) { if (p == "before_restore") restore.arrive(); });
    auto ending = std::async(std::launch::async, [] { return end_test_tone(); });
    restore.wait(); auto stopping_end = std::async(std::launch::async, [] { return stop_transmission_by_user_request(false); });
    restore.release(); assert(done(ending).stopped); assert(done(stopping_end).transmit_disabled);
    assert(!config.transmit && !current_test_tone_status().owned);
    set_test_tone_lifecycle_hook_for_test({});
}
void restoration_failure_and_stop_fence() {
    setup();
    config.use_ini = false; config.mode = ModeType::TONE; config.transmit = true;
    assert(set_direct_tone_startup_request("14097123"));
    set_startup_quiesce_invoker_for_test([] { return wsprrypi::StartupQuiesceResult{false, "admission fixture"}; });
    assert(!run_startup_quiesce_gate_for_test(config));
    copy_runtime_config(config, config);
    assert(start_test_tone(request()).started);
    reset_startup_quiesce_for_test();
    set_direct_tone_start_invoker_for_test([] { throw std::runtime_error("restoration fixture"); });
    const auto failed = end_test_tone();
    assert(failed.stopped && failed.tone_was_active && failed.restoration_failed);
    assert(failed.message.find("restoration fixture") != std::string::npos);
    assert(failed.lifecycle.restoration == "failed");
    reset_direct_tone_start_invoker_for_test();
    assert(stop_transmission_by_user_request(false).transmit_disabled);
    assert(!end_test_tone().tone_was_active);
    clear_direct_tone_startup_request();

    setup(); assert(start_test_tone(request()).started);
    Gate end;
    set_test_tone_lifecycle_hook_for_test([&](std::string_view p) { if (p == "end_locked") end.arrive(); });
    auto a = std::async(std::launch::async, [] { return end_test_tone(); });
    end.wait(); auto b = std::async(std::launch::async, [] { return end_test_tone(); });
    end.release(); assert(done(a).tone_was_active); assert(!done(b).tone_was_active);
    set_test_tone_lifecycle_hook_for_test({});
}
void events_reload_and_failure() {
    for (const auto mode : {ModeType::WSPR, ModeType::QRSS, ModeType::FSKCW, ModeType::DFCW}) {
        setup(mode); const auto started = start_test_tone(request()); assert(started.started);
        assert(started.lifecycle.state == "starting");
        assert(set_config(true)); assert(ini_reload_pending.load());
        assert(config.mode == ModeType::TONE);
        record_test_tone_event(started.lifecycle.generation, WsprTransmissionCallbackEvent::STARTING, "");
        assert(current_test_tone_status().state == "active");
        record_test_tone_event(started.lifecycle.generation, WsprTransmissionCallbackEvent::COMPLETE, "finite completion");
        record_test_tone_event(started.lifecycle.generation, WsprTransmissionCallbackEvent::STARTING, "delayed");
        assert(current_test_tone_status().state == "completed");
        reconcile_test_tone_runtime();
        assert(!current_test_tone_status().owned && current_test_tone_status().state == "completed");
        assert(config.mode == mode && !ini_reload_pending.load());
        const auto watchdog = end_test_tone(started.lifecycle.generation);
        assert(watchdog.stopped && !watchdog.restoration_failed && !watchdog.tone_was_active);
        assert(watchdog.lifecycle.state == "completed");
    }
    for (const auto event : {WsprTransmissionCallbackEvent::COMPLETE, WsprTransmissionCallbackEvent::FAILED}) {
        setup();
        set_test_tone_lifecycle_hook_for_test([event](std::string_view p) {
            if (p == "before_start_async") record_test_tone_event(current_test_tone_status().generation,
                event, "terminal event before acknowledgment");
        });
        auto early = start_test_tone(request());
        assert(early.started && early.lifecycle.state ==
            (event == WsprTransmissionCallbackEvent::COMPLETE ? "completed" : "failed"));
        set_test_tone_lifecycle_hook_for_test({});
        reconcile_test_tone_runtime(); assert(!current_test_tone_status().owned);
    }

    setup(); auto started = start_test_tone(request()); assert(started.started);
    patch_all_from_web({{"CW", {{"Message", "EE"}}}});
    assert(config.mode == ModeType::TONE && ini_reload_pending.load());
    assert(end_test_tone().stopped); assert(config.mode == ModeType::WSPR);

    setup(); started = start_test_tone(request());
    record_test_tone_event(started.lifecycle.generation, WsprTransmissionCallbackEvent::FAILED, "startup failed");
    reconcile_test_tone_runtime();
    assert(!current_test_tone_status().owned && !config.transmit);
    assert(current_test_tone_status().state == "failed");
    assert(current_test_tone_status().message.find("startup failed") != std::string::npos);

    setup(); WtpPiControlHooks hooks; int releases = 0;
    hooks.begin_test_tone = [] { return false; }; set_wtp_pi_control_hooks(hooks);
    assert(!start_test_tone(request()).started && !web_test_tone.load());
    hooks.begin_test_tone = [] { return true; };
    hooks.end_local = [&](bool) { ++releases; }; set_wtp_pi_control_hooks(hooks);
    set_test_tone_lifecycle_hook_for_test([](std::string_view p) {
        if (p == "before_start_async") throw std::runtime_error("partial start");
    });
    // Fail after real planning and commitment, before concrete transmitter
    // launch. The reusable transmitter does not admit simulated TONE jobs.
    const auto failed = start_test_tone(request());
    assert(!failed.started && failed.message == "partial start" && !web_test_tone.load() && releases == 1);
    set_test_tone_lifecycle_hook_for_test({}); clear_wtp_pi_control_hooks();
    set_scheduler_execution_suppressed_for_test(true);
}
}

class TestToneLifecycleTest {
    static void bounded_start(WebSocketServer &server, const char *id, int duration = 60000) {
        server.handleMessage(nlohmann::json{{"command", "bounded_tone"}, {"request_id", id},
            {"duration_ms", duration}, {"frequency_source", "custom_rf"}, {"frequency_hz", 14097100}}.dump());
    }
public:
    static void run() {
        // A peer that does not read must not hold the clients mutex across
        // cleanup. Force a partial/non-writable send without timing sleeps.
        {
            WebSocketServer slow;
            int sockets[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
            int capacity = 4096;
            assert(setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF, &capacity, sizeof(capacity)) == 0);
#ifdef __APPLE__
            // Darwin needs descriptor nonblocking mode for saturated sends.
            // Linux retains the blocking descriptor to exercise MSG_DONTWAIT.
            const int flags = fcntl(sockets[0], F_GETFL, 0);
            assert(flags >= 0);
            assert(fcntl(sockets[0], F_SETFL, flags | O_NONBLOCK) == 0);
#endif
            WebSocketServer::ClientAuthorizationContext authorization{
                "127.0.0.1", "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n"};
            assert(slow.clientAuthorizationCurrent(authorization));
            slow.client_sockets_.push_back(sockets[0]);
            slow.client_authorizations_.emplace(sockets[0], authorization);
            auto broadcast = std::async(std::launch::async, [&] { slow.sendAllClients(std::string(1048576, 'x')); });
            done(broadcast);
            assert(slow.clients_mutex_.try_lock()); slow.clients_mutex_.unlock();
            slow.client_sockets_.clear(); slow.client_authorizations_.clear();
            close(sockets[0]); close(sockets[1]);
        }
        setup();
        {
            WebSocketServer ordered;
            assert(ordered.start(39526, 0, true, WebSocketLoopbackFamily::IPv4));
            Gate reply;
            std::vector<nlohmann::json> replies;
            ordered.reply_ready_for_test_ = [&](const std::string &payload) {
                replies.push_back(nlohmann::json::parse(payload));
                if (replies.back().value("command", "") == "tone_start") reply.arrive();
            };
            auto start = std::async(std::launch::async, [&] {
                ordered.handleMessage("{\"command\":\"tone_start\",\"frequency_hz\":14097100,\"client_request_id\":\"start-1\"}");
            });
            reply.wait();
            // An observer delayed at delivery cannot hold scheduler ownership.
            auto stop = std::async(std::launch::async, [] {
                return web_server_routes::stop_transmission("{\"command\":\"stop\"}");
            });
            assert(done(stop).status == 200);
            auto end = std::async(std::launch::async, [&] {
                ordered.handleMessage("{\"command\":\"tone_end\",\"client_request_id\":\"end-1\"}");
            });
            reply.release(); done(start); done(end);
            assert(replies.size() == 2 && replies[0]["started"] == true);
            assert(replies[0]["client_request_id"] == "start-1" && replies[1]["client_request_id"] == "end-1");
            assert(replies[1]["test_tone"]["revision"] > replies[0]["test_tone"]["revision"]);
            assert(replies[1]["test_tone"]["owned"] == false);
            ordered.stop();
        }
        setup(); WebSocketServer server;
        assert(server.start(39526, 0, true, WebSocketLoopbackFamily::IPv4));
        Gate expired;
        server.bounded_expiry_ready_for_test_ = [&] { expired.arrive(); };
        bounded_start(server, "expired", 1); expired.wait();
        // Reproduces the old deadlock: an expired worker is held just before
        // the command mutex; End completes before that worker is released.
        auto ending = std::async(std::launch::async, [&] { server.handleMessage("{\"command\":\"tone_end\"}"); });
        done(ending); assert(!current_test_tone_status().owned);
        server.handleMessage("{\"command\":\"tone_start\",\"frequency_hz\":14097100}");
        const auto successor = current_test_tone_status(); assert(successor.owned);
        expired.release();
        server.stop(); // Worker must drain without ending the unbounded successor.
        assert(current_test_tone_status().owned && current_test_tone_status().generation == successor.generation);
        server.stop();

        setup(); WebSocketServer rollover;
        assert(rollover.start(39526, 0, true, WebSocketLoopbackFamily::IPv4));
        bounded_start(rollover, "one"); auto first = current_test_tone_status(); assert(first.owned);
        rollover.handleMessage("{\"command\":\"stop\",\"persist_transmit\":false}");
        bounded_start(rollover, "two"); assert(current_test_tone_status().generation > first.generation);
        assert(!end_test_tone(first.generation).stopped);
        rollover.stop(); assert(!current_test_tone_status().owned && !config.transmit);
        assert(rollover.start(39526, 0, true, WebSocketLoopbackFamily::IPv4));
        bounded_start(rollover, "restarted"); assert(current_test_tone_status().owned);
        rollover.stop(); assert(!current_test_tone_status().owned);

        setup(); WebSocketServer failing;
        assert(failing.start(39526, 0, true, WebSocketLoopbackFamily::IPv4));
        failing.fail_bounded_thread_creation_for_test_ = true;
        bounded_start(failing, "no-thread");
        assert(!current_test_tone_status().owned && !web_test_tone.load() && !config.transmit);
        failing.stop();

        setup(); {
            WebSocketServer destruction;
            assert(destruction.start(39526, 0, true, WebSocketLoopbackFamily::IPv4));
            bounded_start(destruction, "destruction");
        }
        assert(!current_test_tone_status().owned);
    }
};
int main() {
    ordered_transactions(); stop_and_stale_owners(); restoration_failure_and_stop_fence(); events_reload_and_failure();
    TestToneLifecycleTest::run();
    set_test_tone_lifecycle_hook_for_test({});
    stop_runtime_components_for_test();
    std::cout << "Test Tone lifecycle interleavings passed\n";
}
