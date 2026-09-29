#include "wtp_pi_control.hpp"

#include <mutex>
#include <utility>

namespace {
std::mutex hooks_mutex;
WtpPiControlHooks hooks;

template <typename Member> auto copy_hook(Member member) {
    std::lock_guard lock(hooks_mutex);
    return hooks.*member;
}
} // namespace

void set_wtp_pi_control_hooks(WtpPiControlHooks replacement) {
    std::lock_guard lock(hooks_mutex);
    hooks = std::move(replacement);
}

void clear_wtp_pi_control_hooks() {
    std::lock_guard lock(hooks_mutex);
    hooks = {};
}

bool wtp_pi_local_effective() noexcept {
    try {
        auto hook = copy_hook(&WtpPiControlHooks::local_effective);
        return !hook || hook();
    } catch (...) { return false; }
}

bool wtp_pi_begin_scheduled() noexcept {
    try {
        auto hook = copy_hook(&WtpPiControlHooks::begin_scheduled);
        return !hook || hook();
    } catch (...) { return false; }
}

bool wtp_pi_begin_test_tone() noexcept {
    try {
        auto hook = copy_hook(&WtpPiControlHooks::begin_test_tone);
        return !hook || hook();
    } catch (...) { return false; }
}

void wtp_pi_end_local(bool output_inactive) noexcept {
    try {
        auto hook = copy_hook(&WtpPiControlHooks::end_local);
        if (hook) hook(output_inactive);
    } catch (...) {}
}

void wtp_pi_config_transmit_committed(bool enabled) noexcept {
    try {
        auto hook = copy_hook(&WtpPiControlHooks::config_transmit_committed);
        if (hook) hook(enabled);
    } catch (...) {}
}

void wtp_pi_end_scheduled(bool output_inactive) noexcept {
    try {
        auto hook = copy_hook(&WtpPiControlHooks::end_scheduled);
        if (hook) hook(output_inactive);
    } catch (...) {}
}

bool wtp_pi_begin_configuration() noexcept {
    try {
        auto hook = copy_hook(&WtpPiControlHooks::begin_configuration);
        return !hook || hook();
    } catch (...) { return false; }
}

void wtp_pi_end_configuration() noexcept {
    try {
        auto hook = copy_hook(&WtpPiControlHooks::end_configuration);
        if (hook) hook();
    } catch (...) {}
}
