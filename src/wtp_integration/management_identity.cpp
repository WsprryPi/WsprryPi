// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "management_identity.hpp"
#include <random>
namespace wsprrypi {
std::string wtp_random_identity() {
  std::random_device random;
  static constexpr char hex[] = "0123456789abcdef";
  std::string id(32, '0');
  for (auto &c : id)
    c = hex[random() & 15];
  return id;
}
}
