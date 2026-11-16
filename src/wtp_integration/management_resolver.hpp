// SPDX-License-Identifier: MIT
#pragma once
#include "tls.hpp"
#include <optional>
namespace wsprrypi {
bool private_management_address(const std::string &, const std::vector<std::string> &local_addresses);
std::unique_ptr<TlsResolver> management_tls_resolver();
// Typed resolver seam only. No config/CLI/browser selector replaces DNS or self
// identity. Tests use it to exercise refusal before transport creation.
std::unique_ptr<TlsResolver> management_tls_resolver_for_test(std::unique_ptr<TlsResolver>,
    std::function<std::optional<std::vector<std::string>>()> local_addresses);
} // namespace wsprrypi
