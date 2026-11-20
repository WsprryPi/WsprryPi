// SPDX-License-Identifier: MIT
#include "pi_pair_machine_backend.hpp"
#include "application_recovery/restore_hold.hpp"
#include "browser_http.hpp"
#include "controller_contract.hpp"
#include "pi_pair_contract.hpp"
#include "fleet_runtime.hpp"
#include <vector>
#include <algorithm>
#include <set>
#include "usb_discovery.hpp"
#include "wtp_settings_json.hpp"
#include <chrono>
#include <thread>
namespace wsprrypi {
PiPairMachineBackend::PiPairMachineBackend(History history,WtpCatalog &catalog,std::shared_ptr<PicoTargetController> picos,
    PicoTargetController::Active active,std::shared_ptr<PiManagedHost> host)
    :history_(std::move(history)),catalog_(catalog),picos_(std::move(picos)),active_(std::move(active)),host_(std::move(host)){
    if(!history_||!picos_||!active_)throw std::invalid_argument("pairing_application_owners_required");
}
PiPairMachineBackend::PiPairMachineBackend(History history,WtpCatalog &catalog,std::shared_ptr<PicoTargetController> picos,
    PicoTargetController::Active active,std::shared_ptr<PiManagedHost> host,Fixtures fixtures)
    :PiPairMachineBackend(std::move(history),catalog,std::move(picos),std::move(active),std::move(host)){
    if(!fixtures.usb_inventory||!fixtures.usb_channel)throw std::invalid_argument("pairing_usb_fixture_sources_required");
    fixtures_=std::move(fixtures);
}
PiPairMachineBackend::Discovery PiPairMachineBackend::discover(const PiPairPrincipal &principal,PiPairHistory &history,
    const PiPairMachineService::Cancellation &cancelled){
    std::unique_lock discovery(discovery_,std::try_to_lock);if(!discovery.owns_lock()||(cancelled&&cancelled()))return {};
    const auto inventory=fixtures_.usb_inventory?fixtures_.usb_inventory():pico_management_usb_discovery();
    if(!inventory.is_object()||inventory.size()!=4||inventory.value("scope",std::string{})!="pico-management-usb/1"||
        !inventory.contains("available")||!inventory["available"].is_boolean()||!inventory.contains("reason")||!inventory["reason"].is_string()||
        !inventory.contains("candidates")||!inventory["candidates"].is_array()||inventory["candidates"].size()>64)throw std::runtime_error("pairing_usb_inventory_unavailable");
    if(inventory["available"]!=true)return {};
    const auto [before,revision]=catalog_.snapshot(active_());(void)revision;
    std::set<std::string> handles,serials;std::vector<std::pair<std::string,WtpSettings>> observations;
    for(const auto &row:inventory["candidates"]){
        if(!row.is_object()||row.size()!=5||row.value("binding",std::string{})!="usb"||row.value("state",std::string{})!="online"||
            !row.contains("id")||!row["id"].is_string()||!row.contains("instance")||!row["instance"].is_string()||!row.contains("settings"))throw std::runtime_error("pairing_usb_inventory_shape");
        auto settings=parse_management_profile_settings(row["settings"],true);const auto handle=row["id"].get<std::string>();
        if(settings.transport!="usb"||!handle.starts_with("usb-")||handle.size()!=36||handle.substr(4).find_first_not_of("0123456789abcdef")!=std::string::npos||!handles.insert(handle).second||!serials.insert(settings.usb_serial).second)throw std::runtime_error("pairing_usb_inventory_binding");
        validate_discovered_usb(inventory,handle,settings);observations.emplace_back(handle,std::move(settings));
    }
    if(!observations.empty()){
        discovery_cursor_%=observations.size();
        std::rotate(observations.begin(),observations.begin()+discovery_cursor_,observations.end());
        discovery_cursor_=(discovery_cursor_+1)%observations.size();
    }
    for(auto &observation:observations){
        auto settings=observation.second;
        bool known=false;
        for(const auto &row:before["profiles"]){const auto saved=parse_wtp_settings(row.at("settings"),true);
            if(saved.transport=="usb"&&saved.usb_serial==settings.usb_serial){known=true;break;}}
        // The current catalog requires every USB profile to have a verified
        // identity. Preserve its explicit locator and transmitter settings.
        if(known)continue;
        if(before["profiles"].size()>=64)return {};
        auto channel=fixtures_.usb_channel?fixtures_.usb_channel(settings):pico_usb_management_channel(settings);
        if(!channel||!channel->matches(settings))throw std::runtime_error("pairing_usb_channel_unavailable");
        const auto deadline=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())+24000;
        {
            std::unique_lock physical(channel->mutex,std::defer_lock);
            while(!physical.try_lock()){
                if((cancelled&&cancelled())||std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()>=static_cast<std::int64_t>(deadline))return {};
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            channel->cancellation=std::make_shared<std::atomic_bool>(false);
            if(!channel->ensure({},false,deadline,cancelled))return {};
            const auto observed=channel->client->device();
            if(!settings.device_id.empty()&&settings.device_id!=observed)return {};
            settings.device_id=observed;
        }
        if(settings.device_id==principal.self_id())return {};
        validate_wtp_settings(settings,true);
        PicoManagementTarget probe(settings,PicoManagementTarget::Fixtures{{},{},channel});
        auto cancellation=probe.scoped_cancellation(cancelled);
        if(probe.connect("usb_physical").status!=200)return {};
        const auto proof=probe.binding();
        if(proof["connected"]!=true||proof["carrier"]!="usb_physical"||proof["device_id"]!=settings.device_id)return {};
        if(cancelled&&cancelled())return {};
        std::unique_lock catalog_gate(pico_management_catalog_gate(),std::try_to_lock);if(!catalog_gate.owns_lock())return {};
        std::string profile;
        const bool published=history.observe_management(principal,[&]{
            if(cancelled&&cancelled())return;
            const auto [current,etag]=catalog_.snapshot(active_());
            for(const auto &row:current["profiles"]){const auto saved=parse_wtp_settings(row.at("settings"),true);
                if(saved.device_id==settings.device_id||saved.usb_serial==settings.usb_serial){
                    if(saved==settings&&!row.at("legacy_unverified").get<bool>())profile=row.at("id").get<std::string>();
                    return;
                }}
            const auto [after,next]=catalog_.mutate({{"operation","add"},{"name","Attached Pico "+settings.device_id.substr(0,8)},
                {"method","manual"},{"discovery_id",""},{"settings",management_profile_settings_json(settings)},{"consent_plain",false}},etag,active_());(void)next;
            for(const auto &row:after["profiles"])if(parse_wtp_settings(row.at("settings"),true)==settings)profile=row.at("id").get<std::string>();
        });
        return published?Discovery{profile,std::move(channel)}:Discovery{};
    }
    return {};
}
PicoHttpResponse PiPairMachineBackend::targets(const PiPairPrincipal &principal,const PiPairMachineService::Cancellation &cancelled){
    try{
        const auto history=history_();
        if(!history||(cancelled&&cancelled())||!history->management_authorization(principal))return controller_target_error(403,"unauthorized");
        // Negotiate at most one existing physical profile per observation. The
        // complete incoming cancellation/deadline reaches native USB I/O; a
        // large catalog cannot queue 64 bounded exchanges behind one request.
        auto discovered=discover(principal,*history,cancelled);
        std::vector<std::string> candidates;
        const auto [catalog,revision]=catalog_.snapshot(active_());(void)revision;
        for(const auto &row:catalog["profiles"]){
            const auto settings=row.find("settings");
            if(settings!=row.end()&&settings->is_object()&&settings->value("Transport",std::string{})=="usb")
                candidates.push_back(row.at("id").get<std::string>());
        }
        if(!discovered.profile.empty()){
            (void)picos_->negotiate_attached_usb(discovered.profile,principal,*history,cancelled);
        }else if(!candidates.empty()){
            std::string profile;
            {std::lock_guard lock(inventory_);cursor_%=candidates.size();profile=candidates[cursor_];cursor_=(cursor_+1)%candidates.size();}
            if(!cancelled||!cancelled())(void)picos_->negotiate_attached_usb(profile,principal,*history,cancelled);
        }
        if((cancelled&&cancelled())||!history->management_authorization(principal))return controller_target_error(403,"unauthorized");
        const auto result=picos_->handle({"GET","/api/control/v1/targets",{},{},false}, {},{},{},principal,*history,cancelled);
        if(result.response.status!=200)return normalize_controller_error(result.response);
        auto document=strict_browser_json(result.response.body);
        if(!valid_controller_document(document,"targets"))return controller_target_error(503,"invalid_shape");
        if(host_){
            if(host_->device_id()!=principal.self_id())return controller_target_error(409,"target_mismatch");
            const auto native=host_->target();
            for(const auto &child:document["targets"])if(child["device_id"]==native["device_id"])return controller_target_error(409,"target_mismatch");
            document["targets"].push_back(native);
        }
        if(!valid_controller_document(document,"targets")||document.dump().size()>32768)return controller_target_error(429,"capacity");
        return {200,document.dump(),{}};
    }catch(...){return controller_target_error(503,"storage_unavailable");}
}
PiPairHttpReply PiPairMachineBackend::request(const PiPairPrincipal &principal,const httplib::Request &request,
    const PiPairMachineService::Cancellation &cancelled){
    const auto failure=[&](unsigned status,const char *code){
        if(principal.identity().purpose!=PiPairTlsPurpose::Retirement)return PiPairHttpReply{controller_target_error(status,code),{},{}};
        const char *closed=status==503?"storage_unavailable":status==429?"capacity":status==400?"invalid_shape":"invalid_proof";
        return PiPairHttpReply{{status,nlohmann::json{{"contract","Wsprry-Pair/1"},{"schema","result"},
            {"pair_id",principal.identity().pair_id},{"state","rejected"},{"code",closed},{"retryable",status==503||status==429}}.dump(),{}},{},{}};
    };
    try{
        if(request.method!="GET"&&restored_remote_authority_held())return failure(503,"recovery_required");
        const auto history=history_();const auto browser=wtp_browser_http_request(request);
        if(!history||!browser||(cancelled&&cancelled()))return failure(403,"unauthorized");
        if(principal.identity().purpose==PiPairTlsPurpose::Retirement){
            const bool status=request.path=="/api/pair/v1/retirement/status";
            if(request.method!="POST"||(!status&&request.path!="/api/pair/v1/retirement"))return failure(403,"unauthorized");
            const auto body=parse_pi_pair_document(request.body,status?"retirement-status":"retirement");
            const auto authorized=history->authorize_retirement(principal,body);if(!authorized)return failure(403,"unauthorized");
            auto result=wtp_fleet_retirement(*authorized,*history,cancelled);
            const auto document=strict_browser_json(result.body,8);
            if((result.status==200&&valid_pi_pair_document(document,"retired"))||
                (result.status==202&&valid_pi_pair_document(document,"operation"))||valid_pi_pair_document(document,"result"))return {std::move(result),{},{}};
            return failure(result.status>=400?result.status:503,"invalid_shape");
        }
        if(!history->management_authorization(principal))return failure(403,"unauthorized");
        if(request.path.starts_with("/api/pair/v1/")){
            if(!host_||host_->device_id()!=principal.self_id()){
                const bool absent=!host_&&request.method=="GET"&&request.path.starts_with("/api/pair/v1/operations/");
                return {{absent?404U:503U,nlohmann::json{{"contract","Wsprry-Pair/1"},{"schema","result"},
                    {"pair_id",principal.identity().pair_id},{"state","rejected"},{"code",absent?"operation_unknown":"storage_unavailable"},{"retryable",false}}.dump(),{}},{},{}};
            }
            return {host_->management(*browser,principal,*history),{},{}};
        }
        const auto native="/api/control/v1/targets/"+principal.self_id()+"/";
        if(request.path.starts_with(native)){
            // Controller-only is an explicit zero-output role. A native route
            // can neither initialize a transmitter nor select another host.
            if(!host_)return failure(404,"unsupported_resource");
            if(host_->device_id()!=principal.self_id())return failure(409,"target_mismatch");
            for(const auto *name:{"X-WsprryPi-Profile-ID","X-WsprryPi-Target-Context","X-WsprryPi-Operation-ID"})
                if(request.has_header(name))return failure(400,"invalid_shape");
            PiPairHttpReply reply;
            reply.payload=host_->management(*browser,principal,*history,&reply.readback_revision);
            return reply;
        }
        // Profile IDs belong to this Pi's private catalog, not to the remote
        // controller. Resolve only a previously negotiated child device ID.
        if(request.has_header("X-WsprryPi-Profile-ID"))return failure(400,"invalid_shape");
        const auto reply=picos_->handle(*browser,{},request.get_header_value("X-WsprryPi-Target-Context"),
            request.get_header_value("X-WsprryPi-Operation-ID"),principal,*history,cancelled);
        return {normalize_controller_error(reply.response),reply.location,reply.recovery_resource,reply.context,reply.operation,reply.readback_revision};
    }catch(...){return failure(503,"storage_unavailable");}
}
}
