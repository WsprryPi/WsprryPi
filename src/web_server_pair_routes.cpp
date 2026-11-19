// SPDX-License-Identifier: MIT
#include "web_server_routes.hpp"
#include "wtp_integration/pi_pair_local_service.hpp"
#include "wtp_integration/controller_contract.hpp"
namespace web_server_routes {
void register_pairing(httplib::Server &server,
    std::function<std::shared_ptr<wsprrypi::PiPairLocalService>()> selected,NetworkSnapshotProvider snapshot){
    if(!selected||!snapshot)throw std::invalid_argument("pairing_route_owner_required");
    const auto handler=[selected=std::move(selected),snapshot=std::move(snapshot)](const httplib::Request &request,httplib::Response &response){
        response.set_header("Cache-Control","no-store");response.set_header("Connection","close");
        auto result=wsprrypi::pi_pair_local_unavailable(request);
        try{
            if(const auto owner=selected()){
                const auto reply=owner->handle(request,snapshot());
                if(reply)result=*reply;
                else result=wsprrypi::controller_target_error(404,"unsupported_resource");
            }
        }catch(...){result=wsprrypi::pi_pair_local_unavailable(request);}
        response.status=static_cast<int>(result.status);
        if(!result.etag.empty())response.set_header("ETag",result.etag);
        response.set_content(result.body,"application/json");
    };
    server.Get("/api/control/v1/host/pairing",handler);
    server.Get("/api/control/v1/host/pairing/discovery",handler);
    server.Get(R"(/api/control/v1/host/pairing/operations/[0-9a-f]{32})",handler);
    for(const auto *path:{"/api/control/v1/host/pairing/window","/api/control/v1/host/pairing/remove","/api/control/v1/host/pairing/connect"})server.Post(path,handler);
    server.Get("/api/pair/v1/window",handler);
    for(const auto *path:{"/api/pair/v1/hello","/api/pair/v1/exchange","/api/pair/v1/confirm","/api/pair/v1/commit"})server.Post(path,handler);
}
}
