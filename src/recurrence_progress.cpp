// SPDX-License-Identifier: MIT
#include "recurrence_progress.hpp"
#include <charconv>
#include <set>
#include <stdexcept>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
constexpr std::array names{"ordinary","day","night","morning","evening"};
void require(bool value){if(!value)throw std::runtime_error("recurrence_progress_hold");}
bool fields(const Json &v,std::initializer_list<const char *> keys){
    if(!v.is_object()||v.size()!=keys.size())return false;
    for(auto key:keys)if(!v.contains(key))return false;
    return true;
}
bool hex(const Json &v,std::size_t size){return v.is_string()&&v.get_ref<const std::string &>().size()==size&&v.get_ref<const std::string &>().find_first_not_of("0123456789abcdef")==std::string::npos;}
std::uint64_t number(const Json &v){
    require(v.is_string());const auto &s=v.get_ref<const std::string &>();std::uint64_t result{};
    const auto parsed=std::from_chars(s.data(),s.data()+s.size(),result);
    require(!s.empty()&&(s=="0"||s.front()!='0')&&parsed.ec==std::errc{}&&parsed.ptr==s.data()+s.size());return result;
}
bool cursors(const Json &v){
    if(!fields(v,{"ordinary","day","night","morning","evening"}))return false;
    for(auto name:names){const auto &row=v.at(name);
        if(!fields(row,{"list_digest","index"})||!hex(row.at("list_digest"),64)||!row.at("index").is_number_unsigned()||row.at("index").get<std::uint64_t>()>UINT32_MAX)return false;
    }return true;
}
Json envelope(const Json &record){const auto bytes=record.dump();return {{"record",record},{"sha256",PiManagementJournal::digest(bytes)}};}
}
bool valid_recurrence_progress(const Json &v,std::string_view self) noexcept {
    try{
        if(!hex(std::string(self),32)||!fields(v,{"schema","device_id","authority_generation","configuration_generation","clock_generation","consumed_through_utc_ns","cursors","in_flight"})||
            v.at("schema")!="pi-recurrence-progress/1"||v.at("device_id")!=std::string(self)||!cursors(v.at("cursors")))return false;
        for(auto key:{"authority_generation","configuration_generation","clock_generation","consumed_through_utc_ns"})(void)number(v.at(key));
        const auto &flight=v.at("in_flight");
        if(!flight.is_null()){
            if(!fields(flight,{"occurrence_id","first_start_ns","last_start_ns","intent_digest","state","successor_cursors"})||
                !hex(flight.at("occurrence_id"),32)||!hex(flight.at("intent_digest"),64)||!cursors(flight.at("successor_cursors"))||
                (flight.at("state")!="reserved"&&flight.at("state")!="uncertain"))return false;
            const auto first=number(flight.at("first_start_ns")),last=number(flight.at("last_start_ns"));
            if(!first||first>last||last!=number(v.at("consumed_through_utc_ns")))return false;
        }
        return v.dump().size()<=16384;
    }catch(...){return false;}
}
Json parse_recurrence_progress(std::string_view bytes,std::string_view self){
    require(!bytes.empty()&&bytes.size()<=16384);std::vector<std::set<std::string>> keys;
    auto v=Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json &item){
        require(depth>=0&&depth<=8);
        if(event==Json::parse_event_t::object_start)keys.emplace_back();
        if(event==Json::parse_event_t::key)require(!keys.empty()&&keys.back().insert(item.get<std::string>()).second);
        if(event==Json::parse_event_t::object_end)keys.pop_back();
        return true;
    });
    require(fields(v,{"record","sha256"})&&hex(v.at("sha256"),64)&&
        v.at("sha256")==PiManagementJournal::digest(v.at("record").dump())&&valid_recurrence_progress(v.at("record"),self));
    return v.at("record");
}
Json initial_recurrence_progress(std::string_view self,const RecurrenceListDigests &digests){
    Json rows=Json::object();for(std::size_t i=0;i<names.size();++i)rows[names[i]]={{"list_digest",digests[i]},{"index",std::uint32_t{0}}};
    Json result{{"schema","pi-recurrence-progress/1"},{"device_id",self},{"authority_generation","0"},{"configuration_generation","0"},
        {"clock_generation","0"},{"consumed_through_utc_ns","0"},{"cursors",rows},{"in_flight",nullptr}};
    require(valid_recurrence_progress(result,self));return result;
}
RecurrenceProgress::RecurrenceProgress(std::string directory,std::string path,std::string self)
    :self_(std::move(self)),journal_(std::move(directory),self_,{{"progress",std::move(path)}}){}
void RecurrenceProgress::recover(bool may_initialize,const RecurrenceListDigests &digests){
    std::lock_guard lock(mutex_);held_=true;
    journal_.recover();observation_=journal_.inspect("progress");
    if(!observation_){require(may_initialize);publish(initial_recurrence_progress(self_,digests));}
    else record_=parse_recurrence_progress(observation_->bytes,self_);
    // Unknown acceptance is deliberately not resolved here. Only the caller's
    // positive output reconciliation may consume the successor and clear it.
    held_=false;
}
void RecurrenceProgress::check(){
    require(!held_&&observation_.has_value());
    try{const auto current=journal_.inspect("progress");require(current&&current->signature==observation_->signature&&current->bytes==observation_->bytes&&!journal_.decision());}
    catch(...){held_=true;throw;}
}
void RecurrenceProgress::publish(const Json &next){
    require(valid_recurrence_progress(next,self_));
    try{
        journal_.prepare({{"progress",observation_?std::optional<std::string>(PiManagementJournal::digest(observation_->bytes)):std::nullopt,envelope(next).dump()}});
        journal_.commit();journal_.recover();const auto current=journal_.inspect("progress");
        require(current&&parse_recurrence_progress(current->bytes,self_)==next);
        record_=next;observation_=current;
        ++publications_;
    }catch(...){held_=true;throw;}
}
Json RecurrenceProgress::snapshot(){std::lock_guard lock(mutex_);check();return record_;}
void RecurrenceProgress::reserve(const std::string &occurrence,std::uint64_t first,std::uint64_t last,
    const std::string &intent,std::uint64_t authority,std::uint64_t configuration,std::uint64_t clock,
    const RecurrenceListDigests &digests,const std::array<std::uint32_t,5> &sizes,std::size_t scope,bool explicit_skip){
    std::lock_guard lock(mutex_);check();require(record_.at("in_flight").is_null()&&first>number(record_.at("consumed_through_utc_ns"))&&last>=first&&scope<names.size()&&
        hex(occurrence,32)&&hex(intent,64)&&authority&&configuration);
    auto next=record_;auto &rows=next["cursors"];
    for(std::size_t i=0;i<names.size();++i){
        auto &row=rows[names[i]];
        if(row["list_digest"]!=digests[i])row={{"list_digest",digests[i]},{"index",std::uint32_t{0}}};
        require(sizes[i]==0?row["index"]==0:row["index"].get<std::uint64_t>()<sizes[i]);
    }
    auto successor=rows;
    if(sizes[scope])successor[names[scope]]["index"]=(rows[names[scope]]["index"].get<std::uint32_t>()+1U)%sizes[scope];
    next["authority_generation"]=std::to_string(authority);next["configuration_generation"]=std::to_string(configuration);next["clock_generation"]=std::to_string(clock);
    next["consumed_through_utc_ns"]=std::to_string(last);
    next["in_flight"]={{"occurrence_id",occurrence},{"first_start_ns",std::to_string(first)},{"last_start_ns",std::to_string(last)},
        {"intent_digest",intent},{"state","reserved"},{"successor_cursors",successor}};
    if(explicit_skip){next["cursors"]=successor;next["in_flight"]=nullptr;}
    publish(next);
}
void RecurrenceProgress::resolve(bool accepted,const RecurrenceListDigests &current){
    std::lock_guard lock(mutex_);check();require(!record_.at("in_flight").is_null());auto next=record_;
    for(std::size_t i=0;i<names.size();++i){
        auto &row=next["cursors"][names[i]];const auto &proposed=next["in_flight"]["successor_cursors"][names[i]];
        if(row["list_digest"]!=current[i])row={{"list_digest",current[i]},{"index",std::uint32_t{0}}};
        else if(accepted&&proposed["list_digest"]==current[i])row=proposed;
    }
    next["in_flight"]=nullptr;publish(next);
}
bool RecurrenceProgress::held() const{std::lock_guard lock(mutex_);return held_;}
std::uint64_t RecurrenceProgress::publication_count() const{std::lock_guard lock(mutex_);return publications_;}
}
