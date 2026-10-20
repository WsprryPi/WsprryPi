/**
 * @file non_wspr_request_builder.hpp
 * @brief Builds backend-neutral and legacy requests for timed CW modes.
 */

#ifndef NON_WSPR_REQUEST_BUILDER_HPP
#define NON_WSPR_REQUEST_BUILDER_HPP

#include "config_types.hpp"
#include "transmission_request.hpp"
#include "wspr_transmit_types.hpp"

namespace scheduling_detail
{
wsprrypi::TransmissionRequest make_qrss_controller_request(
    const ArgParserConfig &cfg, double committed_ppm);
TransmissionRequest make_qrss_legacy_request(
    const ArgParserConfig &cfg, double committed_ppm);
wsprrypi::TransmissionRequest make_fskcw_controller_request(
    const ArgParserConfig &cfg, double committed_ppm);
TransmissionRequest make_fskcw_legacy_request(
    const ArgParserConfig &cfg, double committed_ppm);
wsprrypi::TransmissionRequest make_dfcw_controller_request(
    const ArgParserConfig &cfg, double committed_ppm);
TransmissionRequest make_dfcw_legacy_request(
    const ArgParserConfig &cfg, double committed_ppm);
}

#endif // NON_WSPR_REQUEST_BUILDER_HPP
