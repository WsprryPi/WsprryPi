#include "wtp_server/inhibited_rf_engine.hpp"
#include "wtp_server/job_service.hpp"
#include "wtp_server/sha256.hpp"

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

using namespace wsprrypico::wtp;

namespace {
class TestClock final : public Clock {
public:
    ClockSnapshot now{ClockState::Synchronized, 1'000'000'000'000ULL,
                      10'000'000'000ULL, 1000, 0, LeapState::Normal,
                      std::nullopt};
    ClockSnapshot snapshot() const override { return now; }
};

class TestIdentity final : public IdentitySource {
public:
    std::string new_boot_id() override { return std::string(32, 'a'); }
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Request request(const char* op, RequestBody body, char id) {
    const std::string payload(1, id);
    const auto digest = sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()));
    return {"WTP/1", std::string(32, '1'), std::string(32, id),
            "local-network", op, digest, std::move(body)};
}
} // namespace

int main() {
    TestClock clock;
    TestIdentity ids;
    InhibitedRfEngine engine;
    JobService service(clock, engine, ids);
    require(!service.status().output_active, "startup must inhibit output");
    require(service.handle(request("CLAIM", ClaimBody{std::string(32, '2'), 10'000}, 'a'))
                .error == ErrorCode::HelloRequired,
            "CLAIM before HELLO must fail");
    require(service.handle(request("HELLO", HelloBody{{"WTP/1"}}, 'b')).ok,
            "HELLO must succeed");
    const auto claim = request("CLAIM", ClaimBody{std::string(32, '2'), 10'000}, 'c');
    require(service.handle(claim).ok, "CLAIM must succeed");
    require(service.handle(claim).ok, "identical CLAIM replay must succeed");
    require(service.status().owner_id.has_value(), "claim must establish ownership");
    require(service.local_abort().ok, "local safety abort must succeed");
    require(!service.status().output_active, "local abort must leave output inactive");
}
