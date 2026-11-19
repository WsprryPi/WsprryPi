// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include "wtp_settings.hpp"
#include "wtp_integration/network_http.hpp"
#include "WSPR-Transmitter/src/transmission_backend.hpp"
#include <chrono>
#include <functional>
#include <optional>
std::string wtp_runtime_selection_error(const std::optional<WtpSettings> &);
void select_wtp_runtime(const std::optional<WtpSettings> &);
bool wtp_runtime_selected() noexcept;
bool wtp_runtime_ready() noexcept;
bool wtp_runtime_firmware_busy() noexcept;
bool wtp_runtime_invalidate_for_reload();
bool wtp_runtime_defers_reload() noexcept;
std::chrono::nanoseconds wtp_runtime_preparation_lead();
void wtp_runtime_poll_usb_time();
void wtp_runtime_poll_idle();
wsprrypi::CleanupResult wtp_runtime_recover();
std::string wtp_runtime_json();
wsprrypi::PicoHttpResponse wtp_runtime_management(const std::string &, const std::string &,
    const std::string &, const std::string &, const std::string & = {});
wsprrypi::PicoHttpResponse wtp_runtime_firmware(const std::string &, const std::string &);
wsprrypi::CleanupResult wtp_runtime_cancel_job(const std::string &);
nlohmann::json wtp_runtime_safe_probe(const std::function<nlohmann::json()> &);
bool wtp_runtime_switch_allowed() noexcept;
