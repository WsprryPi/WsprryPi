// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
namespace wsprrypi {
struct BrowserHttpOperation {std::string name;std::size_t maximum_body;std::uint64_t maximum_resource{128ULL*1024*1024};};
// Closed application routes, not an arbitrary HTTP proxy or filesystem API.
std::optional<BrowserHttpOperation> browser_http_operation(std::string_view method,std::string_view path);
}
