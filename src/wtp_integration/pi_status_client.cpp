// SPDX-License-Identifier: MIT
#include "network_http.hpp"
#include "plain_tcp.hpp"
#include "application.hpp"
#include "browser_api.hpp"
#include "fleet_store.hpp"
#include <cstdlib>
namespace wsprrypi {
nlohmann::json wtp_pi_revocation_status(const WtpSettings& settings, unsigned port) {
  if (settings.transport != "network_plain" || port == 0 || port > 65535)
    throw std::runtime_error("Pi reconciliation requires a Plain LAN management endpoint");
  if (const char* disabled=std::getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");
      disabled && std::string(disabled)=="1")
    throw std::runtime_error("Pi network reconciliation is disabled in hardware-free validation");
  WtpSteadyClock clock;
  auto now=[&]{return clock.now_ms();};
  PlainTcpStream stream(now);
  if (!stream.begin_open(settings.hostname,port)) throw std::runtime_error("Pi management connection failed");
  const auto deadline=now()+8000;
  while (stream.opening() && now()<deadline) {stream.poll_open();clock.wait_ms(2);}
  if (!stream.ready()) throw std::runtime_error("Pi management connection unavailable");
  // The WTP SRV target may be an endpoint-specific DNS alias. The Pi HTTP
  // guard admits its own interface addresses and UI hostname; address this
  // Plain LAN status request to the connected peer, then verify full WTP ID.
  std::string authority=stream.observation().address;
  if (authority.empty()) throw std::runtime_error("Pi management peer address unavailable");
  if (authority.find(':')!=std::string::npos) authority='['+authority+']';
  authority+=':'+std::to_string(port);
  const auto reply=wtp_json_http_exchange(stream,now,
      "GET /api/v1/host/wtp-endpoint HTTP/1.1\r\nHost: "+authority+
      "\r\nConnection: close\r\n\r\n",deadline,false);
  if (reply.status!=200) throw std::runtime_error("Pi takeover record is unavailable");
  auto state=strict_browser_json(reply.body);
  if (state.at("schema")!="wsprrypi-wtp-endpoint/1" ||
      state.at("device_id")!=settings.device_id ||
      state.value("revocation_unavailable",true))
    throw std::runtime_error("Pi takeover identity or record is unavailable");
  (void)wtp_fleet_decimal(state.at("revocation_generation"));
  return state;
}
}
