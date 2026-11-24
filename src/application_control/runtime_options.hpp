// SPDX-License-Identifier: MIT
#pragma once
#include "local_transport.hpp"
#include <optional>
namespace wsprrypi {
LocalTransportOptions backend_control_options(const std::optional<std::string> &directory = {});
LocalTransportOptions administrator_control_options(const std::optional<std::string> &directory = {});
LocalTransportOptions web_control_options(const std::optional<std::string> &directory = {});
namespace testing {
// Private actual-process fixture path, selected before production main runs.
// No environment fallback or installed-state discovery is exposed.
void set_application_control_runtime_directory(const std::string &);
bool application_control_runtime_fixture_selected();
}
}
