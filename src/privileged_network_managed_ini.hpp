// SPDX-License-Identifier: MIT
#pragma once
#include "privileged_network_admin.hpp"
// Production application adapter; the independent admin/policy test boundary
// keeps its ordinary filesystem defaults when this adapter is not installed.
PrivilegedNetworkIniIO managed_network_ini_io();
PrivilegedNetworkSettingsIO managed_network_settings_io();
