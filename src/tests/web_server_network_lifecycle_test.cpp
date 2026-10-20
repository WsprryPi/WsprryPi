#include "../privileged_network_runtime.hpp"
#include "../web_server.hpp"
#include "../httplib.hpp"
#include "../json.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

class WebServerNetworkLifecycleTest {
public:
    static int run() {
        constexpr int port = 39530;
        std::atomic<int> phase{0};
        WebServer server;
        server.network_snapshot_provider_ = [&] {
            SupportRequestGuardSnapshot snapshot{true, "wsprrypi", {}, {}};
            switch (phase.load(std::memory_order_acquire)) {
            case 1:
                snapshot.networks = {
                    {"192.168.50.10", "255.255.255.0"}};
                break;
            case 2:
                snapshot.networks = {
                    {"10.20.30.2", "255.255.255.0"}};
                break;
            case 3:
                snapshot.discovery_succeeded = false;
                break;
            default:
                break;
            }
            return snapshot;
        };

        initialize_privileged_network_runtime("enforced");
        server.start(port);
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(3);
        while (!server.isListening() &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        assert(server.isListening());

        const auto request = [&](const std::string &represented_client,
                                 const std::string &host = "wsprrypi") {
            httplib::Client client("127.0.0.1", port);
            client.set_connection_timeout(1, 0);
            client.set_read_timeout(1, 0);
            httplib::Headers headers{{"Host", host}};
            if (!represented_client.empty()) {
                headers.emplace(
                    std::string(WSPRRYPI_TRUSTED_PROXY_IDENTITY_HEADER),
                    represented_client);
            }
            return client.Get("/api/support-bundles/missing", headers);
        };
        const auto require_status = [&](const std::string &client, int status) {
            const auto response = request(client);
            assert(response && response->status == status);
            assert(!response->has_header("Access-Control-Allow-Origin"));
        };

        // Production Pico routes: reads are snapshots and unselected recovery cannot open USB.
        {
            httplib::Client client("127.0.0.1", port);
            httplib::Headers headers{{"Host", "wsprrypi"}};
            auto observation = client.Get("/api/wtp", headers);
            assert(observation && observation->status == 200);
            const auto snapshot = nlohmann::json::parse(observation->body);
            assert(snapshot.at("selected") == false);
            assert(snapshot.at("firmware_maintenance") == false);
            assert(observation->get_header_value("Cache-Control") == "no-store");
            for (const auto &body : {"{}", "[]", "{\"operation\":\"resume\"}", "{\"operation\":\"reconcile\",\"operation\":\"reconcile\"}"}) {
                auto response = client.Post("/api/wtp/recover", headers, body, "application/json");
                assert(response && response->status == 400);
            }
            auto response = client.Post("/api/wtp/recover", headers, "{\"operation\":\"reconcile\"}", "application/json");
            assert(response && response->status == 409);
            response = client.Post("/api/wtp/recover", headers, "{\"operation\":\"reconcile\"}", "application/jsonx");
            assert(response && response->status == 400);
        }

        // The same listener remains live while current authorization changes.
        require_status("192.168.50.42", 403);
        require_status("", 404);  // Legitimate loopback operation.
        phase.store(1, std::memory_order_release);
        require_status("192.168.50.42", 404);
        phase.store(0, std::memory_order_release);
        require_status("192.168.50.42", 403);
        phase.store(2, std::memory_order_release);
        require_status("192.168.50.42", 403);
        require_status("10.20.30.42", 404);
        phase.store(3, std::memory_order_release);
        require_status("10.20.30.42", 403);
        assert(server.isListening());

        // Exercise request/snapshot concurrency; every result must be either
        // the authorization denial or the underlying not-found response.
        std::atomic<bool> valid{true};
        std::vector<std::thread> clients;
        for (int index = 0; index < 4; ++index) {
            clients.emplace_back([&, index] {
                for (int iteration = 0; iteration < 20; ++iteration) {
                    const auto response = request(
                        index % 2 == 0 ? "192.168.50.42" : "10.20.30.42");
                    if (!response ||
                        (response->status != 403 && response->status != 404)) {
                        valid.store(false, std::memory_order_release);
                    }
                }
            });
        }
        for (int iteration = 0; iteration < 80; ++iteration)
            phase.store(iteration % 3, std::memory_order_release);
        for (auto &client : clients) client.join();
        assert(valid.load(std::memory_order_acquire));
        assert(server.isListening());

        set_privileged_network_runtime_mode(
            PrivilegedNetworkMode::insecure_disabled);
        phase.store(0, std::memory_order_release);
        require_status("203.0.113.9", 404);
        const auto bad_host = request("203.0.113.9", "evil.example");
        assert(bad_host && bad_host->status == 403);
        set_privileged_network_runtime_mode(PrivilegedNetworkMode::enforced);

        const auto stop_started = std::chrono::steady_clock::now();
        server.stop();
        assert(std::chrono::steady_clock::now() - stop_started <
               std::chrono::seconds(2));
        assert(!server.isListening());
        std::cout << "web_server_network_lifecycle_test: PASS\n";
        return 0;
    }
};

int main() {
    return WebServerNetworkLifecycleTest::run();
}
