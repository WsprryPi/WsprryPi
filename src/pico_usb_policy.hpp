// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <string_view>
namespace wsprrypi {
inline bool pico_usb_http_mutation(std::string_view method,std::string_view path) {
  return method!="GET" && method!="HEAD" && method!="OPTIONS" &&
      !(method=="POST" && path=="/api/control/v1/member/firmware");
}
inline bool pico_usb_command_mutation(std::string_view command) {
  return command!="get_tx_state" && command!="wspr_band_catalog" && command!="echo";
}
inline bool pico_usb_serial(const std::string& serial) {
  return serial.size()==16 && serial.find_first_not_of("0123456789ABCDEF")==std::string::npos;
}
} // namespace wsprrypi
