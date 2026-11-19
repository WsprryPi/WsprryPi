// SPDX-License-Identifier: MIT
#include "pi_pair_usage.hpp"
#include "pi_pair_records.hpp"
#include "pi_pair_peers.hpp"
#include <charconv>
#include <set>
#include <stdexcept>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw std::runtime_error("pairing_usage_unavailable");}
std::uint64_t number(const Json &value){const auto &text=value.get_ref<const std::string &>();std::uint64_t result{};
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);require(parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size());return result;}
std::uint64_t add(std::uint64_t a,std::uint64_t b){return b>UINT64_MAX-a?UINT64_MAX:a+b;}
template<class F>void current(Json &catalog,Json &peers,F visit){
    if(!catalog["active"].is_null()&&catalog["active"]["state"]=="active")visit(catalog["active"]);
    for(auto &row:peers["profiles"])if(row["stage"]=="active")visit(row["pair"]);
}
}
PiPairUsage::PiPairUsage(std::string self,Clock clock):self_(std::move(self)),clock_(std::move(clock)){require(bool(clock_));}
PiPairUsage::Time PiPairUsage::now(){
    require(!failed_);const auto value=clock_();if(running_&&value<last_){failed_=true;throw std::runtime_error("pairing_usage_unavailable");}
    last_=value;return value;
}
PiPairUsage::Key PiPairUsage::key(const PiPairTlsIdentity &identity){return {identity.pair_id,identity.authorization_generation,identity.epoch};}
std::uint64_t PiPairUsage::service(const Entry &entry,Time at) const{
    require(at>=entry.born);return add(entry.service,static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(at-entry.born).count()));
}
void PiPairUsage::refresh(const Json &catalog,const Json &peers,Time at,bool startup,bool unclean){
    require(valid_pi_pair_catalog(catalog,self_)&&valid_pi_pair_peers(peers,self_));std::set<Key> live;
    auto c=catalog,p=peers;
    current(c,p,[&](const auto &pair){
        const Key identity{pair["pair_id"].template get<std::string>(),pair["authorization_generation"].template get<std::string>(),pair["current"]["epoch"].template get<std::string>()};
        require(live.insert(identity).second);const auto &slot=pair["current"];
        const auto stored_service=number(slot["service_ms"]),stored_connections=number(slot["connections"]);
        const auto found=entries_.find(identity);
        if(found==entries_.end())entries_.emplace(identity,Entry{stored_service,stored_connections,at,startup&&unclean});
        else require(stored_service<=service(found->second,at)&&stored_connections<=found->second.connections);
    });
    require(live.size()<=65);
    for(auto it=entries_.begin();it!=entries_.end();)if(!live.contains(it->first))it=entries_.erase(it);else ++it;
}
void PiPairUsage::begin(const Json &catalog,const Json &peers,bool unclean){
    require(!running_&&!failed_);const auto at=now();
    try{refresh(catalog,peers,at,true,unclean);running_=true;}catch(...){entries_.clear();failed_=true;throw;}
}
void PiPairUsage::reconcile(const Json &catalog,const Json &peers){
    require(running_);try{refresh(catalog,peers,now(),false,false);}catch(...){failed_=true;throw;}
}
void PiPairUsage::authenticated(const PiPairTlsIdentity &identity){
    require(running_&&identity.purpose==PiPairTlsPurpose::Management);(void)now();
    const auto found=entries_.find(key(identity));require(found!=entries_.end());found->second.connections=add(found->second.connections,1);
}
bool PiPairUsage::due(const PiPairTlsIdentity &identity) noexcept{
    try{require(running_&&identity.purpose==PiPairTlsPurpose::Management);const auto at=now();const auto found=entries_.find(key(identity));
        require(found!=entries_.end());return found->second.forced||service(found->second,at)>=service_limit_ms||found->second.connections>=connection_limit;
    }catch(...){return true;}
}
PiPairUsage::Projection PiPairUsage::project(const Json &catalog,const Json &peers){
    reconcile(catalog,peers);const auto at=now();Projection result{catalog,peers};
    current(result.catalog,result.peers,[&](auto &pair){
        const Key identity{pair["pair_id"].template get<std::string>(),pair["authorization_generation"].template get<std::string>(),pair["current"]["epoch"].template get<std::string>()};
        const auto &entry=entries_.at(identity);pair["current"]["service_ms"]=std::to_string(service(entry,at));pair["current"]["connections"]=std::to_string(entry.connections);
    });return result;
}
void PiPairUsage::stop() noexcept{entries_.clear();running_=false;}
bool PiPairUsage::unclean_checkpoint_required() const noexcept{
    if(failed_)return true;
    for(const auto &[identity,entry]:entries_){(void)identity;if(entry.forced)return true;}
    return false;
}
}
