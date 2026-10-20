// SPDX-License-Identifier: MIT
// Hardware-free adapter to the separately compiled, pinned reference server.
#include "reference_bridge.hpp"
#include "wtp/endpoint.hpp"
#include <algorithm>

namespace {
namespace pico = wsprrypico::wtp;
struct Clock : pico::Clock {
    std::uint64_t now_ms{};
    pico::ClockSnapshot snapshot() const override {
        return {pico::ClockState::Synchronized,
                1'000'000'000'000ULL + now_ms * 1'000'000,
                now_ms * 1'000'000,
                1,
                0,
                pico::LeapState::Normal,
                {}};
    }
};
struct Identities : pico::IdentitySource {
    std::string new_boot_id() override { return std::string(32, '5'); }
};
struct Engine : pico::RfEngine {
    unsigned count{};
    std::uint64_t start{}, duration{};
    pico::EngineState state{pico::EngineState::Idle};
    bool active{};
    pico::PrepareResult prepare(const pico::Job &) override { return {true, {}}; }
    bool schedules_locally() const override { return true; }
    bool schedule(const pico::Job &job, std::uint64_t time,
                  const pico::LocalStartConditions &) override {
        start = time;
        duration = job.total_duration_ns;
        state = pico::EngineState::Armed;
        return true;
    }
    bool begin(const pico::Job &, std::uint64_t) override { return false; }
    pico::EngineReport poll(std::uint64_t now) override {
        if (state == pico::EngineState::Armed && now > start)
            state = pico::EngineState::Missed;
        if (state == pico::EngineState::Armed && now == start) {
            state = pico::EngineState::Running;
            active = true;
            ++count;
        }
        if (state == pico::EngineState::Running && now >= start + duration) {
            state = pico::EngineState::Complete;
            active = false;
        }
        return {state, active};
    }
    bool disable(std::uint64_t) override {
        active = false;
        state = pico::EngineState::Idle;
        return true;
    }
    bool output_active() const override { return active; }
};
pico::ServiceConfig config() {
    pico::ServiceConfig c;
    c.capability_engine = "interop-software-engine";
    c.minimum_arm_lead_ns = 1'000'000;
    c.maximum_arm_ahead_ns = 10'000'000'000;
    c.supported_modes = {"tone"};
    return c;
}
class Endpoint final : public ReferenceEndpoint {
    Clock clock_;
    Identities ids_;
    Engine engine_;
    pico::JobService service_{clock_, engine_, ids_, config()};
    pico::Endpoint endpoint_{service_, std::string(32, '4'), "interop-test"};

  public:
    void connect() override { endpoint_.connect("software-test-principal"); }
    void disconnect() override { endpoint_.disconnect(); }
    void advance(std::uint64_t now) override {
        clock_.now_ms = now;
        endpoint_.poll(now);
    }
    bool closed() const override { return endpoint_.closed(); }
    std::size_t receive(std::span<const std::uint8_t> bytes) override {
        return endpoint_.receive(bytes, clock_.now_ms);
    }
    std::size_t read(std::span<std::uint8_t> bytes) override {
        const auto pending = endpoint_.output();
        const auto count = std::min({bytes.size(), pending.size(), std::size_t{7}});
        std::copy_n(pending.begin(), count, bytes.begin());
        endpoint_.consume_output(count, clock_.now_ms);
        return count;
    }
    unsigned executions() const override { return engine_.count; }
    bool output_active() const override { return engine_.active; }
};
} // namespace
std::unique_ptr<ReferenceEndpoint> reference_endpoint() { return std::make_unique<Endpoint>(); }
