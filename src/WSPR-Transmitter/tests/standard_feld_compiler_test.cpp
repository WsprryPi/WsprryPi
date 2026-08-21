#include "execution_plan_compiler.hpp"
#include "standard_feld.hpp"
#include "standard_feld_asset.hpp"

#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{

using namespace wsprrypi;

TransmissionRequest request_for(std::string message, double frequency = 14'096'900.0)
{
    TransmissionRequest request;
    request.id.value = 42;
    request.mode = TransmissionMode::STANDARD_FELD;
    StandardFeldPayload payload;
    payload.message = std::move(message);
    payload.frequency_hz = frequency;
    request.payload = std::move(payload);
    return request;
}

bool rejected(const TransmissionRequest& request)
{
    try
    {
        (void)ExecutionPlanCompiler{}.compile(request);
        return false;
    }
    catch (const std::runtime_error&)
    {
        return true;
    }
}

std::string raster_for_message_cell(const ExecutionPlan& plan)
{
    std::string bits;
    for (const auto& event : plan.events)
    {
        assert(event.raster_progress.has_value());
        if (event.raster_progress->cell_kind ==
            RfEvent::RasterProgress::CellKind::MESSAGE)
            bits.push_back(event.rf_on ? '1' : '0');
    }
    return bits;
}

void assert_fixture_summary(const std::string& message,
                            std::size_t expected_positions,
                            std::chrono::nanoseconds expected_duration)
{
    const auto plan = ExecutionPlanCompiler{}.compile(request_for(message));
    assert(plan.events.size() == expected_positions);
    assert(plan.summary.event_count == expected_positions);
    assert(plan.summary.total_duration == expected_duration);
    for (std::size_t i = 0; i < plan.events.size(); ++i)
    {
        assert(plan.events[i].raster_progress.has_value());
        assert(plan.events[i].raster_progress->absolute_position == i);
    }
    assert(!plan.events.back().rf_on);
}

} // namespace

int main()
{
    using namespace wsprrypi;
    using namespace wsprrypi::standard_feld;

    static_assert(kStoredRows.size() == 64);
    static_assert(kPositionsPerCell == 98);
    assert(kProfileId == "standard-feld-wsprry-v1");
    assert(kAssetId == "wsprry-standard-feld-radiolib-5x5-v1");
    assert(kCanonicalAssetSha256 ==
           "025c4ee1227a6d2043b460c973a98b3c5f875b64c1ee96d20a71ad2e78091227");

    assert(normalize_message("abc xyz") == "ABC XYZ");
    for (unsigned char value = 0x20; value <= 0x5f; ++value)
    {
        const std::string input(1, static_cast<char>(value));
        assert(normalize_message(input) == input);
        const auto plan = ExecutionPlanCompiler{}.compile(request_for(input));
        assert(plan.events.size() == 3U * kPositionsPerCell);
        assert(raster_for_message_cell(plan).size() == kPositionsPerCell);
    }

    assert(rejected(request_for("")));
    assert(rejected(request_for(std::string(1, '\xff'))));
    assert(rejected(request_for("\t")));
    assert(rejected(request_for("\r")));
    assert(rejected(request_for("\n")));
    assert(rejected(request_for(std::string(1, '\0'))));
    assert(rejected(request_for("`")));
    assert(rejected(request_for("{")));
    assert(rejected(request_for("|")));
    assert(rejected(request_for("}")));
    assert(rejected(request_for("~")));
    assert(rejected(request_for("\xc3\xa9")));
    assert(rejected(request_for("ABC\xc3\xa9")));
    assert(rejected(request_for("A", 0.0)));
    auto wrong_profile = request_for("A");
    std::get<StandardFeldPayload>(wrong_profile.payload).profile_id = "other";
    assert(rejected(wrong_profile));

    const auto plan = ExecutionPlanCompiler{}.compile(request_for("a"));
    assert(plan.mode == TransmissionMode::STANDARD_FELD);
    assert(plan.request_id.value == 42);
    assert(plan.reference_frequency_hz == 14'096'900.0);
    assert(plan.events.size() == 294);
    assert(plan.summary.event_count == 294);
    assert(plan.summary.total_duration == std::chrono::milliseconds(1200));
    assert(plan.summary.min_frequency_hz == 14'096'900.0);
    assert(plan.summary.max_frequency_hz == 14'096'900.0);
    assert(raster_for_message_cell(plan) ==
        "00000000000000001111111111000000001100110000000011001100000000110011000011111111110000000000000000");

    for (std::size_t i = 0; i < plan.events.size(); ++i)
    {
        const auto& event = plan.events[i];
        assert(event.raster_progress->absolute_position == i);
        assert(event.duration > std::chrono::nanoseconds::zero());
        if (i > 0)
        {
            const auto& previous = plan.events[i - 1];
            assert(previous.offset_from_start + previous.duration ==
                   event.offset_from_start);
        }
    }
    assert(!plan.events.front().rf_on);
    assert(!plan.events.back().rf_on);
    assert(plan.events.back().type == RfEventType::RF_OFF);
    assert(plan.events.back().offset_from_start + plan.events.back().duration ==
           plan.summary.total_duration);

    const auto spaced = ExecutionPlanCompiler{}.compile(request_for("A  B"));
    assert(spaced.events.size() == 6U * kPositionsPerCell);
    assert(spaced.summary.total_duration == std::chrono::milliseconds(2400));

    // Frozen message fixture summaries.
    assert_fixture_summary("AB", 392, std::chrono::milliseconds(1600));
    assert_fixture_summary("wspry", 686, std::chrono::milliseconds(2800));
    assert_fixture_summary("A  B", 588, std::chrono::milliseconds(2400));
    assert_fixture_summary(" A ", 490, std::chrono::seconds(2));
    assert_fixture_summary(
        "HELL TEST 0123456789 DE WSPRY WSPRY 73",
        3920,
        std::chrono::seconds(16));
    assert_fixture_summary(
        " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_",
        6468,
        std::chrono::milliseconds(26400));
    assert_fixture_summary("12? /+-.,I", 1176, std::chrono::milliseconds(4800));

    // The frozen cancellation contract includes every boundary. One event per
    // physical position makes every [0, total] boundary directly recoverable.
    const auto corpus = ExecutionPlanCompiler{}.compile(
        request_for("HELL TEST 0123456789 DE WSPRY WSPRY 73"));
    for (std::size_t boundary = 0; boundary <= corpus.events.size(); ++boundary)
    {
        const auto actual = boundary == corpus.events.size()
            ? corpus.summary.total_duration
            : corpus.events[boundary].offset_from_start;
        const auto expected = std::chrono::nanoseconds{
            static_cast<std::chrono::nanoseconds::rep>(
                (boundary * 1'000'000'000ULL + 122U) / 245U)};
        assert(actual == expected);
    }

    std::cout << "PASS: Standard Feld compiler contract\n";
}
