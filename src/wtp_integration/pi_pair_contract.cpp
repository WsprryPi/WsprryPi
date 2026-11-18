// SPDX-License-Identifier: MIT
#include "pi_pair_contract.hpp"
#include "pi_pair_contract_data.hpp"
#include "controller_contract.hpp"
#include "pi_management_journal.hpp"
#include "browser_api.hpp"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <openssl/evp.h>
#include <set>
#include <stdexcept>
#include <vector>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
const Json &contract(){static const auto value=Json::parse(pi_pair_contract_data());return value;}
bool canonical_binary(const std::string &input,std::size_t size){
    if(input.size()!=(size*8+5)/6||input.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_")!=std::string::npos)return false;
    auto encoded=input;std::replace(encoded.begin(),encoded.end(),'-','+');std::replace(encoded.begin(),encoded.end(),'_','/');
    while(encoded.size()%4)encoded+='=';
    std::vector<unsigned char> bytes(encoded.size()/4*3);
    if(EVP_DecodeBlock(bytes.data(),reinterpret_cast<const unsigned char *>(encoded.data()),encoded.size())!=static_cast<int>(bytes.size()))return false;
    std::string normalized((size+2)/3*4,'\0');
    EVP_EncodeBlock(reinterpret_cast<unsigned char *>(normalized.data()),bytes.data(),size);
    while(normalized.ends_with('='))normalized.pop_back();
    std::replace(normalized.begin(),normalized.end(),'+','-');std::replace(normalized.begin(),normalized.end(),'/','_');
    return normalized==input&&(size!=65||bytes.front()==4);
}
}
bool valid_pi_pair_document(const Json &value,std::string_view family){
    try{
        const auto name=std::string(family);const auto &spec=contract();
        if(std::find(spec["documents"].begin(),spec["documents"].end(),name)==spec["documents"].end()||
            !valid_closed_contract_document(value,spec["$defs"].at(name),spec["$defs"])||value.dump().size()>4096)return false;
        for(const auto *field:{"controller_nonce","target_nonce","nonce","cA","cB","proof"})
            if(value.contains(field)&&!canonical_binary(value[field].get<std::string>(),32))return false;
        for(const auto *field:{"pA","pB"})if(value.contains(field)&&!canonical_binary(value[field].get<std::string>(),65))return false;
        if(family=="hello"&&value["controller_id"]==value["target_id"])return false;
        if(family=="window"&&(value["open"].get<bool>()!=!value["window_id"].is_null()||
            value["window_id"].is_null()!=value["pair_id"].is_null()||value["window_id"].is_null()!=value["action"].is_null()))return false;
        if(family=="rotate-commit"){
            std::uint64_t old{},next{};
            const auto before=value["expected_epoch"].get<std::string>(),after=value["next_epoch"].get<std::string>();
            const auto a=std::from_chars(before.data(),before.data()+before.size(),old),b=std::from_chars(after.data(),after.data()+after.size(),next);
            if(a.ec!=std::errc{}||b.ec!=std::errc{}||next<=old||next-old!=1)return false;
        }
        if(family=="resume-grant"){
            auto canonical=value;canonical.erase("grant_digest");
            if(value["grant_digest"]!=PiManagementJournal::digest(canonical_pi_management_json(canonical)))return false;
        }
        if(value.contains("controller_id")&&value.contains("target_id")&&value["controller_id"]==value["target_id"])return false;
        if(family=="authority-register"){
            std::set<std::string> ids;
            for(const auto &assignment:value["assignments"])if(!ids.insert(assignment["assignment_id"].get<std::string>()).second)return false;
        }
        return true;
    }catch(...){return false;}
}
Json parse_pi_pair_document(std::string_view bytes,std::string_view family){
    if(bytes.empty()||bytes.size()>4096)throw std::invalid_argument("pairing_invalid_shape");
    auto value=strict_browser_json(std::string(bytes),8);
    if(!valid_pi_pair_document(value,family))throw std::invalid_argument("pairing_invalid_shape");
    return value;
}
bool pi_pair_route(PiPairRoutePurpose purpose,std::string_view method,std::string_view resource){
    if(resource.starts_with("operations/")){
        if(resource.size()!=43||resource.substr(11).find_first_not_of("0123456789abcdef")!=std::string_view::npos)return false;
        resource="operations/{operation_id}";
    }
    const char *role{};
    switch(purpose){
        case PiPairRoutePurpose::Bootstrap:role="bootstrap";break;
        case PiPairRoutePurpose::Management:role="management";break;
        case PiPairRoutePurpose::Retirement:role="retirement";break;
        default:return false;
    }
    const auto &routes=contract()["routes"][role];
    if(!routes.contains(std::string(method)))return false;
    const auto &names=routes[std::string(method)];
    return std::find(names.begin(),names.end(),resource)!=names.end();
}
} // namespace wsprrypi
