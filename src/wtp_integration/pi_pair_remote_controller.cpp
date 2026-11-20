// SPDX-License-Identifier: MIT
#include "pi_pair_remote_controller.hpp"
#include "application_recovery/restore_hold.hpp"
#include "pi_pair_peers.hpp"
#include "controller_contract.hpp"
#include "control_contract.hpp"
#include "configuration.hpp"
#include "privileged_network_runtime.hpp"
#include "pico_management_journal.hpp"
#include "pi_managed_records.hpp"
#include <set>
#include <openssl/rand.h>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw std::runtime_error("remote_management_unavailable");}
bool id(std::string_view value){return value.size()==32&&value.find_first_not_of("0123456789abcdef")==std::string_view::npos;}
bool enforced(){const auto policy=privileged_network_runtime_state();return policy.active_known&&
    policy.active==PrivilegedNetworkMode::enforced&&policy.configured==PrivilegedNetworkMode::enforced;}
PiPairHttpReply failure(unsigned status,const char *code){return {controller_target_error(status,code),{},{}};}
std::string context_id(){
    unsigned char bytes[16];require(RAND_bytes(bytes,sizeof(bytes))==1);std::string value;
    for(const auto byte:bytes){value+="0123456789abcdef"[byte>>4];value+="0123456789abcdef"[byte&15];}return value;
}
}
PiPairRemoteController::ContextLease::ContextLease(std::shared_ptr<Entry> source,std::string target)
    :published(std::move(source)),selected(std::make_shared<Entry>()),device(std::move(target)){
    std::unique_lock lock(published->publication,std::try_to_lock);require(lock.owns_lock());
    selected->selection=published->selection;selected->targets=published->targets;selected->observed=published->observed;
    selected->contexts=published->contexts;selected->wire_contexts=published->wire_contexts;
    selected->public_contexts=published->public_contexts;selected->epochs=published->epochs;
    selected->reads=published->reads;selected->recoveries=published->recoveries;
    for(const auto &row:published->targets)if(row["device_id"]==device)previous_target=row;
}
PiPairRemoteController::ContextLease::~ContextLease(){
    try{
        std::unique_lock lock(published->publication,std::try_to_lock);if(!lock.owns_lock()||!(published->selection==selected->selection))return;
        Json target;for(const auto &row:published->targets)if(row["device_id"]==device)target=row;
        if(target!=previous_target)return;
        const auto merge=[&](auto &destination,const auto &source){const auto found=source.find(device);if(found==source.end())destination.erase(device);else destination[device]=found->second;};
        merge(published->contexts,selected->contexts);merge(published->wire_contexts,selected->wire_contexts);
        merge(published->public_contexts,selected->public_contexts);merge(published->epochs,selected->epochs);
        merge(published->reads,selected->reads);merge(published->recoveries,selected->recoveries);
        for(auto &row:published->targets)if(row["device_id"]==device)for(const auto &current:selected->targets)if(current["device_id"]==device){row=current;break;}
    }catch(...){/* Cache publication cannot turn uncertainty into authority. */}
}
void PiPairRemoteController::authorization_observer(AuthorizationObserver observer){
    std::lock_guard lock(observer_mutex_);observer_=std::move(observer);
}
bool PiPairRemoteController::Selection::operator==(const Selection &other)const{
    return profile==other.profile&&generation==other.generation&&target==other.target&&identity==other.identity&&
        endpoint.host==other.endpoint.host&&endpoint.port==other.endpoint.port;
}
PiPairRemoteController::PiPairRemoteController(History history,PiPairAdmission &budget,std::string self,WtpCatalog &catalog,std::string directory,Active active)
    :catalog_(catalog),active_(std::move(active)),history_(std::move(history)),self_(std::move(self)),directory_(std::move(directory)),client_(budget){require(bool(history_)&&id(self_)&&!directory_.empty());}
PiPairRemoteController::PiPairRemoteController(History history,PiPairAdmission &budget,std::string self,WtpCatalog &catalog,std::string directory,LoopbackTest test,Active active)
    :catalog_(catalog),active_(std::move(active)),history_(std::move(history)),self_(std::move(self)),directory_(std::move(directory)),client_(budget,PiPairClient::Access::LoopbackTest),test_(std::move(test)){require(bool(history_)&&id(self_)&&!directory_.empty());}
PiPairRemoteController::Selection PiPairRemoteController::select(PiPairHistory &history,const std::string &profile){
    require(enforced()&&id(profile));const auto snapshot=history.snapshot();require(snapshot.peers.has_value());
    const auto peers=parse_pi_pair_peers(snapshot.peers->bytes,self_);
    for(const auto &row:peers["profiles"])if(row["profile_id"]==profile){
        require(row["stage"]=="active");const auto &pair=row["pair"];
        return {profile,row["profile_generation"].get<std::string>(),row["target_id"].get<std::string>(),
            {PiPairTlsPurpose::Management,pair["pair_id"].get<std::string>(),pair["authorization_generation"].get<std::string>(),pair["current"]["epoch"].get<std::string>()},
            {row["endpoint"]["host"].get<std::string>(),31418}};
    }
    throw std::runtime_error("remote_profile_unavailable");
}
std::shared_ptr<PiPairRemoteController::Entry> PiPairRemoteController::entry(const std::string &profile){
    std::lock_guard lock(entries_mutex_);const auto found=entries_.find(profile);if(found!=entries_.end())return found->second;
    require(entries_.size()<64);auto value=std::make_shared<Entry>();entries_.emplace(profile,value);return value;
}
std::shared_ptr<PicoManagementJournal> PiPairRemoteController::journal(const std::string &device){
    std::lock_guard lock(journals_mutex_);const auto found=journals_.find(device);if(found!=journals_.end())return found->second;
    require(journals_.size()<96);auto created=std::make_shared<PicoManagementJournal>(directory_,self_,device,
        catalog_.management_history_authority(active_?active_():std::optional<WtpSettings>{}));journals_.emplace(device,created);return created;
}
PiPairClientResult PiPairRemoteController::exchange(PiPairHistory &history,const Selection &selection,const std::string &path,const std::function<bool()> &cancelled,const httplib::Headers &headers){
    bool recorded=false;
    return client_.exchange(test_?test_->endpoint:selection.endpoint,selection.target,
        [&](const auto &identity){return history.outgoing_credential(selection.profile,identity);},selection.identity,
        [&](const auto &principal){
            try{
                if(cancelled()||!(select(history,selection.profile)==selection)||principal.self_id()!=self_||principal.peer_id()!=selection.target||principal.identity()!=selection.identity)return false;
                if(!recorded)recorded=history.record_outgoing_finished(selection.profile,principal);
                return recorded&&bool(history.outgoing_credential(selection.profile,principal.identity()));
            }catch(...){return false;}
        },"GET",path,{},headers,cancelled);
}
void PiPairRemoteController::validate_inventory(PiPairHistory &history,const std::string &profile,const Json &targets){
    // Publication is try-only: an in-flight peer cannot be assumed absent.
    // The caller owns its selected Entry; no mutex here is held over I/O.
    std::unique_lock publish(publication_mutex_,std::try_to_lock);require(publish.owns_lock());
    std::map<std::string,std::shared_ptr<Entry>> entries;
    {std::lock_guard lock(entries_mutex_);entries=entries_;}
    std::set<std::string> devices;
    for(const auto &target:targets)require(devices.insert(target["device_id"].get<std::string>()).second);
    for(const auto &[other,selected]:entries){
        if(other==profile)continue;
        std::unique_lock operation(selected->publication,std::try_to_lock);require(operation.owns_lock());
        try{if(!(select(history,other)==selected->selection)||std::chrono::steady_clock::now()-selected->observed>std::chrono::seconds(30))continue;}catch(...){continue;}
        for(const auto &target:selected->targets)require(devices.insert(target["device_id"].get<std::string>()).second);
    }
    require(devices.size()<=64);
}
bool PiPairRemoteController::observe(const std::string &profile,const std::function<bool()> &cancelled){
    try{
        require(bool(cancelled)&&!cancelled());const auto history=history_();require(bool(history));const auto selection=select(*history,profile);
        const auto selected=entry(profile);std::unique_lock operation(selected->operation,std::try_to_lock);if(!operation.owns_lock())return false;
        const auto now=std::chrono::steady_clock::now();
        {
            std::unique_lock publish(selected->publication,std::try_to_lock);if(!publish.owns_lock())return false;
            if(selected->attempted_once&&now-selected->attempted<std::chrono::seconds(1))return false;
            selected->attempted=now;selected->attempted_once=true;
            if(!(selected->selection==selection)){
                selected->contexts.clear();selected->wire_contexts.clear();selected->public_contexts.clear();selected->epochs.clear();selected->reads.clear();selected->recoveries.clear();
                selected->targets=Json::array();selected->observed={};selected->selection=selection;
            }
        }
        struct Withdrawal {
            Entry &entry;bool published{};
            ~Withdrawal(){if(!published){std::lock_guard lock(entry.publication);entry.targets=Json::array();entry.observed={};entry.contexts.clear();entry.wire_contexts.clear();entry.public_contexts.clear();entry.epochs.clear();entry.reads.clear();entry.recoveries.clear();}}
        } withdrawal{*selected};
        const auto result=exchange(*history,selection,"/api/control/v1/targets",cancelled);
        require(result.reply&&result.principal&&result.reply->payload.status==200&&!cancelled()&&select(*history,profile)==selection);
        const auto document=strict_browser_json(result.reply->payload.body);require(valid_controller_document(document,"targets"));
        std::set<std::string> devices;
        for(const auto &target:document["targets"]){
            require(target["device_id"]!=self_&&devices.insert(target["device_id"].get<std::string>()).second);
            if(target["product"]=="WsprryPi")require(target["device_id"]==selection.target);
            else require(target["product"]=="WsprryPico"&&target["device_id"]!=selection.target);
        }
        std::unique_lock publish(selected->publication,std::try_to_lock);require(publish.owns_lock());
        validate_inventory(*history,profile,document["targets"]);
        for(auto it=selected->contexts.begin();it!=selected->contexts.end();){
            bool retained=false;
            for(const auto &target:document["targets"])if(target==it->second["target"]){retained=true;break;}
            if(!retained){selected->wire_contexts.erase(it->first);selected->public_contexts.erase(it->first);selected->epochs.erase(it->first);selected->reads.erase(it->first);it=selected->contexts.erase(it);}else ++it;
        }
        selected->targets=document["targets"];selected->observed=std::chrono::steady_clock::now();withdrawal.published=true;return true;
    }catch(...){return false;}
}
PicoHttpResponse PiPairRemoteController::targets(){
    try{
        const auto history=history_();require(bool(history)&&enforced());std::map<std::string,std::shared_ptr<Entry>> entries;
        {std::lock_guard lock(entries_mutex_);entries=entries_;}
        Json targets=Json::array();std::set<std::string> devices;
        for(const auto &[profile,selected]:entries){
            std::unique_lock operation(selected->publication,std::try_to_lock);if(!operation.owns_lock())continue;
            try{if(!(select(*history,profile)==selected->selection)||std::chrono::steady_clock::now()-selected->observed>std::chrono::seconds(30))continue;}catch(...){continue;}
            for(const auto &target:selected->targets){
                require(devices.insert(target["device_id"].get<std::string>()).second);
                if(targets.size()>=64)return controller_target_error(429,"capacity");
                targets.push_back(target);
            }
        }
        Json document{{"contract","Wsprry-Control/1"},{"schema","targets"},{"targets",targets}};
        require(valid_controller_document(document,"targets")&&document.dump().size()<=32768);return {200,document.dump(),{}};
    }catch(...){return controller_target_error(503,"authority_pending");}
}
std::optional<std::string> PiPairRemoteController::resolve(const std::string &device,const std::string &hint){
    require(id(device)&&enforced());if(device==self_)return {};
    const auto rows=catalog_.remote_management(active_?active_():std::optional<WtpSettings>{});
    const auto history=history_();require(bool(history));
    // A failed pairing history cannot take over independently held local
    // Pico profiles, but an indexed remote identity cannot change owners.
    if(!history->available()){require(!rows.contains(device));return {};}
    const auto state=history->snapshot();require(state.peers.has_value());
    const auto peers=parse_pi_pair_peers(state.peers->bytes,self_);
    bool paired_hint=false,known=false;
    for(const auto &row:peers["profiles"]){
        if(row["profile_id"]==hint)paired_hint=true;
        if(row["target_id"]==device)known=true;
    }
    // Existing explicit direct-Pico profile selection retains its own owner.
    // It must still prove that profile and its independently issued context.
    if(!hint.empty()&&!paired_hint)return {};
    known=known||rows.contains(device);
    std::map<std::string,std::shared_ptr<Entry>> entries;
    {std::lock_guard lock(entries_mutex_);entries=entries_;}
    std::optional<std::string> result;
    for(const auto &[profile,selected]:entries){
        std::unique_lock lock(selected->publication,std::try_to_lock);require(lock.owns_lock());
        try{if(!(select(*history,profile)==selected->selection)||std::chrono::steady_clock::now()-selected->observed>std::chrono::seconds(30))continue;}catch(...){continue;}
        for(const auto &target:selected->targets)if(target["device_id"]==device){
            require(!result.has_value());result=profile;
        }
    }
    if(result){require(hint.empty()||hint==*result);return result;}
    require(!known&&!paired_hint);return {};
}
PiPairHttpReply PiPairRemoteController::read(const BrowserRequest &request,const std::string &profile,const std::function<bool()> &cancelled,const std::optional<std::string> &browser_context){
    try{
        require(bool(cancelled)&&!cancelled());if(request.method!="GET"||!request.body.empty())return failure(400,"invalid_shape");
        constexpr std::string_view prefix="/api/control/v1/targets/";
        if(!request.path.starts_with(prefix))return failure(404,"unsupported_resource");
        const auto suffix=std::string_view(request.path).substr(prefix.size());
        if(suffix.size()<34||!id(suffix.substr(0,32))||suffix[32]!='/')return failure(400,"invalid_shape");
        const auto device=std::string(suffix.substr(0,32)),resource=std::string(suffix.substr(33));
        auto target_admission=PicoManagementJournal::target_admission(directory_,device);if(!target_admission)return failure(409,"capacity");
        const auto history=history_();require(bool(history));const auto selection=select(*history,profile);ContextLease lease(entry(profile),device);const auto selected=lease.selected;
        std::unique_lock operation(selected->operation,std::try_to_lock);if(!operation.owns_lock())return failure(409,"capacity");
        if(!(selection==selected->selection)||std::chrono::steady_clock::now()-selected->observed>std::chrono::seconds(30))return failure(409,"profile_changed");
        bool pico=false,found=false;for(const auto &target:selected->targets)if(target["device_id"]==device){found=true;pico=target["product"]=="WsprryPico";}
        if(!found)return failure(404,"unsupported_resource");
        validate_inventory(*history,profile,selected->targets);
        const auto journal_owner=this->journal(device);
        const bool receipt=resource.starts_with("operations/")&&id(std::string_view(resource).substr(11));
        if(resource!="context"&&!receipt&&!(pico?resource.starts_with("pico/")&&controller_pico_route(resource.substr(5),"GET"):
            resource=="settings"||resource=="status"||resource=="migration"))return failure(404,"unsupported_resource");
        if(!pico&&browser_context&&!browser_context->empty())return failure(400,"invalid_shape");
        if(pico&&resource!="context"&&!receipt&&browser_context&&
            (!id(*browser_context)||!selected->public_contexts.contains(device)||selected->public_contexts.at(device)!=*browser_context||
             !selected->epochs.contains(device)||selected->epochs.at(device)!=target_admission->epoch()))return failure(409,"connection_changed");
        selected->reads.erase(device);
        if(resource=="context"){
            selected->contexts.erase(device);selected->wire_contexts.erase(device);selected->public_contexts.erase(device);selected->epochs.erase(device);
        }
        if(pico&&receipt){
            const auto live=exchange(*history,selection,std::string(prefix)+device+"/context",cancelled);
            require(live.reply&&live.principal&&live.reply->payload.status==200&&!cancelled()&&select(*history,profile)==selection);
            const auto view=strict_browser_json(live.reply->payload.body);
            require(valid_controller_document(view,"target-context")&&view["target"]["product"]=="WsprryPico"&&view["binding"]["device_id"]==device&&
                !view["authorization"].is_null()&&view["authorization"]["pair_id"]==selection.identity.pair_id&&
                view["authorization"]["authorization_generation"]==selection.identity.authorization_generation&&
                !view["gateway"].is_null()&&view["gateway"]["parent"]["device_id"]==selection.target);
            const auto op=resource.substr(11),start=std::string(prefix)+device+"/pico/";
            const auto saved=journal_owner->snapshot();
            for(const auto &record:saved["receipts"])if(record["receipt"]["operation_id"]==op){
                if(record["principal_id"]!=selection.identity.pair_id||record["authorization_generation"]!=selection.identity.authorization_generation)return failure(404,"operation_unknown");
                const auto route=record["route"].get<std::string>();require(route.starts_with(start));
                const auto prior=route.substr(start.size());require(!controller_pico_recovery_read(prior).empty());
                return {{200,journal_owner->receipt(op).dump(),{}},std::string(prefix)+device+"/operations/"+op,prior};
            }
            return failure(404,"operation_unknown");
        }
        httplib::Headers headers;
        if(pico&&resource!="context"){
            const auto token=selected->wire_contexts.find(device);
            if(token==selected->wire_contexts.end())return failure(409,"connection_changed");
            headers.emplace("X-WsprryPi-Target-Context",token->second);
        }
        auto result=exchange(*history,selection,request.path,cancelled,headers);
        if(!result.reply||!result.principal||cancelled()||!(select(*history,profile)==selection))return failure(503,"transport_unavailable");
        if(result.reply->payload.status==200&&resource=="context"){
            const auto context=strict_browser_json(result.reply->payload.body);require(valid_controller_document(context,"target-context")&&context["binding"]["device_id"]==device&&context["target"]["device_id"]==device);
            const auto &authorization=context["authorization"];require(!authorization.is_null()&&authorization["pair_id"]==selection.identity.pair_id&&authorization["authorization_generation"]==selection.identity.authorization_generation);
            if(pico)require(context["capabilities"].empty()&&!context["gateway"].is_null()&&context["gateway"]["parent"]["device_id"]==selection.target&&context["gateway"]["child"]==context["binding"]&&context["gateway"]["pair_id"]==selection.identity.pair_id&&context["gateway"]["authorization_generation"]==selection.identity.authorization_generation);
            else {
                require(context["gateway"].is_null()&&device==selection.target);
                for(const auto &capability:context["capabilities"])require(valid_pi_resource_capability(capability));
            }
            const RemoteManagementEnrollment enrollment(device,Json{{"parent_id",selection.target},{"profile_id",selection.profile},
                {"profile_generation",selection.generation},{"pair_id",selection.identity.pair_id},
                {"authorization_generation",selection.identity.authorization_generation},{"product",context["target"]["product"]}});
            require(history->observe_outgoing_management(selection.profile,selection.generation,*result.principal,[&]{
                require(!cancelled());(void)catalog_.enroll_remote_management(enrollment,active_?active_():std::optional<WtpSettings>{});
            }));
            for(auto &target:selected->targets)if(target["device_id"]==device)target=context["target"];
            selected->contexts[device]=context;
            if(authorization["renewal_required"]==true){
                AuthorizationObserver observer;{std::lock_guard lock(observer_mutex_);observer=observer_;}
                if(observer)observer(selection.profile,*result.principal,context);
            }
            selected->epochs[device]=journal_owner->target_epoch();
            if(pico){require(id(result.reply->context));selected->wire_contexts[device]=result.reply->context;selected->public_contexts[device]=context_id();}
        }
        if(pico&&resource!="context"&&result.reply->payload.status==200)require(result.reply->context==selected->wire_contexts.at(device));
        if(!pico&&resource!="context"&&result.reply->payload.status==200){
            const auto body=strict_browser_json(result.reply->payload.body);
            const auto family=receipt?"target-operation":resource=="settings"?"target-settings":resource=="status"?"target-status":"target-migration-read";
            require(valid_controller_document(body,family)&&body["binding"]["device_id"]==device);
            if(receipt){
                const auto op=resource.substr(11);require(body["operation_id"]==op);
                auto &journal=*journal_owner;
                const auto saved=journal.snapshot();
                for(const auto &record:saved["receipts"])if(record["receipt"]["operation_id"]==op){
                    require(record["principal_id"]==selection.identity.pair_id&&record["authorization_generation"]==selection.identity.authorization_generation);
                    if(!record["reconciled_revision"].is_null())return {{200,journal.receipt(op).dump(),{}},{},{}};
                    if(body["state"]=="committed"||body["state"]=="rejected")require(journal.confirm_native(op,selection.identity.pair_id,selection.identity.authorization_generation,result.reply->payload));
                }
            }else if(const auto context=selected->contexts.find(device);context!=selected->contexts.end())require(body["binding"]==context->second["binding"]);
        }
        if(resource!="context"&&!receipt&&result.reply->payload.status==200&&!result.reply->readback_revision.empty()){
            const auto context=selected->contexts.find(device);require(context!=selected->contexts.end());
            selected->reads[device]=Entry::Readback{resource,result.reply->readback_revision,result.reply->context,context->second["binding"],std::chrono::steady_clock::now()};
        }
        if(pico&&result.reply->payload.status==200){
            if(!selected->epochs.contains(device)||selected->epochs.at(device)!=journal_owner->target_epoch())return failure(409,"connection_changed");
            const auto token=selected->public_contexts.find(device);require(token!=selected->public_contexts.end());
            if(resource=="pico/management"){
                auto metadata=strict_browser_json(result.reply->payload.body);
                require(metadata.is_object()&&metadata.size()==6&&metadata.value("selection_id",Json(nullptr))==result.reply->context&&
                    metadata.value("contract",Json(nullptr))=="Wsprry-Control/1"&&metadata.value("carrier",Json(nullptr))=="usb_physical"&&metadata.value("connected",Json(nullptr))==true&&
                    metadata.contains("host")&&metadata["host"].is_string()&&metadata["host"].get_ref<const std::string &>().size()<=253&&metadata.contains("session")&&metadata["session"].is_null());
                metadata["selection_id"]=token->second;result.reply->payload.body=metadata.dump();
            }
            result.reply->context=token->second;
        }
        return *result.reply;
    }catch(...){return failure(503,"authority_pending");}
}
PiPairHttpReply PiPairRemoteController::write(const BrowserRequest &request,const std::string &profile,const std::function<bool()> &cancelled){
    if(restored_remote_authority_held())return failure(503,"recovery_required");
    try{
        require(bool(cancelled)&&!cancelled());
        if(!request.mutation_context)return failure(403,"csrf_failed");
        constexpr std::string_view prefix="/api/control/v1/targets/";
        if(!request.path.starts_with(prefix))return failure(404,"unsupported_resource");
        const auto suffix=std::string_view(request.path).substr(prefix.size());
        if(suffix.size()<34||!id(suffix.substr(0,32))||suffix[32]!='/')return failure(400,"invalid_shape");
        const auto device=std::string(suffix.substr(0,32)),resource=std::string(suffix.substr(33));
        std::string family;
        if(request.method=="PUT"&&resource=="settings")family="target-save";
        else if(request.method=="POST"){
            if(resource=="enable")family="target-enable";else if(resource=="disable")family="target-action";
            else if(resource=="stop")family="target-stop";else if(resource=="migration")family="target-migration";
            else if(resource=="migration/rollback")family="target-migration-rollback";
        }
        if(family.empty())return failure(404,"unsupported_resource");
        if(request.body.size()>32768)return failure(413,"payload_too_large");
        Json body;try{body=strict_browser_json(request.body,16);}catch(...){return failure(400,"invalid_json");}
        if(!valid_controller_document(body,family)||body["binding"]["device_id"]!=device)return failure(400,"invalid_shape");
        if(request.revision!="\""+body["expected_revision"].get<std::string>()+"\"")return failure(412,"revision_conflict");
        auto target_admission=PicoManagementJournal::target_admission(directory_,device);if(!target_admission)return failure(409,"capacity");
        const auto history=history_();require(bool(history));const auto selection=select(*history,profile);ContextLease lease(entry(profile),device);const auto selected=lease.selected;
        std::unique_lock operation(selected->operation,std::try_to_lock);if(!operation.owns_lock())return failure(409,"capacity");
        if(device!=selection.target||!(selection==selected->selection)||std::chrono::steady_clock::now()-selected->observed>std::chrono::seconds(30))return failure(409,"profile_changed");
        const auto context=selected->contexts.find(device);
        if(context==selected->contexts.end()||context->second["target"]["product"]!="WsprryPi"||body["binding"]!=context->second["binding"]||
            body["capability_revision"]!=context->second["capability_revision"])return failure(409,"connection_changed");
        const Json enrollment{{"parent_id",selection.target},{"profile_id",selection.profile},{"profile_generation",selection.generation},
            {"pair_id",selection.identity.pair_id},{"authorization_generation",selection.identity.authorization_generation},{"product","WsprryPi"}};
        const auto enrolled=[&]{const auto rows=catalog_.remote_management(active_?active_():std::optional<WtpSettings>{});return rows.contains(device)&&rows[device]==enrollment;};
        if(!enrolled())return failure(409,"profile_changed");
        const auto journal_owner=this->journal(device);
        if(!selected->epochs.contains(device)||selected->epochs.at(device)!=target_admission->epoch())return failure(409,"connection_changed");
        auto &journal=*journal_owner;
        const auto op=body["operation_id"].get<std::string>();
        const auto digest=pi_managed_request_digest(device,selection.identity.pair_id,selection.identity.authorization_generation,request.path,body);
        const auto recovery=[&](const Json &record){
            const auto route=record["route"].get<std::string>();const auto prior=route.substr(std::string(prefix).size()+33);
            const auto read=prior=="stop"?"status":prior=="migration"||prior=="migration/rollback"?"migration":"settings";
            return PiPairHttpReply{controller_target_error(503,"recovery_required"),std::string(prefix)+device+"/operations/"+record["receipt"]["operation_id"].get<std::string>(),read};
        };
        const auto saved=journal.snapshot();
        for(const auto &record:saved["receipts"])if(record["receipt"]["operation_id"]==op){
            if(record["principal_id"]!=selection.identity.pair_id||record["authorization_generation"]!=selection.identity.authorization_generation||
                record["route"]!=request.path||record["method"]!=request.method||record["receipt"]["binding"]!=body["binding"]||record["receipt"]["request_digest"]!=digest)
                return failure(409,"request_id_reuse");
            if(record["response"].is_null())return recovery(record);
            const auto live=exchange(*history,selection,std::string(prefix)+device+"/context",cancelled);
            require(live.reply&&live.principal&&live.reply->payload.status==200&&!cancelled()&&select(*history,profile)==selection);
            const auto view=strict_browser_json(live.reply->payload.body);
            require(valid_controller_document(view,"target-context")&&view["target"]["product"]=="WsprryPi"&&
                view["binding"]["device_id"]==device&&view["gateway"].is_null()&&!view["authorization"].is_null()&&
                view["authorization"]["pair_id"]==selection.identity.pair_id&&view["authorization"]["authorization_generation"]==selection.identity.authorization_generation);
            if(view["binding"]!=body["binding"]){selected->contexts.erase(device);selected->epochs.erase(device);}
            selected->reads.erase(device);
            return {{record["response"]["status"].get<unsigned>(),record["response"]["body"].dump(),record["response"]["etag"].get<std::string>()},{},{}};
        }
        for(const auto &record:saved["receipts"])if(resource!="stop"&&(record["receipt"]["state"]=="pending"||record["receipt"]["state"]=="unknown")&&record["reconciled_revision"].is_null())return recovery(record);
        selected->reads.erase(device);
        bool reserved=false,recorded=false,checkpoint=false;
        const auto result=client_.exchange(test_?test_->endpoint:selection.endpoint,selection.target,
            [&](const auto &identity){return history->outgoing_credential(profile,identity);},selection.identity,
            [&](const auto &principal){
                try{if(cancelled()||!(select(*history,profile)==selection)||principal.self_id()!=self_||principal.peer_id()!=selection.target||principal.identity()!=selection.identity)return false;
                    if(!recorded)recorded=history->record_outgoing_finished(profile,principal);
                    return recorded&&bool(history->outgoing_credential(profile,principal.identity()));}catch(...){return false;}
            },request.method,request.path,request.body,{{"If-Match",request.revision}},cancelled,[&](const auto &principal){
                auto guard=history->admit_outgoing_mutation(profile,selection.generation,principal,!checkpoint);
                if(!guard)return guard;
                checkpoint=true;
                if(cancelled()||!enrolled())return std::optional<PiPairMutationGuard>{};
                if(!reserved){
                    const auto known=resource=="stop"?journal.reserve_native_stop(selection.identity.pair_id,body["binding"],selection.identity.authorization_generation,op,digest):
                        journal.reserve(selection.identity.pair_id,request.path,request.method,body["binding"],selection.identity.authorization_generation,op,digest);
                    if(known)return std::optional<PiPairMutationGuard>{};
                    reserved=true;
                }
                return guard;
            });
        if(!reserved)return failure(503,"transport_unavailable");
        const bool current=result.reply&&result.principal&&!cancelled()&&select(*history,profile)==selection;
        const auto response=current?result.reply->payload:controller_target_error(503,"transport_unavailable");
        if(journal.finish(op,response))selected->contexts.erase(device);
        // HTTP success alone is not a native operation result. The journal's
        // strict receipt validation is authoritative for what can be shown.
        const auto completed=journal.snapshot();
        for(const auto &record:completed["receipts"])if(record["receipt"]["operation_id"]==op&&record["receipt"]["state"]=="unknown")return recovery(record);
        if(!current)return {controller_target_error(503,"recovery_required"),std::string(prefix)+device+"/operations/"+op,
            resource=="stop"?"status":resource=="migration"||resource=="migration/rollback"?"migration":"settings"};
        return *result.reply;
    }catch(...){return failure(503,"recovery_required");}
}
PiPairHttpReply PiPairRemoteController::write_pico(const BrowserRequest &request,const std::string &profile,const std::string &context,
    const std::string &operation_id,const std::function<bool()> &cancelled){
    if(restored_remote_authority_held())return failure(503,"recovery_required");
    try{
        require(bool(cancelled)&&!cancelled());if(!request.mutation_context)return failure(403,"csrf_failed");
        constexpr std::string_view prefix="/api/control/v1/targets/";
        if(!request.path.starts_with(prefix))return failure(404,"unsupported_resource");
        const auto suffix=std::string_view(request.path).substr(prefix.size());
        if(suffix.size()<39||!id(suffix.substr(0,32))||suffix.substr(32,6)!="/pico/")return failure(400,"invalid_shape");
        const auto device=std::string(suffix.substr(0,32)),resource=std::string(suffix.substr(38));
        if(request.method=="GET"||resource=="management"||!controller_pico_route(resource,request.method))return failure(404,"unsupported_resource");
        const bool mutation=resource!="pins/validate";
        if(!id(context)||(mutation?!id(operation_id):!operation_id.empty()))return failure(400,"invalid_shape");
        if(mutation&&request.revision.empty())return failure(428,"revision_required");
        if(request.body.size()>32768)return failure(413,"payload_too_large");
        Json body;try{body=strict_browser_json(request.body,16);}catch(...){return failure(400,"invalid_json");}
        if(!body.is_object())return failure(400,"invalid_shape");
        if(!body.contains("contract")||body["contract"]!="Wsprry-Control/1")return failure(400,"unsupported_contract");
        auto target_admission=PicoManagementJournal::target_admission(directory_,device);if(!target_admission)return failure(409,"capacity");
        const auto history=history_();require(bool(history));const auto selection=select(*history,profile);ContextLease lease(entry(profile),device);const auto selected=lease.selected;
        std::unique_lock operation(selected->operation,std::try_to_lock);if(!operation.owns_lock())return failure(409,"capacity");
        const auto current=selected->contexts.find(device);const auto wire=selected->wire_contexts.find(device);const auto public_context=selected->public_contexts.find(device);
        if(!(selection==selected->selection)||std::chrono::steady_clock::now()-selected->observed>std::chrono::seconds(30)||
            current==selected->contexts.end()||wire==selected->wire_contexts.end()||public_context==selected->public_contexts.end()||context!=public_context->second||
            !selected->epochs.contains(device)||selected->epochs.at(device)!=target_admission->epoch()||
            current->second["target"]["product"]!="WsprryPico")return failure(409,"connection_changed");
        const auto bound=current->second["binding"];
        const auto journal_owner=this->journal(device);
        const auto parent_context=wire->second;
        if(body.contains("target")){
            const auto &target=body["target"];
            if(!target.is_object()||target.size()!=3||!target.contains("scope")||!target.contains("device_id")||!target.contains("boot_id")||target["scope"]!="member")return failure(400,"invalid_shape");
            if(target["device_id"]!=device)return failure(409,"target_mismatch");
            if(target["boot_id"]!=bound["boot_id"])return failure(409,"boot_changed");
        }
        const auto &resources=current->second["gateway"]["resources"];
        if(std::find(resources.begin(),resources.end(),Json(resource))==resources.end())return failure(404,"unsupported_resource");
        const Json enrollment{{"parent_id",selection.target},{"profile_id",profile},{"profile_generation",selection.generation},
            {"pair_id",selection.identity.pair_id},{"authorization_generation",selection.identity.authorization_generation},{"product","WsprryPico"}};
        const auto enrolled=[&]{const auto rows=catalog_.remote_management(active_?active_():std::optional<WtpSettings>{});return rows.contains(device)&&rows[device]==enrollment;};
        if(!enrolled())return failure(409,"profile_changed");
        auto &journal=*journal_owner;
        const auto digest=PicoManagementJournal::digest(Json{{"principal",selection.identity.pair_id},{"route",request.path},{"method",request.method},
            {"binding",bound},{"authorization_generation",selection.identity.authorization_generation},{"revision",request.revision},
            {"gateway",current->second["gateway"]},{"body",body}}.dump());
        const auto recovery=[&](const Json &record){
            const auto route=record["route"].get<std::string>(),start=std::string(prefix)+device+"/pico/";require(route.starts_with(start));
            const auto prior=route.substr(start.size());require(!controller_pico_recovery_read(prior).empty());
            return PiPairHttpReply{controller_target_error(503,"recovery_required"),std::string(prefix)+device+"/operations/"+record["receipt"]["operation_id"].get<std::string>(),prior,context,operation_id};
        };
        if(mutation){
            const auto saved=journal.snapshot();
            for(const auto &record:saved["receipts"])if(record["receipt"]["operation_id"]==operation_id){
                if(record["principal_id"]!=selection.identity.pair_id||record["authorization_generation"]!=selection.identity.authorization_generation||
                    record["route"]!=request.path||record["method"]!=request.method||record["receipt"]["binding"]!=bound||record["receipt"]["request_digest"]!=digest)return failure(409,"request_id_reuse");
                if(record["response"].is_null())return recovery(record);
                // Historical native payloads require current actual gateway
                // authorization, but never resend the original mutation.
                const auto live=exchange(*history,selection,std::string(prefix)+device+"/context",cancelled);
                require(live.reply&&live.principal&&live.reply->payload.status==200&&select(*history,profile)==selection);
                const auto view=strict_browser_json(live.reply->payload.body);
                require(valid_controller_document(view,"target-context")&&view["target"]["product"]=="WsprryPico"&&view["binding"]["device_id"]==device&&
                    !view["authorization"].is_null()&&view["authorization"]["pair_id"]==selection.identity.pair_id&&
                    view["authorization"]["authorization_generation"]==selection.identity.authorization_generation&&
                    !view["gateway"].is_null()&&view["gateway"]["parent"]["device_id"]==selection.target);
                // Exact historical replies retain the accepted binding. A
                // fresh current authorization is not permission to adopt the
                // retired context for another write.
                if(view["binding"]!=bound||live.reply->context!=parent_context){selected->contexts.erase(device);selected->wire_contexts.erase(device);selected->public_contexts.erase(device);selected->epochs.erase(device);}
                selected->reads.erase(device);
                return {{record["response"]["status"].get<unsigned>(),record["response"]["body"].dump(),record["response"]["etag"].get<std::string>()},{},{},context,operation_id};
            }
            for(const auto &record:saved["receipts"])if((record["receipt"]["state"]=="pending"||record["receipt"]["state"]=="unknown")&&record["reconciled_revision"].is_null())return recovery(record);
        }
        selected->reads.erase(device);bool reserved=false,recorded=false,checkpoint=false;
        httplib::Headers headers{{"If-Match",request.revision},{"X-WsprryPi-Target-Context",parent_context}};
        if(mutation)headers.emplace("X-WsprryPi-Operation-ID",operation_id);
        const auto result=client_.exchange(test_?test_->endpoint:selection.endpoint,selection.target,
            [&](const auto &identity){return history->outgoing_credential(profile,identity);},selection.identity,
            [&](const auto &principal){
                try{if(cancelled()||!(select(*history,profile)==selection)||principal.self_id()!=self_||principal.peer_id()!=selection.target||principal.identity()!=selection.identity)return false;
                    if(!recorded){recorded=history->record_outgoing_finished(profile,principal);}
                    return recorded;}catch(...){return false;}
            },request.method,request.path,request.body,headers,cancelled,[&](const auto &principal){
                auto guard=history->admit_outgoing_mutation(profile,selection.generation,principal,!checkpoint);if(!guard)return guard;checkpoint=true;
                if(cancelled()||!enrolled())return std::optional<PiPairMutationGuard>{};
                if(mutation&&!reserved){if(journal.reserve(selection.identity.pair_id,request.path,request.method,bound,selection.identity.authorization_generation,operation_id,digest))return std::optional<PiPairMutationGuard>{};reserved=true;}
                return guard;
            });
        const bool replied=result.reply&&result.principal&&!cancelled()&&select(*history,profile)==selection;
        if(mutation){
            if(!reserved)return failure(503,"transport_unavailable");
            auto response=replied?result.reply->payload:controller_target_error(503,"transport_unavailable");
            // An authenticated, closed pre-dispatch refusal points to the
            // parent's earlier operation. It is not uncertainty about this
            // new command, and reveals no older receipt or command body.
            bool parent_recovery=false;
            if(replied&&response.status==503){
                const auto earlier_prefix=std::string(prefix)+device+"/operations/";
                const auto &reply=*result.reply;
                const auto error=strict_browser_json(response.body);
                if(valid_controller_document(error,"target-error")&&error["error"]["code"]=="recovery_required"&&
                    reply.location.starts_with(earlier_prefix)&&id(std::string_view(reply.location).substr(earlier_prefix.size()))&&
                    reply.location!=earlier_prefix+operation_id&&!controller_pico_recovery_read(reply.recovery_resource).empty()){
                    selected->recoveries[device]={reply.location.substr(earlier_prefix.size()),reply.recovery_resource};
                    response.status=409;parent_recovery=true;
                }
            }
            // A successful native save can retire its old parent context as
            // its capability/authorization generation changes. Confirm only
            // this frozen operation and closed native result. Never adopt the
            // returned handle as a new write permission; later read/replay
            // checks obtain the actual current context independently.
            if(response.status>=200&&response.status<300&&(!result.reply||!id(result.reply->context)||result.reply->operation!=operation_id||
                !pico_management_response_valid(resource,request.method,response,bound,request.body)))
                response=controller_target_error(503,"recovery_required");
            if(response.status>=400&&response.status<500){
                bool closed=false;try{closed=valid_controller_document(strict_browser_json(response.body,16),"target-error");}catch(...){}
                if(!closed)response=controller_target_error(503,"recovery_required");
            }
            if(journal.finish(operation_id,response)){selected->contexts.erase(device);selected->wire_contexts.erase(device);}
            if(parent_recovery){auto reply=*result.reply;reply.payload=response;reply.context=context;return reply;}
            const auto completed=journal.snapshot();for(const auto &record:completed["receipts"])if(record["receipt"]["operation_id"]==operation_id&&record["receipt"]["state"]=="unknown")return recovery(record);
        }
        if(!replied)return failure(503,"transport_unavailable");
        auto reply=*result.reply;reply.context=context;if(mutation)reply.operation=operation_id;return reply;
    }catch(...){return failure(503,"recovery_required");}
}
PiPairHttpReply PiPairRemoteController::review(const BrowserRequest &request,const std::string &profile,const std::function<bool()> &cancelled){
    if(restored_remote_authority_held())return failure(503,"recovery_required");
    try{
        require(bool(cancelled)&&!cancelled());if(!request.mutation_context)return failure(403,"csrf_failed");
        constexpr std::string_view prefix="/api/control/v1/targets/";
        if(request.method!="POST"||!request.path.starts_with(prefix))return failure(404,"unsupported_resource");
        const auto suffix=std::string_view(request.path).substr(prefix.size());
        if(suffix.size()!=83||!id(suffix.substr(0,32))||suffix.substr(32,12)!="/operations/"||!id(suffix.substr(44,32))||suffix.substr(76)!="/review")return failure(400,"invalid_shape");
        const auto device=std::string(suffix.substr(0,32)),op=std::string(suffix.substr(44,32));
        if(request.body.size()>32768)return failure(413,"payload_too_large");
        Json body;try{body=strict_browser_json(request.body,16);}catch(...){return failure(400,"invalid_json");}
        if(!valid_controller_document(body,"target-operation-review")||body["operation_id"]!=op||body["binding"]["device_id"]!=device)return failure(400,"invalid_shape");
        auto target_admission=PicoManagementJournal::target_admission(directory_,device);if(!target_admission)return failure(409,"capacity");
        const auto history=history_();require(bool(history));const auto selection=select(*history,profile);ContextLease lease(entry(profile),device);const auto selected=lease.selected;
        std::unique_lock operation(selected->operation,std::try_to_lock);if(!operation.owns_lock())return failure(409,"capacity");
        const auto context=selected->contexts.find(device);
        const auto read=selected->reads.find(device);
        if(!(selection==selected->selection)||std::chrono::steady_clock::now()-selected->observed>std::chrono::seconds(30)||
            context==selected->contexts.end()||read==selected->reads.end()||read->second.binding!=body["binding"]||read->second.token!=body["readback_revision"].get_ref<const std::string &>()||
            std::chrono::steady_clock::now()-read->second.at>std::chrono::seconds(30))return failure(409,"recovery_required");
        const bool pico=context->second["target"]["product"]=="WsprryPico";
        const auto journal_owner=this->journal(device);
        if(!selected->epochs.contains(device)||selected->epochs.at(device)!=target_admission->epoch())return failure(409,"connection_changed");
        const auto evidence=read->second;selected->reads.erase(read);
        auto &journal=*journal_owner;
        const auto saved=journal.snapshot();bool found=false;
        for(const auto &record:saved["receipts"])if(record["receipt"]["operation_id"]==op){
            if(record["receipt"]["state"]!="pending"&&record["receipt"]["state"]!="unknown")return failure(409,"recovery_required");
            const auto route=record["route"].get<std::string>(),start=std::string(prefix)+device+"/";require(route.starts_with(start));
            const auto resource=route.substr(start.size());
            const auto expected=pico?"pico/"+std::string(controller_pico_recovery_read(resource.starts_with("pico/")?resource.substr(5):std::string{})):
                resource=="stop"?"status":resource=="migration"||resource=="migration/rollback"?"migration":"settings";
            require(evidence.resource==expected);found=true;
        }
        if(!found){
            // With no controller-owned operation there is no local barrier to
            // clear. The authenticated parent alone validates its durable old
            // operation/resource mapping; its acknowledgement fences only the
            // fresh physical context. This also survives loss of RAM hints.
            if(!pico||!evidence.resource.starts_with("pico/"))return failure(404,"operation_unknown");
            const auto resource=evidence.resource.substr(5);
            if(!controller_pico_route(resource,"GET")||controller_pico_recovery_read(resource)!=resource)return failure(409,"recovery_required");
            if(const auto recovery=selected->recoveries.find(device);recovery!=selected->recoveries.end()&&
                (recovery->second.operation!=op||resource!=controller_pico_recovery_read(recovery->second.resource)))return failure(409,"recovery_required");
        }
        httplib::Headers headers;if(pico){if(!id(evidence.wire_context))return failure(409,"connection_changed");headers.emplace("X-WsprryPi-Target-Context",evidence.wire_context);}
        bool recorded=false,checkpoint=false;
        const auto result=client_.exchange(test_?test_->endpoint:selection.endpoint,selection.target,
            [&](const auto &identity){return history->outgoing_credential(profile,identity);},selection.identity,
            [&](const auto &principal){
                try{if(cancelled()||!(select(*history,profile)==selection)||principal.self_id()!=self_||principal.peer_id()!=selection.target||principal.identity()!=selection.identity)return false;
                    if(!recorded){recorded=history->record_outgoing_finished(profile,principal);}
                    return recorded;}catch(...){return false;}
            },"POST",request.path,request.body,headers,cancelled,[&](const auto &principal){auto guard=history->admit_outgoing_mutation(profile,selection.generation,principal,!checkpoint);if(guard)checkpoint=true;return guard;});
        if(!result.reply||!result.principal||cancelled()||!(select(*history,profile)==selection))return failure(503,"recovery_required");
        if(result.reply->payload.status!=200)return *result.reply;
        const auto acknowledgement=strict_browser_json(result.reply->payload.body);
        require(valid_controller_document(acknowledgement,"target-operation-reviewed")&&acknowledgement["operation_id"]==op&&acknowledgement["readback_revision"]==evidence.token);
        for(const auto *field:{"device_id","boot_id","profile_generation"})require(acknowledgement["binding"][field]==body["binding"][field]);
        require(acknowledgement["binding"]["connection_generation"]!=body["binding"]["connection_generation"]);
        require(history->observe_outgoing_management(profile,selection.generation,*result.principal,[&]{
            require(!cancelled());const auto rows=catalog_.remote_management(active_?active_():std::optional<WtpSettings>{});
            require(rows.contains(device)&&rows[device]["parent_id"]==selection.target&&rows[device]["profile_id"]==profile&&rows[device]["profile_generation"]==selection.generation&&
                rows[device]["pair_id"]==selection.identity.pair_id&&rows[device]["authorization_generation"]==selection.identity.authorization_generation);
            if(found)(void)journal.reconcile(op,acknowledgement["binding"],evidence.token);
            else target_admission->retire();
        }));
        selected->contexts.erase(device);selected->wire_contexts.erase(device);selected->public_contexts.erase(device);selected->epochs.erase(device);
        selected->recoveries.erase(device);
        auto reply=*result.reply;reply.context.clear();return reply;
    }catch(...){return failure(503,"recovery_required");}
}
}
