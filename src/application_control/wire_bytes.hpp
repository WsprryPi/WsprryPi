// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <string>
namespace wsprrypi {
std::string encode_wire_chunk(const std::string &);
std::string decode_wire_chunk(const std::string &);
std::uint64_t wire_offset(const std::string &);
}
