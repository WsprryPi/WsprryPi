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
    const auto id=[](char value){return std::string(32,value);};
    Json registration{{"contract","Wsprry-Pair/1"},{"schema","authority-register"},{"operation_id",id('e')},{"pair_id",id('f')},
        {"authorization_generation","1"},{"target_id",self},{"controller_id",id('b')},{"controller_incarnation",id('c')},{"controller_generation","1"},
        {"assignments",Json::array({{{"assignment_id",id('d')},{"assignment_revision",std::string(64,'e')},{"state","legacy"}}})},
        {"endpoint",{{"host","wsprry-management-"+id('b')+".local."},{"port",31418}}}};
    Json receipt{{"contract","Wsprry-Pair/1"},{"schema","operation"},{"operation_id",id('e')},{"pair_id",id('f')},{"authorization_generation","1"},
        {"request_digest",std::string(64,'1')},{"state","committed"},{"result_digest",std::string(64,'2')}};
    const auto registered=recurrence_authority_registration(initial,registration,receipt,id('9'));
    assert(registered["management"]["complete"]==false&&registered["state"]==initial["state"]&&registered["controllers"]==initial["controllers"]);
    assert(registered["management"]["authorities"].size()==1&&registered["management"]["operations"].size()==1);
    assert(recurrence_authority_registration(registered,registration,receipt,id('9'))==registered);
    assert(parse_recurrence_authority_record(registered.dump(),self)==registered);
    auto known=initial;known["clean_proof"]={{"installation_id",id('8')},{"kind","never_initialized"},{"attestation_id",id('7')}};
    const auto complete=recurrence_authority_registration(known,registration,receipt,id('9'));
    assert(complete["management"]["complete"]==true&&complete["management"]["history_id"]==id('8'));
    bad=registered;bad["management"]["complete"]=true;invalid(bad);
    bad=registered;bad["management"]["authorities"].push_back(bad["management"]["authorities"][0]);invalid(bad);
    bad=registered;bad["management"]["operations"].push_back(bad["management"]["operations"][0]);invalid(bad);
    const auto refuse_registration=[&](const Json &before,const Json &intent,const Json &ack){
        bool refused=false;try{(void)recurrence_authority_registration(before,intent,ack,id('9'));}catch(...){refused=true;}assert(refused);
    };
    auto wrong_registration=registration;wrong_registration["endpoint"]["host"]="127.0.0.1";refuse_registration(initial,wrong_registration,receipt);
    auto wrong_receipt=receipt;wrong_receipt["pair_id"]=id('8');refuse_registration(initial,registration,wrong_receipt);
    wrong_receipt=receipt;wrong_receipt["request_digest"]=std::string(64,'3');refuse_registration(registered,registration,wrong_receipt);
    wrong_registration=registration;wrong_registration["controller_id"]=self;refuse_registration(initial,wrong_registration,receipt);
    auto edited=registration;edited["operation_id"]=id('6');edited["assignments"][0]["assignment_revision"]=std::string(64,'f');
    auto edited_receipt=receipt;edited_receipt["operation_id"]=id('6');edited_receipt["request_digest"]=std::string(64,'6');
    const auto changed_assignment=recurrence_authority_registration(registered,edited,edited_receipt,id('9'));
    assert(changed_assignment["management"]["authorities"][0]["assignments"][0]["assignment_revision"]==std::string(64,'f'));
    assert(recurrence_authority_registration(changed_assignment,registration,receipt,id('9'))==changed_assignment);
    auto update=registration;update["operation_id"]=id('1');update["controller_generation"]="2";update["assignments"][0]["state"]="retired";
    auto acknowledgement=receipt;acknowledgement["operation_id"]=id('1');acknowledgement["request_digest"]=std::string(64,'4');
    const auto retired=recurrence_authority_registration(registered,update,acknowledgement,id('9'));
    update["operation_id"]=id('2');update["controller_generation"]="3";update["assignments"][0]["state"]="legacy";acknowledgement["operation_id"]=id('2');
    refuse_registration(retired,update,acknowledgement);
    update["assignments"]=Json::array();const auto omitted=recurrence_authority_registration(retired,update,acknowledgement,id('9'));
    assert(omitted["management"]["authorities"][0]["assignments"].size()==1&&omitted["management"]["authorities"][0]["assignments"][0]["state"]=="retired");
    update["operation_id"]=id('3');update["controller_generation"]="1";acknowledgement["operation_id"]=id('3');refuse_registration(omitted,update,acknowledgement);
    auto replaced=registration;replaced["operation_id"]=id('4');replaced["pair_id"]=id('5');replaced["authorization_generation"]="3";
    acknowledgement["operation_id"]=id('4');acknowledgement["pair_id"]=id('5');acknowledgement["authorization_generation"]="3";
    const auto replacement=recurrence_authority_registration(omitted,replaced,acknowledgement,id('9'));
    assert(replacement["management"]["authorities"].size()==2&&replacement["management"]["complete"]==false);
    assert(replacement["controllers"]==initial["controllers"]&&replacement["retirements"]==initial["retirements"]&&replacement["jobs"]==initial["jobs"]);
    const auto plan=recurrence_retirement_plan(complete,id('0'));
    assert(plan["generation"]=="2"&&plan["controllers"].size()==1&&plan["requests"].size()==1);
    assert(plan["requests"][0]["request"]["pair_id"]==id('f')&&plan["requests"][0]["request"]["retirement_generation"]=="2");
    const auto refuse_plan=[&](const Json &record){bool refused=false;try{(void)recurrence_retirement_plan(record,id('0'));}catch(...){refused=true;}assert(refused);};
    refuse_plan(registered); // Pairing or a complete browser list cannot attest an unknown installation.
    auto versioned=replaced;versioned["assignments"][0]["assignment_revision"]=std::string(64,'0');
    const auto known_replacement=recurrence_authority_registration(complete,versioned,acknowledgement,id('9'));
    const auto retained=recurrence_retirement_plan(known_replacement,id('0'));
    assert(retained["controllers"].size()==1&&retained["controllers"][0]["assignment_revision"]==std::string(64,'0'));
    assert(retained["requests"][0]["request"]["pair_id"]==id('f')&&retained["requests"][0]["covered"].size()==2);
    assert(retained["requests"][0]["covered"][0]["assignment_revision"]==std::string(64,'e'));
    assert(retained["requests"][0]["covered"][1]["assignment_revision"]==std::string(64,'0'));
    auto original_again=registration;original_again["operation_id"]=id('1');original_again["assignments"][0]["assignment_revision"]=std::string(64,'7');
    auto latest_ack=receipt;latest_ack["operation_id"]=id('1');latest_ack["request_digest"]=std::string(64,'3');
    const auto original_updated=recurrence_authority_registration(known_replacement,original_again,latest_ack,id('9'));
    const auto reordered=recurrence_retirement_plan(original_updated,id('0'));
    assert(reordered["requests"][0]["request"]["pair_id"]==id('f')&&reordered["controllers"][0]["assignment_revision"]==std::string(64,'7'));
    auto ambiguous=original_updated;ambiguous["management"]["operations"]=Json::array();refuse_plan(ambiguous);
    auto orphan=complete;orphan["controllers"].push_back({{"controller_id",id('b')},{"assignment_id",id('6')},{"assignment_revision",std::string(64,'7')}});refuse_plan(orphan);
    auto exhausted=complete;exhausted["generation"]="18446744073709551615";refuse_plan(exhausted);
    assert(complete["controllers"].empty()&&complete["management"]["authorities"].size()==1); // Preparation has no mutation.
    auto frozen=complete;frozen["state"]="retiring";frozen["generation"]=plan["generation"];frozen["transition_id"]=id('0');frozen["controllers"]=plan["controllers"];
    auto requests=plan["requests"];for(auto &row:requests){row["request"]["proof"]=std::string(43,'A');row["state"]="prepared";row["response"]=nullptr;}
    frozen["management"]["handover"]={{"schema","pi-remote-handover/1"},{"generation",plan["generation"]},{"transition_id",id('0')},
        {"candidate_digest",std::string(64,'9')},{"inventory",complete["management"]["authorities"]},{"operations",complete["management"]["operations"]},{"resume_basis",nullptr},
        {"controllers",plan["controllers"]},{"requests",requests}};
    assert(parse_recurrence_authority_record(frozen.dump(),self)==frozen);
    for(const auto *field:{"generation","transition_id","candidate_digest","inventory","operations","resume_basis","controllers","requests"}){
        bad=frozen;bad["management"]["handover"].erase(field);invalid(bad);
    }
    bad=frozen;bad["management"]["handover"]["unknown"]=true;invalid(bad);
    bad=frozen;bad["management"]["handover"]["requests"][0]["state"]="confirmed";invalid(bad);
    bad=frozen;bad["management"]["handover"]["requests"][0]["request"]["assignment_revision"]=std::string(64,'0');invalid(bad);
    bad=frozen;bad["management"]["handover"]["requests"][0]["covered"]=Json::array();invalid(bad);
    bad=frozen;bad["management"]["handover"]["generation"]="3";invalid(bad);
    auto attempted=frozen;attempted["management"]["handover"]["requests"][0]["state"]="awaiting_receipt";
    assert(parse_recurrence_authority_record(attempted.dump(),self)==attempted);
    auto confirmed=attempted;auto response=requests[0]["request"];response["schema"]="retired";response["receipt_digest"]=std::string(64,'8');
    confirmed["management"]["handover"]["requests"][0].update({{"state","confirmed"},{"response",response}});
    confirmed["retirements"].push_back({{"controller_id",response["controller_id"]},{"assignment_id",response["assignment_id"]},
        {"transition_id",response["transition_id"]},{"retirement_generation",response["retirement_generation"]},{"receipt_digest",response["receipt_digest"]}});
    assert(parse_recurrence_authority_record(confirmed.dump(),self)==confirmed);
    assert(!recurrence_retirement_scope_closed(initial,id('f'),"1"));
    assert(!recurrence_retirement_scope_closed(attempted,id('f'),"1"));
    assert(!recurrence_retirement_scope_closed(confirmed,id('f'),"1")); // Retirement still pending.
    auto closed_scope=confirmed;closed_scope["state"]="staged";closed_scope["generation"]="3";closed_scope["staged_revision"]=std::string(64,'7');
    assert(valid_recurrence_authority_record(closed_scope,self));
    assert(recurrence_retirement_scope_closed(closed_scope,id('f'),"1"));
    assert(!recurrence_retirement_scope_closed(closed_scope,id('f'),"2"));
    assert(!recurrence_retirement_scope_closed(closed_scope,id('4'),"1"));
    auto incomplete_scope=closed_scope;incomplete_scope["management"]["complete"]=false;
    assert(!recurrence_retirement_scope_closed(incomplete_scope,id('f'),"1"));
    const auto repeated_plan=recurrence_retirement_plan(confirmed,id('8'));
    const auto reusable=recurrence_reusable_retirement(confirmed,repeated_plan["requests"][0]["request"]);
    assert(!reusable.is_null()&&reusable["request"]==confirmed["management"]["handover"]["requests"][0]["request"]);
    // Re-pairing the same controller does not reinterpret its permanent
    // fence under the new pair. Reuse the original signed proof and cover
    // both authenticated registrations until an explicit resume selects a scope.
    auto re_paired_registration=replaced;re_paired_registration["assignments"][0]["state"]="retired";
    const auto re_paired=recurrence_authority_registration(closed_scope,re_paired_registration,acknowledgement,id('9'));
    const auto re_paired_plan=recurrence_retirement_plan(re_paired,id('8'));
    assert(re_paired_plan["requests"][0]["request"]["pair_id"]==id('f')&&re_paired_plan["requests"][0]["covered"].size()==2);
    assert(recurrence_reusable_retirement(re_paired,re_paired_plan["requests"][0]["request"])==reusable);
    auto repeated=confirmed;repeated["generation"]=repeated_plan["generation"];repeated["transition_id"]=id('8');
    repeated["management"]["handover"]["generation"]=repeated_plan["generation"];repeated["management"]["handover"]["transition_id"]=id('8');
    assert(valid_recurrence_authority_record(repeated,self)&&repeated["retirements"]==confirmed["retirements"]);
    bad=repeated;bad["management"]["handover"]["requests"][0]["state"]="prepared";invalid(bad);
    bad=confirmed;bad["retirements"][0]["receipt_digest"]=std::string(64,'7');invalid(bad);
    bad=confirmed;bad["management"]["handover"]["requests"][0]["response"]["authorization_generation"]="2";invalid(bad);
    // Private grant shape is not cryptographic authority. The live owner must
    // supply the actual previous disabled receipt and verified source proof.
    auto pending_resume=confirmed;pending_resume["generation"]="4";pending_resume["state"]="reconciling";pending_resume["transition_id"]=id('6');
    const Json disabled_receipt={{"contract","Wsprry-Control/1"},{"schema","target-operation"},
        {"binding",{{"device_id",self},{"boot_id",id('2')},{"profile_generation","1"},{"connection_generation","1"}}},
        {"operation_id",id('7')},{"request_digest",std::string(64,'8')},{"state","committed"},{"revision",std::string(64,'1')},{"error",nullptr}};
    Json grant={{"contract","Wsprry-Pair/1"},{"schema","resume-grant"},{"operation_id",id('6')},{"pair_id",id('5')},{"authorization_generation","3"},
        {"target_id",self},{"target_boot_id",id('4')},{"controller_id",id('b')},{"controller_incarnation",id('c')},{"assignment_id",id('d')},
        {"assignment_revision",std::string(64,'e')},{"transition_id",id('0')},{"retirement_receipt_digest",std::string(64,'8')},{"authority_generation","4"},
        {"disabled_revision",std::string(64,'1')},{"disable_receipt_digest",std::string(64,'d')},{"candidate_digest",std::string(64,'2')},{"grant_digest",std::string(64,'3')},
        {"wtp_binding",{{"revocation_generation","0"},{"wtp_port",31417},{"management_port",31415}}}};
    pending_resume["management"]["resume"]={{"schema","pi-remote-resume/1"},{"grant",grant},
        {"disabled_snapshot",{{"candidate_digest",std::string(64,'2')},{"configuration_generation","1"},{"receipt",disabled_receipt}}},
        {"retirement",{{"request",requests[0]["request"]},{"response",response}}},{"state","pending"},{"acknowledgement",nullptr},{"transport",nullptr},{"registered",nullptr}};
    assert(parse_recurrence_authority_record(pending_resume.dump(),self)==pending_resume);refuse_plan(pending_resume);
    bad=pending_resume;bad["state"]="native";invalid(bad);
    bad=pending_resume;bad["management"]["resume"]["retirement"]["response"]["receipt_digest"]=std::string(64,'9');invalid(bad);
    bad=pending_resume;bad["management"]["resume"]["disabled_snapshot"]["receipt"]["state"]="pending";invalid(bad);
    bad=pending_resume;bad["management"]["resume"]["extra"]=true;invalid(bad);
    auto resumed=pending_resume;resumed["state"]="rollback";resumed["generation"]="5";auto &reservation=resumed["management"]["resume"];
    reservation["state"]="resumed";reservation["transport"]={{"pair_id",id('5')},{"authorization_generation","3"}};
    reservation["acknowledgement"]={{"contract","Wsprry-Pair/1"},{"schema","resume-ack"},{"operation_id",id('6')},{"pair_id",id('5')},{"authorization_generation","3"},
        {"target_id",self},{"controller_id",id('b')},{"assignment_id",id('d')},{"grant_digest",std::string(64,'3')},{"outcome","resumed"},
        {"controller_generation","5"},{"receipt_digest",std::string(64,'9')},{"proof",std::string(43,'A')}};
    assert(valid_recurrence_authority_record(resumed,self)&&recurrence_remote_resume_active(resumed));refuse_plan(resumed); // Wait for the actual refreshed source version, never guess it.
    assert(!recurrence_retirement_scope_closed(pending_resume,id('5'),"3"));
    assert(!recurrence_retirement_scope_closed(resumed,id('5'),"3"));
    bad=resumed;bad["state"]="staged";invalid(bad); // Old retirement proof predates consumption.
    auto resumed_registration=versioned;resumed_registration["controller_generation"]="5";resumed_registration["assignments"][0]["state"]="rollback";
    const auto registered_resume=recurrence_authority_registration(resumed,resumed_registration,acknowledgement,id('9'));
    assert(!registered_resume["management"]["resume"]["registered"].is_null());
    const auto resume_plan=recurrence_retirement_plan(registered_resume,id('9'));
    assert(recurrence_reusable_retirement(registered_resume,resume_plan["requests"][0]["request"]).is_null());
    assert(resume_plan["requests"][0]["request"]["pair_id"]==id('5')&&resume_plan["requests"][0]["request"]["authorization_generation"]=="3"&&
        resume_plan["controllers"][0]["assignment_revision"]==std::string(64,'0'));
    auto fresh_handover=registered_resume;fresh_handover["state"]="retiring";fresh_handover["generation"]=resume_plan["generation"];
    fresh_handover["transition_id"]=id('9');fresh_handover["controllers"]=resume_plan["controllers"];fresh_handover["retirements"]=Json::array();
    auto fresh_requests=resume_plan["requests"];for(auto &row:fresh_requests){row["request"]["proof"]=std::string(43,'A');row["state"]="prepared";row["response"]=nullptr;}
    fresh_handover["management"]["handover"]={{"schema","pi-remote-handover/1"},{"generation",resume_plan["generation"]},{"transition_id",id('9')},
        {"candidate_digest",std::string(64,'9')},{"inventory",registered_resume["management"]["authorities"]},{"operations",registered_resume["management"]["operations"]},
        {"resume_basis",registered_resume["management"]["resume"]},{"controllers",resume_plan["controllers"]},{"requests",fresh_requests}};
    assert(valid_recurrence_authority_record(fresh_handover,self)&&!recurrence_remote_resume_active(fresh_handover));
    bad=fresh_handover;bad["management"]["handover"]["resume_basis"]=nullptr;invalid(bad); // Never reinterpret a frozen callback under the original pair.
    // Later registrations cannot rewrite the frozen intent or its source credential.
    auto updated=recurrence_authority_registration(frozen,versioned,acknowledgement,id('9'));
    assert(updated["management"]["handover"]==frozen["management"]["handover"]&&updated["management"]["authorities"].size()==2);
    auto full=complete;full["controllers"].push_back({{"controller_id",self},{"assignment_id",id('a')},{"assignment_revision",std::string(64,'a')}});
    auto &assignments=full["management"]["authorities"][0]["assignments"];
    for(unsigned index=1;index<31;++index){auto name=std::string(30,'0');name+="0123456789abcdef"[index/16];name+="0123456789abcdef"[index%16];
        assignments.push_back({{"assignment_id",name},{"assignment_revision",std::string(64,'e')},{"state","legacy"}});}
    assert(recurrence_retirement_plan(full,id('0'))["controllers"].size()==32);
    assignments.push_back({{"assignment_id",id('0')},{"assignment_revision",std::string(64,'e')},{"state","legacy"}});
    refuse_plan(full); // The existing 32-controller bound includes the target's own legacy assignment.
    std::cout<<"PASS: closed authority and path-bound bootstrap records, unknown baseline, canonical generations, identities, duplicates and corrupt/oversized input; no eligibility or I/O\n";
}
