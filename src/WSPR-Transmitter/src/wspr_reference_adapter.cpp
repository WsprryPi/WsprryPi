/**
 * @file wspr_reference_adapter.cpp
 * @brief Adapter helpers for wspr-reference integration.
 */

#include "wspr_reference_adapter.hpp"

#include "wspr_ref_encode_internal.hpp"

#include <sstream>
#include <stdexcept>

namespace
{
PreparedWsprFrame to_frame(const std::string& symbols)
{
    if (symbols.size() != WSPR_SYMBOL_COUNT)
    {
        std::ostringstream oss;
        oss << "Encoded WSPR frame has invalid length: " << symbols.size();
        throw std::runtime_error(oss.str());
    }

    PreparedWsprFrame frame;
    for (std::size_t i = 0; i < WSPR_SYMBOL_COUNT; ++i)
    {
        const char ch = symbols[i];
        if (ch < '0' || ch > '3')
        {
            std::ostringstream oss;
            oss << "Encoded WSPR frame contains invalid symbol at index "
                << i << ".";
            throw std::runtime_error(oss.str());
        }
        frame.symbols[i] = static_cast<std::uint8_t>(ch - '0');
    }

    return frame;
}

PreparedWsprTransmission build_prepared_wspr_transmission(
    const wspr::WsprEncodeResult& result,
    const wspr::internal::WsprEncodeRuntimeMetadata& metadata)
{
    if (!result.ok)
    {
        throw std::runtime_error(result.error.empty() ?
            "WSPR encoding failed." : result.error);
    }

    PreparedWsprTransmission plan;
    plan.plan_type = result.type;
    plan.callsign = result.callsign;
    plan.locator = result.locator;
    plan.callsign_raw = metadata.callsign_raw;
    plan.locator_raw = metadata.locator_raw;
    plan.callsign_normalized =
        metadata.callsign_normalized.empty() ? result.callsign : metadata.callsign_normalized;
    plan.locator_normalized =
        metadata.locator_normalized.empty() ? result.locator : metadata.locator_normalized;
    plan.frame_callsigns = metadata.frame_callsigns;
    plan.frame_locators = metadata.frame_locators;
    plan.power_dbm = result.power_dbm;

    if (!result.symbols_list.empty())
    {
        for (const auto& symbols : result.symbols_list)
            plan.frames.push_back(to_frame(symbols));
    }
    else if (!result.symbols.empty())
    {
        plan.frames.push_back(to_frame(result.symbols));
    }

    if (plan.frames.empty())
        throw std::runtime_error("WSPR encoding returned no frames.");

    plan.total_frame_count =
        metadata.total_frame_count != 0U ? metadata.total_frame_count : plan.frames.size();
    plan.current_frame = 1U;
    if (!plan.frame_callsigns.empty())
        plan.frame_callsign = plan.frame_callsigns.front();
    else
        plan.frame_callsign = plan.callsign_normalized;
    if (!plan.frame_locators.empty())
        plan.frame_locator = plan.frame_locators.front();
    else
        plan.frame_locator = plan.locator_normalized;

    return plan;
}
} // namespace

// Remember: Top-level wsprrypi should not call it directly outside scheduler/orchestration logic
PreparedWsprTransmission build_prepared_wspr_transmission(
    const std::string& callsign,
    const std::string& locator,
    int power_dbm)
{
    return build_prepared_wspr_transmission(
        callsign,
        locator,
        power_dbm,
        wspr::TransmissionPlanPreference::Auto);
}

PreparedWsprTransmission build_prepared_wspr_transmission(
    const std::string& callsign,
    const std::string& locator,
    int power_dbm,
    wspr::TransmissionPlanPreference preference)
{
    const auto prepared = wspr::internal::encode_message_with_metadata(
        callsign,
        locator,
        power_dbm,
        preference);
    return build_prepared_wspr_transmission(
        prepared.encoded,
        prepared.metadata);
}

PreparedWsprTransmission build_prepared_wspr_transmission(
    const wspr::WsprEncodeResult& result)
{
    wspr::internal::WsprEncodeRuntimeMetadata metadata;
    metadata.callsign_raw = result.callsign;
    metadata.locator_raw = result.locator;
    metadata.callsign_normalized = result.callsign;
    metadata.locator_normalized = result.locator;
    metadata.total_frame_count =
        !result.symbols_list.empty() ? result.symbols_list.size() :
        (!result.symbols.empty() ? 1U : 0U);
    return build_prepared_wspr_transmission(result, metadata);
}
