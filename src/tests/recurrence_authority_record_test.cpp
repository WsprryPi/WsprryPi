// SPDX-License-Identifier: MIT
#include "recurrence_authority.hpp"
#include <cassert>
#include <iostream>
using namespace wsprrypi;
using Json=nlohmann::json;
int main(){
    const std::string self(32,'a');const auto initial=unclassified_recurrence_authority(self);
    assert(initial.at("state")=="unclassified"&&initial.at("clean_proof").is_null()&&initial.at("controllers").empty());
    assert(parse_recurrence_authority_record(initial.dump(),self)==initial);
    const auto invalid=[&](const auto &value){assert(!valid_recurrence_authority_record(value,self));};
    auto bad=initial;bad["device_id"]=std::string(32,'b');invalid(bad);
    bad=initial;bad["extra"]=false;invalid(bad);
    bad=initial;bad["generation"]=1;invalid(bad);
    for(const auto *value:{"0","01","-1","18446744073709551616"}){bad=initial;bad["generation"]=value;invalid(bad);}
    bad=initial;bad["generation"]="18446744073709551615";assert(valid_recurrence_authority_record(bad,self));
    bad=initial;bad["state"]="clean";invalid(bad);
    auto record=initial;record["controllers"].push_back({{"controller_id",self},{"assignment_id",std::string(32,'c')},{"assignment_revision",std::string(64,'d')}});
    assert(valid_recurrence_authority_record(record,self));
    bad=record;bad["controllers"].push_back(bad["controllers"][0]);invalid(bad);
    bad=record;bad["controllers"][0]["assignment_revision"]="bad";invalid(bad);
    bad=record;auto conflict=bad["controllers"][0];conflict["assignment_revision"]=std::string(64,'0');
    bad["controllers"].push_back(conflict);invalid(bad);
    auto maximum=initial;
    for(unsigned i=0;i<32;++i){
        auto id=std::string(30,'0');id+="0123456789abcdef"[i/16];id+="0123456789abcdef"[i%16];
        maximum["controllers"].push_back({{"controller_id",self},{"assignment_id",id},{"assignment_revision",std::string(64,'d')}});
    }
    assert(valid_recurrence_authority_record(maximum,self));
    maximum["controllers"].push_back({{"controller_id",self},{"assignment_id",std::string(32,'f')},{"assignment_revision",std::string(64,'d')}});invalid(maximum);
    bad=initial;bad["clean_proof"]={{"installation_id",self},{"kind","empty_catalog"},{"attestation_id",self}};invalid(bad);
    record["retirements"].push_back({{"controller_id",self},{"assignment_id",std::string(32,'c')},{"transition_id",self},{"retirement_generation","1"},{"receipt_digest",std::string(64,'f')}});
    record["jobs"].push_back({{"controller_id",self},{"job_id",self},{"boot_id",self},{"state","unknown"}});
    assert(parse_recurrence_authority_record(record.dump(),self)==record);
    bad=record;bad["jobs"][0]["state"]="idle";invalid(bad);
    bad=record;bad["retirements"][0]["verified"]=true;invalid(bad);
    for(const auto &text:{std::string{},std::string(1048577,'x'),std::string("{\"schema\":0,\"schema\":1}"),std::string("{bad}")}){
        bool refused=false;try{(void)parse_recurrence_authority_record(text,self);}catch(...){refused=true;}assert(refused);
    }
    const std::string binding(64,'b');const auto bootstrap=recurrence_bootstrap_record(self,binding,std::string(32,'c'));
    assert(parse_recurrence_storage_record(bootstrap.dump(),self,binding)==bootstrap);
    assert(parse_recurrence_storage_record(initial.dump(),self,binding)==initial);
    for(const auto &value:{bootstrap.dump(),std::string("{\"schema\":0,\"schema\":1}")}){
        bool refused=false;try{(void)parse_recurrence_storage_record(value,self,std::string(64,'d'));}catch(...){refused=true;}assert(refused);
    }
    auto extra=bootstrap;extra["verified"]=true;
    bool refused=false;try{(void)parse_recurrence_storage_record(extra.dump(),self,binding);}catch(...){refused=true;}assert(refused);
    std::cout<<"PASS: closed authority and path-bound bootstrap records, unknown baseline, canonical generations, identities, duplicates and corrupt/oversized input; no eligibility or I/O\n";
}
