// SPDX-License-Identifier: MIT
#include "wtp_integration/catalog.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
using namespace wsprrypi;
using Json=nlohmann::json;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
std::string bytes(const std::string &path){std::ifstream file(path);return {(std::istreambuf_iterator<char>(file)),{}};}
}
int main(){
 try{
  char pattern[]="/tmp/pi-management-anchor-XXXXXX";const auto directory=mkdtemp(pattern);check(directory,"Private temporary directory");
  struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}} remove{directory};
  const auto path=std::string(directory)+"/catalog.json";WtpCatalog catalog(path);
  const std::string self(32,'a'),epoch(32,'b');auto anchor=catalog.pi_management_anchor(self,{});
  check(anchor.complete()&&!anchor.read()&&!std::filesystem::exists(path),"An anchor read must not create catalog state");
  bool precommit_failure=false,prepared_verified=false;
  try{anchor.coordinate(epoch,[&](const auto &before,const auto &after){
    check(bytes(path)==before&&Json::parse(before)["pi_management_history"].is_null(),"Authority unchanged before the owning WAL");
    check(Json::parse(after)["pi_management_history"]["history_id"]==epoch,"Exact staged authority after-image");
    prepared_verified=true;
    throw std::runtime_error("fixture precommit failure");
  });}catch(const std::runtime_error &){precommit_failure=true;}
  check(precommit_failure&&prepared_verified&&!anchor.read(),"Failed preparation must not publish an epoch");
  const auto publish=[&](const PiManagementAnchor &selected,const std::string &history,const std::string &file,const std::string &transaction){
    selected.coordinate(history,[&](const auto &before,const auto &after){
      if(before==after)return;
      PiManagementJournal journal(transaction,self,{{"authority",file}});
      journal.prepare({{"authority",PiManagementJournal::digest(before),after}});journal.commit();check(journal.recover(),"Apply authority through the owning WAL");
    });
  };
  publish(anchor,epoch,path,std::string(directory)+"/transaction");check(anchor.read()==std::optional<std::string>(epoch),"Transactionally published epoch");
  const auto original=bytes(path);const auto public_before=catalog.snapshot({});
  publish(anchor,epoch,path,std::string(directory)+"/transaction");check(bytes(path)==original,"Matching anchor coordination must not rewrite state");
  bool conflict=false;try{publish(anchor,std::string(32,'c'),path,std::string(directory)+"/transaction");}catch(const std::runtime_error &){conflict=true;}
  check(conflict&&bytes(path)==original,"An anchored epoch cannot be silently replaced");
  bool foreign=false;try{(void)catalog.pi_management_anchor(std::string(32,'d'),{}).read();}catch(const std::runtime_error &){foreign=true;}
  check(foreign&&bytes(path)==original,"Different machine identity cannot adopt existing history");
  check(catalog.snapshot({})==public_before&&!public_before.first.contains("pi_management_history"),"Private authority must not leak into the public catalog");
  auto corrupt=Json::parse(original);corrupt["pi_management_history"]["unknown"]=true;
  corrupt["checksum"]=wtp_catalog_revision(corrupt).substr(1,64);
  {std::ofstream output(path);output<<corrupt.dump();}
  bool refused=false;try{(void)anchor.read();}catch(const std::runtime_error &){refused=true;}
  check(refused,"Closed anchor shape is mandatory even with a matching checksum");
  {std::ofstream output(path);output<<original;}
  auto legacy=Json{{"version",1},{"profiles",Json::array()}};
  const auto legacy_path=std::string(directory)+"/legacy.json";{std::ofstream output(legacy_path);output<<legacy.dump();}check(!chmod(legacy_path.c_str(),0600),"Legacy permissions");
  WtpCatalog old(legacy_path);auto old_anchor=old.pi_management_anchor(self,{});const auto legacy_bytes=bytes(legacy_path);
  check(!old_anchor.read()&&bytes(legacy_path)==legacy_bytes,"Legacy reads preserve bytes");
  publish(old_anchor,epoch,legacy_path,std::string(directory)+"/legacy-transaction");
  check(old_anchor.read()==anchor.read()&&Json::parse(bytes(legacy_path))["version"]==2,"Explicit publication migrates private storage");
  std::cout<<"PASS: read-only absent/legacy anchor inspection, durable self-bound epochs, no silent replacement, closed storage, and unchanged public catalog\n";
 }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
