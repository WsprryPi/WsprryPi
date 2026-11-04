#pragma once
#include "mcp2221a_protocol.hpp"
namespace mcp2221a {
Inventory discover(); // IORegistry metadata only; never opens a HID session.
std::shared_ptr<Channel> native_channel();
}
