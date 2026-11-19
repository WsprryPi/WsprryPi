// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_managed_records.hpp"
#include "wtp_integration/controller_contract.hpp"
#include "wtp_settings_json.hpp"
#include "wtp_server_settings_json.hpp"
#include "wtp_retirement.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include <fstream>
#include <iostream>
#include <limits>
using namespace wsprrypi;
using Json=nlohmann::json;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void refuses(F action){bool refused=false;try{action();}catch(...){refused=true;}check(refused,"Expected record refusal");}
}
int main(int argc,char **argv){
 if(argc!=2)return 2;
 try{
  std::ifstream input(argv[1]);Json vectors;input>>vectors;Json request;
  for(const auto &entry:vectors["cases"])if(entry["name"]=="Pi closed structural save")request=entry["value"];
  check(request.is_object(),"Authoritative complete candidate required");
  const auto self=request["binding"]["device_id"].get<std::string>();
  const auto route="/api/control/v1/targets/"+self+"/settings";
  const auto private_digest=PiManagementJournal::digest("preserved private INI state");
  const auto saved=pi_managed_settings_record(self,"1",request["candidate"],private_digest);
  check(valid_pi_managed_settings_record(saved,self,private_digest),"Approved private settings shape and canonical digests");
  // Storage recognition preserves the original sealed bytes and hashes;
  // the single current control view refuses these retired fields.
  auto old_candidate=request["candidate"];
  old_candidate["configuration"]["WTP"]=wtp_settings_json(WtpSettings{});
  auto dormant_listener=WtpServerSettings{};dormant_listener.enabled=false;
  old_candidate["configuration"]["WTP Server"]=wtp_server_settings_json(dormant_listener);
  old_candidate["configuration"]["WTP"]["Hostname"]="retained-custom.local";
  old_candidate["configuration"]["WTP"]["TLS Client Key"]="/private/retained-key";
  check(valid_pi_stored_candidate_shape(old_candidate)&&!valid_pi_candidate_shape(old_candidate),"Stored legacy fields are not a current control view");
  refuses([&]{(void)pi_managed_settings_record(self,"1",old_candidate,private_digest);});
  auto original=saved;original["configuration"]=old_candidate["configuration"];
  const auto old_digest=PiManagementJournal::digest(canonical_pi_management_json(old_candidate));
  original["candidate_digest"]=old_digest;
  original["revision"]=PiManagementJournal::digest(canonical_pi_management_json(Json{{"device_id",self},{"configuration_generation","1"},
      {"candidate_digest",old_digest},{"private_state_digest",private_digest}}));
  const auto bytes=original.dump();
  check(parse_pi_managed_settings_record(bytes,self,private_digest)==original,"Exact legacy checksum recognized without rewriting");
  const auto projected=current_pi_settings_record(original);
  check(projected["revision"]==original["revision"]&&projected["configuration"]==request["candidate"]["configuration"]&&
      original.dump()==bytes,"Current projection preserves sealed storage and its exact revision");
  auto malformed=old_candidate;malformed["configuration"]["WTP"]["unexpected"]=true;
  check(!valid_pi_stored_candidate_shape(malformed),"Stored legacy validation remains closed");
  auto tampered=original;tampered["configuration"]["WTP"]["TLS Client Key"]="/private/other";
  check(!valid_pi_managed_settings_record(tampered,self,private_digest),"Retained private legacy fields remain hash bound");
  old_candidate["configuration"]["Operation"]["Transmit Backend"]="wtp";
  check(valid_pi_stored_candidate_shape(old_candidate)&&wtp_retirement_source_active(old_candidate["configuration"]),"Active stored legacy state is recognized for a hold, never execution");
  check(saved.size()==7&&!saved.contains("private_state_digest"),"Private-preservation digest remains outside the saved/public candidate");
  auto equivalent=request["candidate"],integer=equivalent;
  equivalent["configuration"]["GPIO"]["Manual PPM"]=1.0;integer["configuration"]["GPIO"]["Manual PPM"]=1;
  check(pi_managed_settings_record(self,"1",equivalent,private_digest)["revision"]==pi_managed_settings_record(self,"1",integer,private_digest)["revision"],"Integral float/integer revision equivalence");
  auto changed=request["candidate"];changed["configuration"]["CW"]["Message"]="OTHER";
  const auto middle=pi_managed_settings_record(self,"2",changed,private_digest);
  const auto returned=pi_managed_settings_record(self,"3",request["candidate"],private_digest);
  check(saved["candidate_digest"]==returned["candidate_digest"]&&saved["revision"]!=returned["revision"]&&middle["revision"]!=returned["revision"],"A/B/A generations do not reuse revisions");
  const auto private_change=pi_managed_settings_record(self,"1",request["candidate"],PiManagementJournal::digest("different private state"));
  check(private_change["candidate_digest"]==saved["candidate_digest"]&&private_change["revision"]!=saved["revision"],"Preserved private fields participate in revision");
  for(const auto &generation:{"0","01","-1","18446744073709551616"})refuses([&]{(void)pi_managed_settings_record(self,generation,request["candidate"],private_digest);});
  check(valid_pi_managed_settings_record(pi_managed_settings_record(self,"18446744073709551615",request["candidate"],private_digest),self,private_digest),"Maximum generation stays exact");
  auto corrupt=saved;corrupt["configuration"]["CW"]["Message"]="TAMPER";check(!valid_pi_managed_settings_record(corrupt,self,private_digest),"Stored settings tamper refusal");
  corrupt=saved;corrupt["unexpected"]=false;check(!valid_pi_managed_settings_record(corrupt,self,private_digest),"Closed settings record");
  check(!valid_pi_managed_settings_record(saved,std::string(32,'f'),private_digest)&&!valid_pi_managed_settings_record(saved,self,std::string(64,'0')),"Identity/private-state mismatch refusal");
  auto invalid=request["candidate"];invalid["configuration"]["CW"]["Base Frequency"]=std::numeric_limits<double>::infinity();
  refuses([&]{(void)pi_managed_settings_record(self,"1",invalid,private_digest);});
  const auto digest=pi_managed_request_digest(self,self,"1",route,request);
  auto equivalent_request=request;equivalent_request["candidate"]=integer;auto float_request=request;float_request["candidate"]=equivalent;
  check(pi_managed_request_digest(self,self,"1",route,equivalent_request)==pi_managed_request_digest(self,self,"1",route,float_request),"Request hashing follows the same canonical candidate rules");
  check(digest!=pi_managed_request_digest(self,std::string(32,'f'),"1",route,request)&&digest!=pi_managed_request_digest(self,self,"2",route,request),"Request binds principal and authorization generation");
  refuses([&]{(void)pi_managed_request_digest(self,self,"1",route+"?other",request);});
  refuses([&]{(void)pi_managed_request_digest(self,self,"1","/api/control/v1/targets/"+self+"/enable",request);});
  const std::string recovery_route="/api/control/v1/host/configuration-recovery";
  for(const auto &test:vectors["cases"]){
   if(test["name"]!="P209 complete recovery save"&&test["name"]!="P209 explicit recovery repair"&&test["name"]!="P209 explicit recovery reset")continue;
   auto recovery=test["value"];recovery["binding"]["device_id"]=self;
   const auto recovery_hash=pi_managed_request_digest(self,self,"1",recovery_route,recovery);
   check(recovery_hash.size()==64&&recovery_hash!=digest,"Recovery commands have independently bound request hashes");
   auto changed_context=recovery;changed_context["binding"]["boot_id"]=std::string(32,'f');
   check(recovery_hash!=pi_managed_request_digest(self,self,"1",recovery_route,changed_context),"Recovery digest binds its boot context");
   changed_context=recovery;changed_context["binding"]["device_id"]=std::string(32,'f');
   refuses([&]{(void)pi_managed_request_digest(self,self,"1",recovery_route,changed_context);});
   changed_context=recovery;changed_context["schema"]="configuration-recovery";
   refuses([&]{(void)pi_managed_request_digest(self,self,"1",recovery_route,changed_context);});
   refuses([&]{(void)pi_managed_request_digest(self,self,"1",route,recovery);});
  }
  const auto entry=[&](unsigned index,bool pending){
   auto operation=request;operation["operation_id"]=PiManagementJournal::digest(std::to_string(index)).substr(0,32);
   const auto hash=pi_managed_request_digest(self,self,"1",route,operation);
   return Json{{"principal_id",self},{"route",route},{"authorization_generation","1"},{"request_digest",hash},
    {"receipt",{{"contract","Wsprry-Control/1"},{"schema","target-operation"},{"binding",operation["binding"]},
      {"operation_id",operation["operation_id"]},{"request_digest",hash},{"state",pending?"pending":"committed"},
      {"revision",pending?Json(nullptr):saved["revision"]},{"error",nullptr}}}};
  };
  Json entries=Json::array();for(unsigned i=0;i<256;++i)entries.push_back(entry(i,false));
  for(unsigned i=0;i<32;++i)entries.push_back(entry(256+i,true));
  const auto full=pi_operations_record(self,entries);check(!full.evicted&&valid_pi_operations_record(full.document,self),"256 terminal plus 32 unresolved records");
  check(full.document.dump().size()>32768&&parse_pi_operations_record(full.document.dump(),self)==full.document,"Bounded private history is not truncated to the HTTP body limit");
  check(parse_pi_managed_settings_record(saved.dump(),self,private_digest)==saved,"Strict private settings decoding");
  refuses([&]{(void)parse_pi_operations_record("{\"schema\":\"duplicate\","+full.document.dump().substr(1),self);});
  auto duplicated=full.document.dump();const auto revision_key=duplicated.find("\"revision\":");check(revision_key!=std::string::npos,"Nested receipt revision");
  duplicated.insert(revision_key,"\"revision\":null,");refuses([&]{(void)parse_pi_operations_record(duplicated,self);});
  refuses([&]{(void)parse_pi_operations_record(std::string(1048577,' '),self);});
  refuses([&]{(void)parse_pi_managed_settings_record("{broken secret data",self,private_digest);});
  auto unknown=entries;unknown[256]["receipt"]["state"]="unknown";unknown[256]["receipt"]["error"]={{"code","operation_unknown"},{"retryable",false}};
  check(valid_pi_operations_record(pi_operations_record(self,unknown).document,self),"Unknown records are retained as unresolved");
  entries.push_back(entry(999,false));const auto pruned=pi_operations_record(self,entries);
  check(pruned.evicted&&pruned.document["receipts"].size()==288&&pruned.document["receipts"].front()==entries[1],"Only the oldest terminal receipt is evicted");
  unsigned pending=0;for(const auto &row:pruned.document["receipts"])if(row["receipt"]["state"]=="pending")++pending;
  check(pending==32&&valid_pi_operations_record(pruned.document,self),"No unresolved receipt eviction");
  auto disabled=entry(2000,false);disabled["disabled_snapshot"]={{"candidate_digest",saved["candidate_digest"]},{"configuration_generation","1"}};
  const auto proof=pi_operations_record(self,Json::array({disabled}));check(valid_pi_operations_record(proof.document,self),"Closed committed disabled-snapshot metadata has no self-referential receipt hash");
  corrupt=proof.document;corrupt["receipts"][0]["disabled_snapshot"]["configuration_generation"]="0";
  check(!valid_pi_operations_record(corrupt,self),"Disabled snapshot requires a positive canonical settings generation");
  corrupt=proof.document;corrupt["receipts"][0]["disabled_snapshot"]["configuration"]={};
  check(!valid_pi_operations_record(corrupt,self),"Disabled snapshot does not retain a request/configuration body or arbitrary private fields");
  corrupt=proof.document;corrupt["receipts"][0]["receipt"]["state"]="pending";corrupt["receipts"][0]["receipt"]["revision"]=nullptr;
  check(!valid_pi_operations_record(corrupt,self),"Pending receipt cannot prove a committed disabled snapshot");
  auto too_many=full.document["receipts"];too_many.push_back(entry(1000,true));const auto before=too_many;
  refuses([&]{(void)pi_operations_record(self,too_many);});check(too_many==before,"Capacity refusal changes no input");
  auto duplicate=Json::array({entry(1,false),entry(1,false)});refuses([&]{(void)pi_operations_record(self,duplicate);});
  auto bad=full.document;bad["receipts"][0]["route"]="/arbitrary/path";check(!valid_pi_operations_record(bad,self),"Route closure");
  bad=full.document;bad["receipts"][0]["request_digest"]=std::string(64,'0');check(!valid_pi_operations_record(bad,self),"Private/public request digest agreement");
  bad=full.document;bad["receipts"][0]["password"]="secret";check(!valid_pi_operations_record(bad,self),"No private request-body or credential field in receipts");
  bad=full.document;bad["receipts"][0]["receipt"]["revision"]=nullptr;check(!valid_pi_operations_record(bad,self),"Committed Pi result requires its authoritative revision");
  bad=full.document;bad["receipts"][0]["receipt"]["error"]={{"code","operation_unknown"},{"retryable",false}};check(!valid_pi_operations_record(bad,self),"A committed Pi operation is not an uncertain failure");
  check(valid_pi_operations_record(pi_operations_record(self,Json::array()).document,self),"Empty inspection record");
  std::cout<<"PASS: exact Pi managed-settings/operations records, canonical principal/revision hashes, A/B/A/private-state binding, closed routes, and terminal/unresolved retention; no authority or service admission granted\n";
 }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
