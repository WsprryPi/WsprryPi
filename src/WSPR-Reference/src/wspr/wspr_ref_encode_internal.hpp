#ifndef WSPR_REF_ENCODE_INTERNAL_HPP
#define WSPR_REF_ENCODE_INTERNAL_HPP

#include "wspr_ref_api.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace wspr::internal
{
struct WsprEncodeRuntimeMetadata
{
    std::string callsign_raw;
    std::string locator_raw;
    std::string callsign_normalized;
    std::string locator_normalized;
    std::vector<std::string> frame_callsigns;
    std::vector<std::string> frame_locators;
    std::size_t total_frame_count = 0;
};

struct WsprPreparedEncodeResult
{
    WsprEncodeResult encoded;
    WsprEncodeRuntimeMetadata metadata;
};

WsprPreparedEncodeResult encode_message_with_metadata(
    const std::string& callsign,
    const std::string& locator,
    int power_dbm,
    TransmissionPlanPreference preference);
} // namespace wspr::internal

#endif // WSPR_REF_ENCODE_INTERNAL_HPP
