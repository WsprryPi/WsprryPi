// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include <string>
#include <initializer_list>
namespace wsprrypi::gray_line_contract {
inline bool settings(const nlohmann::json& v) {
    const auto closed=[](const auto& x, std::initializer_list<const char*> names) {
        if (!x.is_object() || x.size()!=names.size()) return false;
        for(const auto* name:names) if(!x.contains(name)) return false;
        return true;
    };
    if(!closed(v,{"location","morning","evening"}) ||
       (v["location"]!="manual" && v["location"]!="gps")) return false;
    for(const auto* name:{"morning","evening"}) {
        const auto& p=v[name];
        if(!closed(p,{"enabled","lower_mdeg","upper_mdeg","wspr_dial_frequency_hz","message_frequency_nhz"}) ||
           !p["enabled"].is_boolean() || !p["lower_mdeg"].is_number_integer() ||
           !p["upper_mdeg"].is_number_integer() || p["lower_mdeg"] < -90000 ||
           p["upper_mdeg"] > 90000 || p["lower_mdeg"] >= p["upper_mdeg"]) return false;
        const auto& dial=p["wspr_dial_frequency_hz"];
        if(!dial.is_null() && (!dial.is_number_integer() || dial<6503 || dial>159998496)) return false;
        const auto& frequency=p["message_frequency_nhz"];
        if(!frequency.is_null()) {
            if(!frequency.is_string()) return false;
            const auto text=frequency.get<std::string>();
            if(text.empty() || text[0]=='0' || text.size()>20 ||
               text.find_first_not_of("0123456789")!=std::string::npos ||
               (text.size()==20 && text>"18446744073709551615")) return false;
        }
    }
    return true;
}
}
