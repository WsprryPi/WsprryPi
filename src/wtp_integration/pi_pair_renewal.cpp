// SPDX-License-Identifier: MIT
#include "pi_pair_renewal.hpp"
#include "pi_pair_peers.hpp"
#include "pi_pair_contract.hpp"
#include "controller_contract.hpp"
#include "privileged_network_runtime.hpp"
#include <openssl/rand.h>
#include <algorithm>
#include <array>
#include <stdexcept>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw std::runtime_error("pairing_renewal_unavailable");}
bool id(const std::string &value){return value.size()==32&&value.find_first_not_of("0123456789abcdef")==std::string::npos;}
std::string fresh(){std::array<unsigned char,16> bytes{};require(RAND_bytes(bytes.data(),bytes.size())==1);std::string value;
    for(const auto byte:bytes){value+="0123456789abcdef"[byte>>4];value+="0123456789abcdef"[byte&15];}return value;}
}
PiPairRenewal::PiPairRenewal(PiPairHistory &history,ManagementBrowserSessions &sessions,PiPairAdmission &budget,std::string self)
    :history_(history),sessions_(sessions),self_(std::move(self)),client_(budget){require(id(self_));}
PiPairRenewal::PiPairRenewal(PiPairHistory &history,ManagementBrowserSessions &sessions,PiPairAdmission &budget,std::string self,LoopbackTest test)
    :history_(history),sessions_(sessions),self_(std::move(self)),test_(std::move(test)),client_(budget,PiPairClient::Access::LoopbackTest){require(id(self_));}
PiPairRenewal::Selection PiPairRenewal::selected(const std::string &profile){
    require(id(profile));const auto snapshot=history_.snapshot();require(snapshot.peers.has_value());const auto peers=parse_pi_pair_peers(snapshot.peers->bytes,self_);
    Selection result;bool found=false;
    for(const auto &row:peers["profiles"]){
        result.live.push_back(row["profile_id"].get<std::string>());
        if(row["profile_id"]!=profile)continue;
        require(row["stage"]=="active");found=true;const auto &pair=row["pair"];
        result.profile=profile;result.generation=row["profile_generation"].get<std::string>();result.target=row["target_id"].get<std::string>();
        result.pair=pair["pair_id"].get<std::string>();result.authorization=pair["authorization_generation"].get<std::string>();result.current=pair["current"]["epoch"].get<std::string>();
        result.pending=pair["pending"].is_null()?"":pair["pending"]["epoch"].get<std::string>();
        result.previous=pair["previous"].is_null()?"":pair["previous"]["epoch"].get<std::string>();
        result.endpoint={row["endpoint"]["host"].get<std::string>(),31418};
    }
    require(found);return result;
}
std::shared_ptr<PiPairRenewal::Entry> PiPairRenewal::entry(const Selection &selection){
    std::lock_guard guard(mutex_);
    for(auto it=entries_.begin();it!=entries_.end();)if(it->second.use_count()==1&&std::find(selection.live.begin(),selection.live.end(),it->first)==selection.live.end())it=entries_.erase(it);else ++it;
    const auto found=entries_.find(selection.profile);if(found!=entries_.end())return found->second;
    require(entries_.size()<64);auto created=std::make_shared<Entry>();entries_.emplace(selection.profile,created);return created;
}
bool PiPairRenewal::target_requires(const std::string &profile,const PiPairPrincipal &principal,const Json &context){
    try{
        require(valid_controller_document(context,"target-context"));const auto &authorization=context["authorization"];
        require(!authorization.is_null()&&authorization["pair_id"]==principal.identity().pair_id&&
            authorization["authorization_generation"]==principal.identity().authorization_generation&&authorization["renewal_required"]==true);
        const auto selection=selected(profile);require(principal.self_id()==self_&&principal.peer_id()==selection.target&&principal.identity()==
            PiPairTlsIdentity{PiPairTlsPurpose::Management,selection.pair,selection.authorization,selection.current});
        if(context["gateway"].is_null())require(context["target"]["product"]=="WsprryPi"&&context["target"]["device_id"]==selection.target);
        else require(context["gateway"]["parent"]["device_id"]==selection.target);
        const auto state=entry(selection);std::lock_guard guard(state->hints);
        state->signature=selection.pair+"/"+selection.authorization+"/"+selection.generation;state->hint_epoch=selection.current;return true;
    }catch(...){return false;}
}
PiPairRenewal::Result PiPairRenewal::advance(const std::string &profile,const std::function<bool()> &cancelled){
    try{
        require(bool(cancelled)&&!cancelled());const auto policy=privileged_network_runtime_state();
        require(policy.active_known&&policy.active==PrivilegedNetworkMode::enforced&&policy.configured==PrivilegedNetworkMode::enforced);
        auto selection=selected(profile);const auto state=entry(selection);
        std::unique_lock operation(state->operation,std::try_to_lock);if(!operation.owns_lock())return Result::Busy;
        bool hinted=false;
        {
            std::lock_guard hints(state->hints);const auto signature=selection.pair+"/"+selection.authorization+"/"+selection.generation;
            if(state->signature!=signature){state->signature=signature;state->hint_epoch.clear();state->commit_next=false;}
            hinted=state->hint_epoch==selection.current;
        }
        auto request=history_.outgoing_rotation_request(profile);
        if(!request){
            if(!hinted&&!history_.outgoing_renewal_due(profile))return Result::Ready;
            request=history_.prepare_outgoing_rotation(profile,fresh());if(!request)return Result::Held;
        }
        const auto op=(*request)["operation_id"].get<std::string>();
        const bool prepare=(*request)["schema"]=="rotate-prepare";
        const PiPairTlsIdentity current{PiPairTlsPurpose::Management,selection.pair,selection.authorization,selection.current};
        const auto lookup=[&](const auto &identity){return history_.outgoing_credential(profile,identity);};
        const auto endpoint=test_?test_->endpoint:selection.endpoint;
        // A prepared epoch may already have committed on the target. Prefer
        // exact new-key readback; if unavailable, the next bounded step sends
        // only the same durable commit, never another operation or epoch.
        if(!prepare&&(!selection.previous.empty()||!state->commit_next)){
            const auto next=(*request)["next_epoch"].get<std::string>();
            const PiPairTlsIdentity identity{PiPairTlsPurpose::Management,selection.pair,selection.authorization,next};bool acknowledged=false,recorded=false;
            (void)client_.exchange(endpoint,selection.target,lookup,identity,[&](const auto &principal){
                if(!acknowledged)acknowledged=history_.acknowledge_outgoing_rotation(profile,principal,sessions_);
                if(acknowledged&&!recorded)recorded=history_.record_outgoing_finished(profile,principal);
                return acknowledged&&recorded&&bool(history_.outgoing_credential(profile,identity));
            },"GET","/api/pair/v1/operations/"+op,"",{},cancelled);
            if(acknowledged){state->commit_next=false;return history_.outgoing_renewal_due(profile)?Result::Pending:Result::Ready;}
            state->commit_next=true;return Result::Pending;
        }
        const PiPairTlsIdentity identity=prepare?current:PiPairTlsIdentity{PiPairTlsPurpose::Management,selection.pair,selection.authorization,(*request)["expected_epoch"].get<std::string>()};
        bool recorded=false;
        const auto response=client_.exchange(endpoint,selection.target,lookup,identity,[&](const auto &principal){
            if(!recorded)recorded=history_.record_outgoing_finished(profile,principal);
            return recorded&&bool(history_.outgoing_credential(profile,identity));
        },"POST",prepare?"/api/pair/v1/rotate/prepare":"/api/pair/v1/rotate/commit",request->dump(),{},cancelled,
            [&](const auto &principal){return history_.admit_rotation_handoff(profile,selection.generation,op,principal);});
        if(response.state==PiPairClientResult::State::Replied&&response.reply&&response.principal&&response.reply->payload.status==200){
            const auto value=parse_pi_pair_document(response.reply->payload.body,prepare?"rotate-prepared":"rotate-committed");
            if(!history_.accept_outgoing_rotation(profile,*response.principal,value))return Result::Held;
        }
        state->commit_next=false;return Result::Pending;
    }catch(...){return Result::Held;}
}
}
