// SPDX-License-Identifier: MIT
#pragma once
#include "browser_api.hpp"
#include "httplib.hpp"
#include <optional>
#include <set>
#include <cctype>
namespace wsprrypi {
// The only query-bearing shared route. Validate encoded shape and decoded IDs
// before the common adapter admits it; every other resource remains query-free.
inline std::optional<std::string> wtp_fleet_action_query(const httplib::Request& r) {
  if(r.method!="GET" || r.path!="/api/control/v1/host/fleet/actions" ||
     !r.target.starts_with(r.path+"?ids=") || r.params.size()!=1 || !r.has_param("ids"))return {};
  const auto query=r.target.substr(r.path.size()+1);
  if(query.size()>2048 || query.find('&')!=std::string::npos)return {};
  for(std::size_t i=0;i<query.size();++i)if(query[i]=='%') {
    if(i+2>=query.size() || !std::isxdigit(static_cast<unsigned char>(query[i+1])) ||
       !std::isxdigit(static_cast<unsigned char>(query[i+2])))return {};
    i+=2;
  }
  const auto ids=r.get_param_value("ids");std::set<std::string> unique;std::size_t begin=0;
  while(true) {
    const auto end=ids.find(',',begin);const auto id=ids.substr(begin,end==std::string::npos?end:end-begin);
    if(id.size()!=32 || id.find_first_not_of("0123456789abcdef")!=std::string::npos || !unique.insert(id).second || unique.size()>32)return {};
    if(end==std::string::npos)break;
    begin=end+1;
  }
  return ids;
}
// Shared by the installed HTTP routes and in-process protected-route fixtures.
// Peer/Host/Origin authority is checked separately by BackendHttpGuard.
inline std::optional<BrowserRequest> wtp_browser_http_request(const httplib::Request &r) {
  if ((r.target != r.path && !wtp_fleet_action_query(r)) || r.body.size() > 32768 || r.get_header_value_count("Host") != 1) return {};
  for (const auto name : {"Origin","Content-Type","If-Match","X-WsprryPico-Request","X-WsprryPi-Member-Context","Sec-Fetch-Site"})
    if (r.get_header_value_count(name) > 1) return {};
  const auto fetch = r.get_header_value("Sec-Fetch-Site");
  return BrowserRequest{r.method,r.path,r.body,r.get_header_value("If-Match"),
      r.has_header("Origin") && r.get_header_value("Content-Type") == "application/json" &&
      r.get_header_value("X-WsprryPico-Request") == "1" && (fetch.empty() || fetch == "same-origin" || fetch == "none"),
      r.get_header_value("X-WsprryPi-Member-Context")};
}
}
