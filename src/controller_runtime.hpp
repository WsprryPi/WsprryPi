// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once

#include "json.hpp"
#include <string>
#include <string_view>

// A process role, independent of every transmitter backend. No INI or browser
// action can select it, and failed output initialization never enters it.
bool controller_only_requested(int argc, char *argv[]) noexcept;
bool controller_only_mode() noexcept;
bool controller_only_ready() noexcept;
void configure_controller_only(int argc, char *argv[]);
// Tests may supply an explicit identity without changing the production machine
// identity provider. No CLI, environment, INI, or HTTP path exposes this seam.
bool controller_only_loop(const std::string &fixture_identity = {});
nlohmann::json controller_only_status();
// Process-owned management boot; unavailable until the role is established.
std::string controller_only_process_boot();
// Prepare the one backend process boot for private local admission before
// listener readiness. This grants no browser or device management permission.
std::string prepare_controller_process_boot();
bool controller_http_allowed(std::string_view method, std::string_view path);
namespace wsprrypi {class WtpCatalog;}
// Backend-only SQL owners, available only in the explicit controller role.
wsprrypi::WtpCatalog &controller_storage_catalog();
std::string persist_controller_network_safety_setting(const std::string &expected_revision,bool insecure_disabled);
std::string persist_controller_advanced_settings(const std::string &expected_revision,const nlohmann::json &changes);
