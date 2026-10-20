#include "../privileged_network_runtime.hpp"

#include <cassert>
#include <iostream>
#include <optional>

int main() {
    initialize_privileged_network_runtime(std::nullopt);
    auto state = privileged_network_runtime_state();
    assert(state.configured == PrivilegedNetworkMode::enforced);
    assert(state.active == PrivilegedNetworkMode::enforced);
    assert(!state.setting_was_valid && state.setting_was_missing);
    assert(state.active_known);

    initialize_privileged_network_runtime("TRUE");
    state = privileged_network_runtime_state();
    assert(state.active == PrivilegedNetworkMode::enforced);
    assert(!state.setting_was_valid && !state.setting_was_missing);

    initialize_privileged_network_runtime("insecure-disabled");
    state = privileged_network_runtime_state();
    assert(state.configured == PrivilegedNetworkMode::insecure_disabled);
    assert(state.active == PrivilegedNetworkMode::insecure_disabled);
    assert(state.setting_was_valid);
    assert(state.active_known);
    assert(privileged_network_runtime_status_text() == "NETWORK SAFETY OFF");

    set_active_privileged_network_mode(PrivilegedNetworkMode::enforced);
    state = privileged_network_runtime_state();
    assert(state.configured == PrivilegedNetworkMode::insecure_disabled);
    assert(state.active == PrivilegedNetworkMode::enforced);
    assert(privileged_network_runtime_status_text() == "NETWORK SAFETY ENFORCED");

    std::cout << "privileged_network_runtime_test: PASS\n";
}
