#include "wtp_pi_control.hpp"

#include <stdexcept>

int main() {
    if (!wtp_pi_local_effective() || !wtp_pi_begin_scheduled() ||
        !wtp_pi_begin_test_tone() || !wtp_pi_begin_configuration())
        throw std::runtime_error("Uninstalled hooks must preserve direct runtime behavior");
    bool committed = false, ended = false, scheduled_ended = false,
         configuration_ended = false;
    set_wtp_pi_control_hooks({
        [] { return false; }, [] { return false; }, [] { return false; },
        [&](bool off) { ended = off; },
        [&](bool enabled) { committed = enabled; },
        [&](bool off) { scheduled_ended = off; },
        [] { return false; },
        [&] { configuration_ended = true; }});
    if (wtp_pi_local_effective() || wtp_pi_begin_scheduled() ||
        wtp_pi_begin_test_tone() || wtp_pi_begin_configuration())
        throw std::runtime_error("Installed hooks must block local RF admission");
    wtp_pi_end_local(true);
    wtp_pi_config_transmit_committed(true);
    wtp_pi_end_scheduled(true);
    wtp_pi_end_configuration();
    if (!ended || !committed || !scheduled_ended || !configuration_ended)
        throw std::runtime_error("Local cleanup and config hooks were not invoked");
    clear_wtp_pi_control_hooks();
    if (!wtp_pi_local_effective())
        throw std::runtime_error("Cleared hooks must restore ordinary runtime behavior");
    set_wtp_pi_management_readiness([]{return false;});
    if(wtp_pi_begin_scheduled()||wtp_pi_begin_test_tone()||wtp_pi_begin_configuration())
        throw std::runtime_error("Durable readiness must fence every new local admission without an endpoint");
    set_wtp_pi_control_hooks({[]{return true;},[]{return true;},[]{return true;},{},{},{},[]{return true;},{}});
    if(wtp_pi_begin_scheduled()||wtp_pi_begin_test_tone()||wtp_pi_begin_configuration())
        throw std::runtime_error("Endpoint hooks cannot bypass durable readiness");
    clear_wtp_pi_control_hooks();
    if(wtp_pi_begin_scheduled())throw std::runtime_error("Endpoint teardown cannot clear durable readiness");
    set_wtp_pi_management_readiness({});
    if(!wtp_pi_begin_scheduled())throw std::runtime_error("Owner teardown restores direct CLI behavior");
}
