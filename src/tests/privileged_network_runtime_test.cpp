#include "../privileged_network_runtime.hpp"

#include <cassert>
#include <iostream>
#include <optional>
#include <future>
#include <chrono>
#include <functional>
#include <array>

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

    // The actual policy publishers cannot cross a held final management
    // admission. This exercises their shared gate, not an artificial callback.
    for(const auto &publish:std::array<std::function<void()>,4>{
        []{initialize_privileged_network_runtime("enforced");},
        []{set_active_privileged_network_mode(PrivilegedNetworkMode::insecure_disabled);},
        []{set_privileged_network_runtime_mode(PrivilegedNetworkMode::insecure_disabled);},
        []{set_privileged_network_runtime_unknown();}}){
        std::unique_lock admission(privileged_network_admission_mutex());std::promise<void> entered;auto ready=entered.get_future();
        const auto before=privileged_network_runtime_state();
        auto writer=std::async(std::launch::async,[&]{entered.set_value();publish();});ready.wait();
        assert(writer.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout);
        const auto during=privileged_network_runtime_state();
        assert(during.active==before.active&&during.configured==before.configured&&during.active_known==before.active_known);
        admission.unlock();writer.get();
    }
    assert(!privileged_network_runtime_state().active_known);

    std::cout << "privileged_network_runtime_test: PASS\n";
}
