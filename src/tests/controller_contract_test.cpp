// SPDX-License-Identifier: MIT
#include "wtp_integration/controller_contract.hpp"
#include "wtp_integration/control_contract.hpp"
#include "wtp_integration/pi_configuration_recovery.hpp"
#include <fstream>
#include <iostream>
#include <limits>
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    try {
        std::ifstream source(argv[1]);nlohmann::json vectors;source>>vectors;unsigned count=0;
        for(const auto &item:vectors.at("cases")) {
            if(wsprrypi::valid_controller_document(item.at("value"),item.at("family").get<std::string>())!=item.at("valid").get<bool>())
                throw std::runtime_error(item.at("name").get<std::string>());
            ++count;
        }
        bool pi_save=false;
        for(const auto &item:vectors.at("cases"))if(item.at("name")=="Pi closed structural save") {
            pi_save=true;
            for(const auto number:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()}) {
                auto value=item.at("value");value["candidate"]["configuration"]["CW"]["Base Frequency"]=number;
                if(wsprrypi::valid_controller_document(value,"target-save"))throw std::runtime_error("Nonfinite Pi configuration admitted");
            }
        }
        if(!pi_save)throw std::runtime_error("Current Pi structural save vector required");
        for(const auto &item:vectors.at("cases"))if(item.at("name")=="Pi closed structural save"){
            auto source=item.at("value").at("candidate").at("configuration");
            source["Security"]={{"Private Key","never-leak-private-value"}};
            source["WTP"]["Password"]="never-leak-private-value";
            source["WTP"]["TLS Client Key"]="/never-leak-private-value";
            source["CW"]["Message"]="Custom snowman ☃";
            auto draft=wsprrypi::pi_configuration_recovery_draft(source);
            auto document=nlohmann::json{{"contract","Wsprry-Control/1"},{"schema","configuration-recovery"},
                {"binding",item.at("value").at("binding")},{"capability_revision",std::string(64,'a')},{"revision",std::string(64,'b')},
                {"writable",true},{"draft",draft},{"defaulted_fields",nlohmann::json::array()},{"validation","invalid"}};
            if(!wsprrypi::valid_controller_document(document,"configuration-recovery")||draft["CW"]["Message"]!="Custom snowman ☃"||
                draft.dump().find("never-leak-private-value")!=std::string::npos)
                throw std::runtime_error("Recovery projection must retain public drafts and redact private references/unknown fields");
            source["CW"]["Message"]=false;draft=wsprrypi::pi_configuration_recovery_draft(source);document["draft"]=draft;
            if(!wsprrypi::valid_controller_document(document,"configuration-recovery")||draft["CW"]["Message"]["state"]!="unavailable"||draft["CW"]["Message"]["saved_text"]!="false")
                throw std::runtime_error("Invalid saved scalar must not become a typed default");
            source["CW"]["Message"]=std::string(100000,'x');source["WSPR"]["Call Sign"]=std::string(24000,'a');
            draft=wsprrypi::pi_configuration_recovery_draft(source,10000);document["draft"]=draft;
            if(draft.dump().size()>10000||!wsprrypi::valid_controller_document(document,"configuration-recovery")||draft["CW"]["Message"]["saved_text"]!=nullptr)
                throw std::runtime_error("Recovery document must remain bounded without truncating or normalizing saved fields");
            bool bounded=false;try{(void)wsprrypi::pi_configuration_recovery_draft(source,1);}catch(...){bounded=true;}
            if(!bounded)throw std::runtime_error("Unrepresentable recovery envelope must refuse");
        }
        for(const auto *resource:{"pi-config","pi-scheduling","stop","migration"}){
            auto capability=nlohmann::json::parse(wsprrypi::pi_resource_descriptor(resource));
            capability.update({{"resource",resource},{"read",true},{"write",false},{"activation","unsupported"},{"maximum_bytes",32768}});
            if(!wsprrypi::valid_pi_resource_capability(capability))throw std::runtime_error("Current Pi descriptor refused");
            for(const auto &change:{nlohmann::json{{"schema_digest",std::string(64,'0')}},nlohmann::json{{"maximum_bytes",32769}},nlohmann::json{{"write",true}},nlohmann::json{{"extra",true}}}){
                auto invalid=capability;invalid.update(change);
                if(wsprrypi::valid_pi_resource_capability(invalid))throw std::runtime_error("Changed Pi descriptor admitted");
            }
        }
        for(const auto &item:vectors.at("cases"))if(item.at("name")=="Pi closed structural save"){
            auto value=item.at("value");
            value["candidate"]["scheduling"]["solar"]["day"]["cw_base_hz"]=std::string(24000,'1');
            if(!wsprrypi::valid_controller_document(value,"target-save"))throw std::runtime_error("Bounded decimal shape must not depend on regex stack depth");
            value["candidate"]["scheduling"]["solar"]["day"]["cw_base_hz"]=std::string(24000,'1')+".0";
            if(wsprrypi::valid_controller_document(value,"target-save"))throw std::runtime_error("Noncanonical long decimal admitted");
        }
        using Json=nlohmann::json;
        const auto canonical=[](const Json &value){return wsprrypi::canonical_pi_management_json(value);};
        if(canonical(Json{{"z",-0.0},{"a",Json::array({1,1.0,1u})}})!=R"({"a":[1,1,1],"z":0})")
            throw std::runtime_error("Canonical integral and signed-zero equivalence");
        if(canonical(Json{{"text",std::string("\b\t\n\f\r\\n\"/")}})!=R"({"text":"\u0008\u0009\u000a\u000c\u000d\\n\"/"})")
            throw std::runtime_error("Canonical controls and escaped backslashes");
        if(canonical(Json{{"é",2},{"a",1}})!=R"({"a":1,"é":2})"||
            canonical(Json::array({1e-7,1e20}))!="[1e-07,1e+20]")
            throw std::runtime_error("Canonical UTF-8 order or pinned exponent spelling");
        for(const Json &value:{Json(std::numeric_limits<double>::quiet_NaN()),Json(std::numeric_limits<double>::infinity()),Json(9007199254740992ULL),Json(-9007199254740992LL)}) {
            bool refused=false;try{(void)canonical(value);}catch(...){refused=true;}
            if(!refused)throw std::runtime_error("Canonical nonfinite/unsafe integer admitted");
        }
        for(const auto method:{"GET","POST","PUT","DELETE"})if(wsprrypi::controller_pico_route("jobs",method)||wsprrypi::controller_pico_route("../station",method))throw std::runtime_error("Unreviewed route admitted");
        for(const auto &item:vectors.at("recovery_cases"))if(wsprrypi::controller_pico_recovery_read(item.at("resource").get<std::string>())!=item.at("read").get<std::string>())throw std::runtime_error("Recovery mapping drift");
        if(!wsprrypi::controller_pico_route("station","PUT")||wsprrypi::controller_pico_route("station","POST"))throw std::runtime_error("Route method drift");
        const auto error=wsprrypi::normalize_controller_error({503,R"({"error":{"code":"untrusted secret detail","password":"sensitive"}})",{}});
        const auto public_error=nlohmann::json::parse(error.body);
        if(!wsprrypi::valid_controller_document(public_error,"target-error"))throw std::runtime_error("Controller error contract drift");
        if(public_error!=nlohmann::json{{"contract","Wsprry-Control/1"},{"schema","target-error"},
            {"error",{{"code","transport_unavailable"},{"retryable",false}}}})throw std::runtime_error("Untrusted controller error exposed");
        std::cout<<"PASS: "<<count<<" authoritative closed controller vectors and exact Pico route methods\n";
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
