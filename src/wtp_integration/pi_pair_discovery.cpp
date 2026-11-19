// SPDX-License-Identifier: MIT
#include "pi_pair_discovery.hpp"
#include "controller_contract.hpp"
#include "control_contract.hpp"
#include <openssl/rand.h>
#include <array>
#include <charconv>
#include <set>
#include <stdexcept>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw std::invalid_argument("invalid_pairing_discovery");}
bool id(const std::string &value){return value.size()==32&&value.find_first_not_of("0123456789abcdef")==std::string::npos;}
std::string fresh(){std::array<unsigned char,16> bytes{};require(RAND_bytes(bytes.data(),bytes.size())==1);std::string result;
    for(const auto byte:bytes){result+="0123456789abcdef"[byte>>4];result+="0123456789abcdef"[byte&15];}return result;}
std::string dns_name(std::string value){for(auto &c:value)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');if(!value.empty()&&value.back()!='.')value+='.';return value;}
Json document(bool available,const Json &entries){return {{"contract","Wsprry-Control/1"},{"schema","pair-discovery"},{"available",available},{"discoveries",entries}};}
Json decode(const PiPairDiscovery::Key &key,const std::string &domain,const std::string &host,unsigned port,const std::vector<std::string> &txt,bool wide){
    require(key.interface_index&&key.instance.size()<=63&&dns_name(domain)=="local."&&!wide&&port==31418&&txt.size()==7&&txt.front()=="txtvers=1");
    std::map<std::string,std::string> fields;
    for(const auto &part:txt){require(part.size()<=255);const auto equal=part.find('=');require(equal!=std::string::npos&&equal>0&&fields.emplace(part.substr(0,equal),part.substr(equal+1)).second);}
    require(fields.size()==7&&fields["txtvers"]=="1"&&fields["binding"]=="Wsprry-Pair/1"&&id(fields["id"])&&id(fields["boot"])&&
        fields["suite"]=="SPAKE2+-P256-SHA256-HKDF-HMAC"&&fields["control-sha256"]==control_schema_sha256);
    const auto &text=fields["http-port"];unsigned http{};const auto parsed=std::from_chars(text.data(),text.data()+text.size(),http);
    require(!text.empty()&&text.front()!='0'&&parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()&&http&&http<=65535);
    Json result{{"discovery_id",std::string(32,'0')},{"device_id",fields["id"]},{"boot_id",fields["boot"]},{"name",key.instance},
        {"host",dns_name(host)},{"bootstrap_port",http},{"management_port",port},{"suite",fields["suite"]},{"control_schema_digest",fields["control-sha256"]}};
    require(valid_controller_document(document(true,Json::array({result})),"pair-discovery"));return result;
}
}
std::vector<std::string> pi_pair_discovery_txt(const std::string &self,const std::string &boot,unsigned http_port){
    require(id(self)&&id(boot)&&http_port&&http_port<=65535);
    return {"txtvers=1","binding=Wsprry-Pair/1","id="+self,"boot="+boot,"http-port="+std::to_string(http_port),
        "suite=SPAKE2+-P256-SHA256-HKDF-HMAC","control-sha256="+std::string(control_schema_sha256)};
}
PiPairDiscovery::PiPairDiscovery(std::string self,Clock clock):self_(std::move(self)),clock_(std::move(clock)){
    require(id(self_)&&bool(clock_));last_=clock_();
}
void PiPairDiscovery::expire(){
    const auto now=clock_();if(now<last_)entries_.clear();last_=now;
    for(auto it=entries_.begin();it!=entries_.end();)if(now-it->second.observed>=std::chrono::seconds(30))it=entries_.erase(it);else ++it;
}
void PiPairDiscovery::available(bool value){std::lock_guard guard(mutex_);expire();available_=value;if(!value)entries_.clear();}
bool PiPairDiscovery::resolved(const Key &key,std::string domain,std::string host,unsigned port,const std::vector<std::string> &txt,bool wide){
    std::lock_guard guard(mutex_);expire();
    try{
        require(available_);auto value=decode(key,domain,host,port,txt,wide);require(value["device_id"]!=self_);
        auto found=entries_.find(key);std::string handle;
        if(found!=entries_.end()){
            auto previous=found->second.value;previous["discovery_id"]=value["discovery_id"];
            if(previous==value)handle=found->second.value["discovery_id"].get<std::string>();
        }
        if(handle.empty())handle=fresh();
        // Multiple local interfaces may resolve the same service. Their
        // public observation must agree exactly; do not select one conflict.
        for(const auto &[other,entry]:entries_)if(other!=key&&entry.value["device_id"]==value["device_id"]){
            auto comparable=entry.value;comparable["discovery_id"]=value["discovery_id"];
            if(comparable!=value){
                const auto target=value["device_id"];
                for(auto it=entries_.begin();it!=entries_.end();)if(it->second.value["device_id"]==target)it=entries_.erase(it);else ++it;
                return false;
            }
            handle=entry.value["discovery_id"].get<std::string>();
        }
        require(found!=entries_.end()||entries_.size()<64);value["discovery_id"]=handle;
        entries_.insert_or_assign(key,Entry{std::move(value),last_});return true;
    }catch(...){
        // A conflicting update withdraws the old observation too. Keeping it
        // would leave a selectable stale boot or port after a failed refresh.
        const auto old=entries_.find(key);if(old!=entries_.end()){
            const auto target=old->second.value["device_id"];
            for(auto it=entries_.begin();it!=entries_.end();)if(it->second.value["device_id"]==target)it=entries_.erase(it);else ++it;
        }
        return false;
    }
}
void PiPairDiscovery::removed(const Key &key){std::lock_guard guard(mutex_);expire();entries_.erase(key);}
nlohmann::json PiPairDiscovery::snapshot(){
    std::lock_guard guard(mutex_);expire();Json values=Json::array();std::set<std::string> seen;
    if(available_)for(const auto &[key,entry]:entries_){(void)key;if(seen.insert(entry.value["device_id"].get<std::string>()).second)values.push_back(entry.value);}
    auto result=document(available_,values);require(valid_controller_document(result,"pair-discovery"));return result;
}
std::optional<nlohmann::json> PiPairDiscovery::candidate(const std::string &handle){
    std::lock_guard guard(mutex_);expire();if(!available_||!id(handle))return {};
    for(const auto &[key,entry]:entries_){(void)key;if(entry.value["discovery_id"]==handle)return entry.value;}return {};
}
} // namespace wsprrypi
