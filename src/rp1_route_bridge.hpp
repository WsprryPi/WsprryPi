#pragma once

#include "config_types.hpp"
#include "test_tone_types.hpp"
#include "transmission_request.hpp"
#include "wspr_transmit_types.hpp"

#include <cstdint>
#include <string>

struct Rp1IdleReconciliation
{
    bool ok = false;
    std::string message;
    std::string policy_domain;
};

struct Rp1RouteStatus
{
    std::string requested;
    std::string persisted;
    std::string configured;
    std::string active;
    bool eligible = false;
    std::string journal;
};

bool apply_direct_rp1_development_confirmation_bridge(
    const ArgParserConfig &, TransmissionRequest &, std::string *);
void apply_test_tone_rp1_development_confirmation_bridge(
    const ParsedTestToneRequest::Rp1DevelopmentConfirmation &,
    const ArgParserConfig &, TransmissionRequest &);
Rp1IdleReconciliation reconcile_rp1_idle_startup(int gpio);
bool acknowledge_rp1_restoration(const std::string &token, bool transmit);
Rp1RouteStatus query_rp1_route_status();
void reset_rp1_development_reconcile_invoker_bridge() noexcept;
