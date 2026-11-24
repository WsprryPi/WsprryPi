// SPDX-License-Identifier: MIT
#pragma once
#include "pico_usb_policy.hpp"
#ifndef _WIN32
#include "pico_usb_installer_state.hpp"
#else
#include <memory>
#include <string>
namespace wsprrypi {
// The optional Linux firmware installer is not supplied on Windows. Its
// persistent holds and shared updater lock must never be inferred from a
// Windows drive-relative spelling of /var or /run. Firmware mutation remains
// unavailable; the native CDC adapter independently owns its selected device
// through CreateFile with no sharing and verified native identity.
inline bool pico_usb_any_update_blocked() noexcept { return false; }
inline bool pico_usb_update_blocked(const std::string &) noexcept { return false; }
inline std::shared_ptr<void> pico_usb_runtime_lease(const std::string &) { return {}; }
} // namespace wsprrypi
#endif
