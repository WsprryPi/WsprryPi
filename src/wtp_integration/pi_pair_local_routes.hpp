// SPDX-License-Identifier: MIT
#pragma once
#include "pi_pair_contract.hpp"
#include <optional>
#include <string_view>
namespace wsprrypi {
// Closed path classification only. A matching path grants no authority.
inline std::optional<std::string_view> local_pairing_operation_id(std::string_view path){
    constexpr std::string_view prefix="/api/control/v1/host/pairing/operations/";
    if(!path.starts_with(prefix))return {};
    const auto id=path.substr(prefix.size());
    if(id.size()!=32||id.find_first_not_of("0123456789abcdef")!=std::string_view::npos)return {};
    return id;
}
inline bool local_pairing_read_path(std::string_view path){
    return path=="/api/control/v1/host/pairing"||path=="/api/control/v1/host/pairing/discovery"||local_pairing_operation_id(path).has_value();
}
inline bool local_pairing_mutation_path(std::string_view path){
    return path=="/api/control/v1/host/pairing/window"||path=="/api/control/v1/host/pairing/remove"||path=="/api/control/v1/host/pairing/connect";
}
inline bool pi_pair_bootstrap_path(std::string_view method,std::string_view path){
    constexpr std::string_view prefix="/api/pair/v1/";
    return path.starts_with(prefix)&&pi_pair_route(PiPairRoutePurpose::Bootstrap,method,path.substr(prefix.size()));
}

}
