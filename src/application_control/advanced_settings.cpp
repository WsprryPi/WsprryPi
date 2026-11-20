// SPDX-License-Identifier: MIT
#include "advanced_settings.hpp"
#include "local_transport.hpp"
#include <algorithm>
#include <charconv>
#include <limits>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
constexpr int largest=std::numeric_limits<int>::max();
const std::array<AdvancedField,10> registry{{
    {"Meta.debug_logging","Meta","debug_logging",AdvancedValueKind::Boolean,true,0,0},
    {"Operation.Web Port","Operation","Web Port",AdvancedValueKind::Integer,true,1024,49151},
    {"Operation.Socket Port","Operation","Socket Port",AdvancedValueKind::Integer,true,1024,49151},
    {"Experimental.Allow Unqualified Frequency","Experimental","Allow Unqualified Frequency",AdvancedValueKind::Boolean,false,0,0},
    {"Experimental.Allow Non-Amateur Frequency","Experimental","Allow Non-Amateur Frequency",AdvancedValueKind::Boolean,false,0,0},
    {"Si5351.TX Output","Si5351","TX Output",AdvancedValueKind::Choice,false,0,0},
    {"CW.Fade Shape","CW","Fade Shape",AdvancedValueKind::Choice,false,0,0},
    {"CW.Fade In Ms","CW","Fade In Ms",AdvancedValueKind::Integer,false,0,largest},
    {"CW.Fade Out Ms","CW","Fade Out Ms",AdvancedValueKind::Integer,false,0,largest},
    {"CW.Fade Slice Ms","CW","Fade Slice Ms",AdvancedValueKind::Integer,false,1,largest}
}};
void invalid(){throw LocalCommandRefused(LocalRefusal::InvalidRequest);}
void put(PersistentConfiguration &settings,const AdvancedField &field,const Json &value){
    const std::string_view name=field.name;
    if(name=="Meta.debug_logging")settings.private_settings.debug_logging=value.get<bool>();
    else if(name=="Experimental.Allow Unqualified Frequency")settings.private_settings.allow_unqualified_frequency=value.get<bool>();
    else if(name=="Experimental.Allow Non-Amateur Frequency")settings.private_settings.allow_non_amateur_frequency=value.get<bool>();
    else settings.configuration[field.section][field.key]=value;
}
}
const std::array<AdvancedField,10> &advanced_fields(){return registry;}
const AdvancedField *advanced_field(std::string_view name){
    const auto found=std::find_if(registry.begin(),registry.end(),[&](const auto &field){return name==field.name;});
    return found==registry.end()?nullptr:&*found;
}
bool valid_advanced_value(const AdvancedField &field,const Json &value){
    if(field.kind==AdvancedValueKind::Boolean)return value.is_boolean();
    if(field.kind==AdvancedValueKind::Integer)return value.is_number_integer()&&value>=field.minimum&&value<=field.maximum;
    if(!value.is_string())return false;
    if(std::string_view(field.name)=="Si5351.TX Output")return value=="CLK0"||value=="CLK1"||value=="CLK2";
    return value=="none"||value=="linear"||value=="raised_cosine";
}
Json parse_advanced_argument(const AdvancedField &field,std::string_view text){
    Json value;
    if(field.kind==AdvancedValueKind::Boolean){
        if(text=="true"||text=="True")value=true;
        else if(text=="false"||text=="False")value=false;
        else invalid();
    }else if(field.kind==AdvancedValueKind::Integer){
        if(text.empty())invalid();
        int number{};const auto parsed=std::from_chars(text.data(),text.data()+text.size(),number);
        if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())invalid();
        value=number;
    }else value=std::string(text);
    if(!valid_advanced_value(field,value))invalid();
    return value;
}
Json advanced_values(const PersistentConfiguration &settings){
    if(settings.role!=StateRole::Pi&&settings.role!=StateRole::Controller)invalid();
    Json result=Json::object();
    for(const auto &field:registry){
        if(settings.role==StateRole::Controller&&!field.controller)continue;
        const std::string_view name=field.name;
        if(name=="Meta.debug_logging")result[field.name]=settings.private_settings.debug_logging;
        else if(name=="Experimental.Allow Unqualified Frequency")result[field.name]=settings.private_settings.allow_unqualified_frequency;
        else if(name=="Experimental.Allow Non-Amateur Frequency")result[field.name]=settings.private_settings.allow_non_amateur_frequency;
        else result[field.name]=settings.configuration.at(field.section).at(field.key);
    }
    return result;
}
PersistentConfiguration apply_advanced_changes(const PersistentConfiguration &before,const Json &changes){
    if(before.role!=StateRole::Pi&&before.role!=StateRole::Controller)invalid();
    if(!changes.is_object()||changes.empty()||changes.size()>registry.size())invalid();
    auto after=before;
    for(const auto &[name,value]:changes.items()){
        const auto field=advanced_field(name);
        if(!field||(before.role==StateRole::Controller&&!field->controller)||!valid_advanced_value(*field,value))invalid();
        put(after,*field,value);
    }
    const auto &ports=after.configuration.at("Operation");
    if(ports.at("Web Port")==ports.at("Socket Port"))invalid();
    return after;
}
bool advanced_only_change(const PersistentConfiguration &before,const PersistentConfiguration &after){
    if(before.role!=after.role)return false;
    try{
        const auto previous=advanced_values(before),next=advanced_values(after);Json changes=Json::object();
        for(const auto &[name,value]:next.items())if(previous.at(name)!=value)changes[name]=value;
        return changes.empty()?before==after:apply_advanced_changes(before,changes)==after;
    }catch(...){return false;}
}
bool valid_advanced_view(const Json &view){
    try{
        if(!view.is_object()||view.size()!=5||view.at("schema")!="advanced-settings/1"||
            (view.at("role")!="pi"&&view.at("role")!="controller")||!view.at("revision").is_string())return false;
        const auto text=view.at("revision").get<std::string>();std::uint64_t number{};
        const auto parsed=std::from_chars(text.data(),text.data()+text.size(),number);
        if(text.empty()||text.front()=='0'||parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())return false;
        bool known=false;
        for(const auto *state:{"ready","starting","stopping","setup_required","configuration_recovery","output_recovery","storage_recovery"})
            if(view.at("editing")==state)known=true;
        if(!known||!view.at("settings").is_object())return false;
        const auto &settings=view.at("settings");const bool controller=view.at("role")=="controller";
        if(settings.size()!=(controller?3:10))return false;
        for(const auto &field:registry){
            if(controller&&!field.controller)continue;
            const auto &row=settings.at(field.name);
            if(!row.is_object()||row.size()!=4||!row.at("valid").is_boolean())return false;
            const auto &saved=row.at("saved"),&active=row.at("active");
            if(!saved.is_null()&&!saved.is_boolean()&&!saved.is_number_integer()&&!saved.is_string())return false;
            if(saved.is_string()&&saved.get_ref<const std::string &>().size()>128)return false;
            if(row.at("valid")!=valid_advanced_value(field,saved)||(!active.is_null()&&!valid_advanced_value(field,active)))return false;
            known=false;
            for(const auto *activation:{"active","invalid","pending","inactive","restart_required","startup_override"})
                if(row.at("activation")==activation)known=true;
            if(!known||(row.at("activation")=="invalid")!=(!row.at("valid").get<bool>()))return false;
            if(row.at("activation")=="active"&&saved!=active)return false;
        }
        return true;
    }catch(...){return false;}
}
}
