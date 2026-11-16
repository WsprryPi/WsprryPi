// SPDX-License-Identifier: MIT
#include "pi_configuration_recovery.hpp"
#include "controller_contract.hpp"
#include "control_contract.hpp"
#include "config_handler_serialization.hpp"
#include <optional>
#include <stdexcept>
#include <vector>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
bool private_reference(const std::string &section,const std::string &key){
    return section=="WTP"&&(key=="TLS CA File"||key=="TLS Client Certificate"||key=="TLS Client Key");
}
Json unavailable(const char *reason,const Json *value=nullptr){
    Json text=nullptr;
    if(value)try{const auto encoded=value->dump(-1,' ',true);if(encoded.size()<=256)text=encoded;}catch(...){}
    return {{"state","unavailable"},{"reason",reason},{"saved_text",text}};
}
Json project(const Json &internal,const Json &raw,std::size_t maximum_bytes){
    static const auto definitions=Json::parse(controller_document_definitions());
    const auto &sections=definitions["pi-config"]["properties"];
    Json draft=Json::object();std::vector<std::pair<std::string,std::string>> fields;
    for(const auto &[section,group]:sections.items()){
        draft[section]=Json::object();
        for(const auto &[key,rule]:group["properties"].items()){
            fields.emplace_back(section,key);const Json *saved=nullptr;
            if(internal.is_object()&&internal.contains(section)&&internal[section].is_object()&&internal[section].contains(key))saved=&internal[section][key];
            auto &value=draft[section][key];
            if(!saved){value=unavailable("missing");continue;}
            // The owning INI parser represents hexadecimal addresses as
            // integers; the current saved-control view uses its canonical
            // address string. Preserve that same value rather than falsely
            // marking every valid saved address as unavailable.
            if(section=="Si5351"&&key=="I2C Address"&&saved->is_number_integer()&&*saved>=0x60&&*saved<=0x6f){
                value=config_handler_serialization::config_serialization_si5351_i2c_address(saved->get<int>());
                continue;
            }
            if(private_reference(section,key)){
                value=valid_pi_configuration_field(section,key,*saved)?Json{{"state","preserved"}}:unavailable("invalid_shape");
                continue;
            }
            if(valid_pi_configuration_field(section,key,*saved)){value=*saved;continue;}
            const bool large=saved->is_string()&&saved->get_ref<const std::string&>().size()>32768;
            const Json *original=saved;
            if(raw.is_object()&&raw.contains(section)&&raw[section].is_object()&&raw[section].contains(key))original=&raw[section][key];
            value=unavailable(large?"too_large":"invalid_shape",large?nullptr:original);
        }
    }
    // Keep the complete fixed field closure. When a field cannot fit the
    // negotiated envelope, represent its unavailability; never truncate it or
    // replace its saved value with a default. The caller accounts for metadata.
    while(draft.dump().size()>maximum_bytes){
        std::optional<std::pair<std::string,std::string>> largest;Json replacement;std::size_t saving=0;
        for(const auto &[section,key]:fields){
            const auto &value=draft[section][key];Json reduced;
            if(value.is_object()&&value.value("state",Json(nullptr))=="unavailable"){
                if(value["saved_text"].is_null())continue;
                reduced=value;reduced["saved_text"]=nullptr;
            }else{
                if(private_reference(section,key))continue;
                reduced=unavailable("too_large");
            }
            const auto before=value.dump().size(),after=reduced.dump().size();
            if(before>after&&before-after>saving){largest={{section,key}};replacement=std::move(reduced);saving=before-after;}
        }
        if(!largest)throw std::runtime_error("configuration_recovery_capacity");
        draft[largest->first][largest->second]=std::move(replacement);
    }
    return draft;
}
}
Json pi_configuration_recovery_draft(const Json &internal,std::size_t maximum_bytes){
    return project(internal,Json::object(),maximum_bytes);
}
Json pi_configuration_recovery_draft(const ConfigIniDraftSnapshot &snapshot,std::size_t maximum_bytes){
    return project(snapshot.source,snapshot.raw_source,maximum_bytes);
}
} // namespace wsprrypi
