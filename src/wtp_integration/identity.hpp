// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#pragma once
#include <algorithm>
#include <arpa/inet.h>
#include <optional>
#include <string>

namespace wsprrypi {
// Canonicalize only at use sites: persisted settings and browser drafts retain
// their spelling. No resolution, reverse lookup, URI or wildcard interpretation.
inline std::optional<std::string> canonical_network_identity(std::string name) {
  if (name.empty() || name.size() > 254 || std::any_of(name.begin(), name.end(), [](unsigned char c) { return c <= 32 || c >= 127; })) return std::nullopt;
  in6_addr address{};
  char numeric[INET6_ADDRSTRLEN]{};
  for (const auto family : {AF_INET, AF_INET6}) {
    if (inet_pton(family, name.c_str(), &address) == 1) {
      if (!inet_ntop(family, &address, numeric, sizeof(numeric))) return std::nullopt;
      // Darwin accepts leading-zero IPv4 octets that Linux rejects. Keep one
      // unambiguous grammar across hosts and the browser.
      if (family == AF_INET && name != numeric) return std::nullopt;
      if (family == AF_INET6 && name.find('.') != std::string::npos) {
        const auto tail = name.substr(name.rfind(':') + 1);
        if (canonical_network_identity(tail) != tail) return std::nullopt;
      }
      return std::string(numeric);
    }
  }
  if (name.find(':') != std::string::npos) return std::nullopt;
  if (name.back() == '.') name.pop_back();
  if (name.empty() || name.size() > 253 || name.back() == '.') return std::nullopt;
  std::size_t start = 0;
  bool numeric_looking = true;
  while (start < name.size()) {
    auto end = name.find('.', start);
    if (end == std::string::npos) end = name.size();
    if (end == start || end - start > 63 || name[start] == '-' || name[end - 1] == '-')
      return std::nullopt;
    for (auto i = start; i < end; ++i) {
      auto &c = name[i];
      if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
      if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
        return std::nullopt;
    }
    const auto label = name.substr(start, end - start);
    const auto decimal = [](unsigned char c) { return c >= '0' && c <= '9'; };
    const bool numeric_label = std::all_of(label.begin(), label.end(), decimal) ||
        (label.size() > 2 && label.starts_with("0x") &&
         std::all_of(label.begin() + 2, label.end(), [&](unsigned char c) { return decimal(c) || (c >= 'a' && c <= 'f'); }));
    numeric_looking = numeric_looking && numeric_label;
    start = end + 1;
  }
  // getaddrinfo may treat abbreviated, octal or hexadecimal forms as numeric.
  // Only the canonical inet_pton/inet_ntop IPv4 form above is admitted.
  if (numeric_looking) return std::nullopt;
  return name;
}
} // namespace wsprrypi
