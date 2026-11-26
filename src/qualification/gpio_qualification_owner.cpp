// SPDX-License-Identifier: MIT
#include "gpio_qualification_owner.hpp"
#include "system_clock_frequency_estimate.hpp"
#include "wtp_server/sha256.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <fcntl.h>
#include <set>
#include <stdexcept>
#include <unistd.h>

namespace gpio_qualification
{
void require(bool condition, const char* reason)
{
    if (!condition) throw std::runtime_error(reason);
}

void closed(const Json& value, std::initializer_list<const char*> fields)
{
    require(value.is_object() && value.size() == fields.size(), "Invalid closed record.");
    for (const char* field : fields)
        require(value.contains(field), "Missing or unexpected field.");
}

std::string token(const Json& value)
{
    require(value.is_string(), "Expected an owner identifier.");
    const auto result = value.get<std::string>();
    require(result.size() == 32 &&
        result.find_first_not_of("0123456789abcdef") == std::string::npos,
        "Malformed owner identifier.");
    return result;
}

int integer(const Json& value, int lower, int upper)
{
    require(value.is_number_integer() &&
        (!value.is_number_unsigned() || value.get<std::uint64_t>() <= std::uint64_t(upper)) &&
        value.get<std::int64_t>() >= lower && value.get<std::int64_t>() <= upper,
        "Invalid bounded integer.");
    return value.get<int>();
}

Json parse(std::string_view bytes)
{
    require(!bytes.empty() && bytes.size() <= max_message, "Invalid message size.");
    std::array<std::set<std::string>, 16> keys;
    return Json::parse(bytes.begin(), bytes.end(), [&](int depth, Json::parse_event_t event, Json& item)
    {
        require(depth >= 0 && depth < int(keys.size()) - 1, "Message nesting exceeds limit.");
        switch (event)
        {
        case Json::parse_event_t::object_start: keys[depth + 1].clear(); break;
        case Json::parse_event_t::key:
            require(keys[depth].insert(item.get<std::string>()).second, "Duplicate JSON key.");
            break;
        default: break;
        }
        return true;
    });
}

namespace
{
std::string hex(std::span<const std::uint8_t> bytes)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (auto byte : bytes)
    {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}

constexpr std::array<int, 10> carriers{
    475700, 1838100, 3570100, 5366200, 7040100,
    10140200, 14097100, 18106100, 21096100, 28126100};
constexpr std::array bands{"630m", "160m", "80m", "60m", "40m", "30m", "20m", "17m", "15m", "10m"};
}

std::string sha256(std::string_view bytes)
{
    return hex(wsprrypico::wtp::sha256(
        {reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()}));
}

std::string nonce()
{
    std::array<std::uint8_t, 16> bytes{};
    const int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    require(fd >= 0, "Random identifier unavailable.");
    const auto count = read(fd, bytes.data(), bytes.size());
    close(fd);
    require(count == ssize_t(bytes.size()), "Random identifier unavailable.");
    return hex(bytes);
}

std::int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now().time_since_epoch()).count();
}

Json point(unsigned index)
{
    require(index < 80, "Point outside BCM2835 matrix.");
    char id[5];
    std::snprintf(id, sizeof(id), "P%03u", index + 1);
    return {{"point_id", id}, {"band", bands[index % 10]}, {"dial_hz", carriers[index % 10] - 1500},
        {"carrier_hz", carriers[index % 10]}, {"drive_ma", 2 * (index / 10 + 1)}};
}

Json admission(const Json& source, const Json& requested)
{
    closed(requested, {"point_id", "band", "dial_hz", "carrier_hz", "drive_ma"});
    const auto hz = integer(requested.at("carrier_hz"), carriers.front(), carriers.back());
    const auto drive = integer(requested.at("drive_ma"), 2, 16);
    const auto carrier = std::find(carriers.begin(), carriers.end(), hz);
    require(carrier != carriers.end() && requested.dump() ==
        point((drive / 2 - 1) * 10 + (carrier - carriers.begin())).dump(),
        "Changed fixed BCM2835 matrix point.");
    const auto additional = integer(source.at("ppm_millionths"), -200'000'000, 200'000'000);
    const bool uncalibrated = source.at("clock_policy") == "uncalibrated";
    const auto correction = select_gpio_frequency_correction(
        uncalibrated, 0.0, additional / 1e6, {});
    Json result{{"carrier_hz", hz}, {"drive_code", drive / 2 - 1},
        {"ppm_millionths", additional}, {"policy", source.at("clock_policy")},
        {"lower_nhz", nullptr}, {"upper_nhz", nullptr}, {"deadline_ms", nullptr},
        {"plan", nullptr}, {"refusal", nullptr}};
    try
    {
        require(correction.valid && (!uncalibrated || additional == 0), "Invalid correction policy.");
        const auto plan = wsprrypi::planBcm2835Qualification(
            source.at("gpio").get<int>(), drive / 2 - 1, hz, correction.effective_ppm);
        result["lower_nhz"] = plan.lower_nhz;
        result["upper_nhz"] = plan.upper_nhz;
        result["deadline_ms"] = cutoff_ms;
        const auto digest_data = Json{{"contract", contract}, {"source", source},
            {"point", requested}, {"dividers", plan.dividers.words},
            {"rf_clock_binary64", std::bit_cast<std::uint64_t>(plan.rf_clock_hz)},
            {"pwm_clock_binary64", std::bit_cast<std::uint64_t>(plan.pwm_clock_hz)},
            {"intrinsic_ppm_binary64", std::bit_cast<std::uint64_t>(plan.intrinsic_ppm)},
            {"effective_ppm_binary64", std::bit_cast<std::uint64_t>(plan.effective_ppm)},
            {"dither_clocks", wsprrypi::bcm2835_qualification_dither_clocks},
            {"lower_clocks", plan.lower_clocks}, {"duration_ms", cutoff_ms},
            {"window_s", 1}, {"lower_nhz", plan.lower_nhz}, {"upper_nhz", plan.upper_nhz}};
        result["plan"] = sha256(digest_data.dump());
    }
    catch (const std::exception& error)
    {
        result["refusal"] = error.what();
    }
    return result;
}

Json bankJson(QualificationBank bank)
{
    return {{"drive", bank.bits & 7}, {"hysteresis", bool(bank.bits & 8)}, {"slew", bool(bank.bits & 16)}};
}

Json nativeJson(const QualificationObservation& state, int pin, bool owned)
{
    const auto selected = pin == 4 ? state.gpio4 : state.gpio20;
    return {{"gpclk_enabled", state.clock_enabled}, {"gpclk_busy", state.clock_busy},
        {"gpio_input", selected == 0}, {"other_gpio_input", (pin == 4 ? state.gpio20 : state.gpio4) == 0},
        {"dma_idle", state.dma_idle}, {"pwm_idle", state.pwm_idle},
        {"mux", selected == 0 ? "INPUT" : selected == (pin == 4 ? 4u : 2u) ?
            (pin == 4 ? "ALT0" : "ALT5") : "UNKNOWN"},
        {"owned", owned}};
}

Json merge(Json left, const Json& right)
{
    left.update(right);
    return left;
}

Json envelope(std::string_view bytes, const Json& request, const Json& receipt)
{
    return {{"contract", contract}, {"operation", request.at("operation")}, {"call", request.at("call")},
        {"request_sha256", sha256(bytes)}, {"receipt", receipt}};
}
}
