// SPDX-License-Identifier: MIT
#include "browser_api.hpp"
#include "pi_pair_history.hpp"
#include "controller_contract.hpp"
#include "control_contract.hpp"
namespace wsprrypi {
PicoHttpResponse WtpBrowserApi::handle(const BrowserRequest &request,const PiPairPrincipal &principal,PiPairHistory &history){
    constexpr std::string_view member="/api/control/v1/member/";
    const auto resource=request.path.starts_with(member)?request.path.substr(member.size()):
        request.path=="/api/control/v1/network"?std::string("network"):std::string{};
    if(resource.empty()||resource=="management"||!controller_pico_route(resource,request.method))
        return controller_target_error(404,"unsupported_resource");
    if(!history.management_authorization(principal))return controller_target_error(403,"unauthorized");
    return handle_impl(request,true);
}
}
