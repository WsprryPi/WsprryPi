// SPDX-License-Identifier: MIT
#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "scheduling.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
std::string bytes(const std::string &path){std::ifstream file(path);return {(std::istreambuf_iterator<char>(file)),{}};}
}
int main(){
 std::string temporary;
 try{
  char pattern[]="/tmp/pi-config-coordinator-XXXXXX";const auto directory=mkdtemp(pattern);check(directory,"Temporary directory");temporary=directory;
  struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}} remove{temporary};
  init_default_config();set_scheduler_execution_suppressed_for_test(true);set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
  config.use_ini=true;config.ini_filename=temporary+"/settings.ini";config.transmit=false;
  config.simulated_backend_override=true;config.transmit_backend=TransmitBackendKind::SIMULATED;
  config.callsign="AA0NT";config.grid_square="EM18";config.power_dbm=20;config.frequencies="20m";
  resolve_backend_specific_config(config);config_to_json();
  {std::ofstream file(config.ini_filename);file<<"; preserved configuration comment\n[Security]\nPrivileged Network Safety = enforced\n";}
  iniFile.set_filename(config.ini_filename);iniFile.set_raw_passthrough_keys({{"Security","Privileged Network Safety"}});json_to_ini();
  const auto before=bytes(config.ini_filename);const auto original=get_public_config_snapshot();
  const nlohmann::json patch={{"CW",{{"Message","COORDINATED"}}}};
  unsigned calls=0;
  const auto refuse=[&](const nlohmann::json &,const std::function<void()> &){++calls;throw std::runtime_error("test reservation failure");};
  try{patch_config_with_persistence_coordinator(patch,original.second,refuse);throw std::logic_error("Missing refusal");}
  catch(const std::runtime_error &error){check(std::string(error.what())=="test reservation failure","Reservation failure propagation");}
  check(calls==1&&bytes(config.ini_filename)==before&&get_public_config_snapshot()==original,"Reservation failure must precede INI and runtime publication");
  try{patch_config_with_persistence_coordinator(patch,"\"stale\"",refuse);throw std::logic_error("Missing stale refusal");}
  catch(const std::runtime_error &error){check(std::string(error.what())=="revision_conflict","Stale revision refusal");}
  check(calls==1&&bytes(config.ini_filename)==before,"Stale revision must precede coordinator");
  bool invalid=false;
  try{patch_config_with_persistence_coordinator({{"CW",{{"Fade Shape","unknown"}}}},original.second,refuse);}
  catch(const std::exception &){invalid=true;}
  check(invalid&&calls==1&&bytes(config.ini_filename)==before,"Owning semantic validation must precede coordinator");
  bool skipped=false;
  try{patch_config_with_persistence_coordinator(patch,original.second,[](const auto &,const auto &){});}
  catch(const std::logic_error &){skipped=true;}
  check(skipped&&get_public_config_snapshot()==original&&bytes(config.ini_filename)==before,"Skipping persistence must not publish runtime state");
  const auto revision=patch_config_with_persistence_coordinator(patch,original.second,[&](const auto &candidate,const auto &persist){
   ++calls;check(candidate.at("CW").at("Message")=="COORDINATED","Coordinator receives normalized candidate");
   check(bytes(config.ini_filename)==before&&get_public_config_snapshot()==original,"No publication before durable owner callback");
   persist();check(bytes(config.ini_filename)!=before,"Existing INI writer invoked");
   check(get_public_config_snapshot()==original,"Runtime publication follows persistence owner completion");
   bool repeated=false;try{persist();}catch(const std::logic_error &){repeated=true;}check(repeated,"No repeated INI persistence attempt");
  });
  const auto after=get_public_config_snapshot();check(after.second==revision&&revision!=original.second,"Published revision");
  check(after.first.at("CW").at("Message")=="COORDINATED"&&calls==2,"Published candidate");
  const auto saved=bytes(config.ini_filename);check(saved.find("; preserved configuration comment")!=std::string::npos&&saved.find("Privileged Network Safety = enforced")!=std::string::npos,"Existing INI layout and safety owner preserved");
  bool uncertain=false;
  try{patch_config_with_persistence_coordinator({{"CW",{{"Message","UNCERTAIN"}}}},revision,[](const auto &,const auto &persist){persist();throw std::runtime_error("post-persist fault");});}
  catch(const std::runtime_error &error){uncertain=std::string(error.what())=="post-persist fault";}
  check(uncertain&&bytes(config.ini_filename)!=saved&&get_public_config_snapshot()==after,"A post-persist owner failure must propagate without publishing success or runtime state");
  check(!config.transmit&&config.transmit_backend==TransmitBackendKind::SIMULATED,"No output or backend activation");
  std::cout<<"PASS: owning validation/revision guards, pre-write coordinator refusal, mandatory single persistence attempt, normalized candidate, and publication order without hardware\n";
 }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
