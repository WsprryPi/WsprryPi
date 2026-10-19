// SPDX-License-Identifier: MIT
#include "pico_usb_installer.hpp"
#include "pico_usb_installer_state.hpp"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <thread>
#define CHECK(x) do {if(!(x)) throw std::runtime_error(#x);} while(false)
int main() {
  using namespace wsprrypi;
  try {
    char scratch[]="/tmp/wsprrypi-pico-usb-test.XXXXXX";
    CHECK(mkdtemp(scratch)); const std::filesystem::path root(scratch);
    const auto marker=root/"C.json";
    const nlohmann::json record={{"serial","1545B6EFBF78028C"},{"version","fixture"}};
    pico_usb_save_hold(marker,record,true);
    CHECK(pico_usb_read_hold(marker)==record);
    bool refused=false;
    try {pico_usb_save_hold(marker,record,true);} catch(...) {refused=true;}
    CHECK(refused && pico_usb_read_hold(marker)==record);
    pico_usb_save_hold(marker,{{"verified",true}});
    CHECK(pico_usb_read_hold(marker).at("verified")==true);
    chmod(marker.c_str(),0644); refused=false;
    try {(void)pico_usb_read_hold(marker);} catch(...) {refused=true;}
    CHECK(refused); chmod(marker.c_str(),0600);
    pico_usb_remove_hold(marker); CHECK(!std::filesystem::exists(marker));
    CHECK(pico_usb_serial("1545B6EFBF78028C") && !pico_usb_serial("../other"));
    const auto controls=root/"control"; std::filesystem::create_directory(controls);
    CHECK(!pico_usb_any_update_blocked(controls));
    pico_usb_save_hold(controls/"pending.json",record,true);CHECK(pico_usb_any_update_blocked(controls));
    pico_usb_remove_hold(controls/"pending.json");CHECK(!pico_usb_any_update_blocked(controls));
    { std::ofstream file(root/"not-a-directory"); file<<"invalid"; }
    CHECK(pico_usb_any_update_blocked(root/"not-a-directory"));
    CHECK(!pico_usb_http_mutation("GET","/api/v1/status"));
    CHECK(!pico_usb_http_mutation("POST","/api/v1/member/firmware"));
    for(const auto* path:{"/config","/config/repair","/api/v1/jobs","/api/v1/host/devices/use","/api/v1/member/config"})
      CHECK(pico_usb_http_mutation("POST",path));
    CHECK(pico_usb_http_mutation("PUT","/api/v1/member/firmware"));
    for(const auto* cmd:{"reboot","shutdown","tone_start","tone_end","bounded_tone","stop","unknown"}) CHECK(pico_usb_command_mutation(cmd));
    for(const auto* cmd:{"get_tx_state","wspr_band_catalog","echo"}) CHECK(!pico_usb_command_mutation(cmd));
    const std::string code="import json,sys; print(json.dumps(dict(schema='wsprrypico-usb-result/1',installer_contract=1,installer_version='0.9.3',ok=True,argument=sys.argv[1])))";
    const auto argument="path with spaces; $(literal)";
    const auto result=pico_usb_installer_process({"/usr/bin/python3","-c",code,argument});
    CHECK(result.at("argument")==argument && result.at("ok")==true);
    CHECK(result.at("installer_version")=="0.9.3"); // Compatible fixes do not require another Pi version pin.
    refused=false;
    try {(void)pico_usb_installer_process({"/usr/bin/python3","-c","import json; print(json.dumps(dict(schema='wsprrypico-usb-result/1',installer_contract=2,installer_version='0.9.3',ok=True)))"});} catch(...) {refused=true;}
    CHECK(refused);
    const auto update=pico_usb_public_task({{"result",{{"installer_update",{{"state","retained"},{"reason","GitHub update request rate-limited. Retry after UTC."},{"private","/private/path"}}}}}}).at("result").at("installer_update");
    CHECK(update.at("state")=="retained" && update.contains("reason") && !update.contains("private"));
    refused=false;
    try {(void)pico_usb_installer_process({"/usr/bin/python3","-c","print('{}')"});} catch(...) {refused=true;}
    CHECK(refused);
    const auto orphan=root/"descendant.pid";
    refused=false;
    try {
      (void)pico_usb_installer_process({"/usr/bin/python3","-c",
          "import subprocess,sys; p=subprocess.Popen(['/usr/bin/python3','-c','import time; time.sleep(20)'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL); open(sys.argv[1],'w').write(str(p.pid)); print('{}')",
          orphan.string()});
    } catch(...) {refused=true;}
    CHECK(refused);
    std::ifstream child_record(orphan); int descendant=0; child_record>>descendant; CHECK(descendant>0);
    bool gone=false;
    for(int i=0;i<100;++i) {
      std::ifstream state("/proc/"+std::to_string(descendant)+"/stat"); std::string line; std::getline(state,line);
      const auto end=line.rfind(')');
      if(!state || (end!=std::string::npos && line.substr(end+2,1)=="Z")) {gone=true;break;}
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(gone); // No surviving writer after an invalid/crashed leader.
    refused=false;
    try {(void)pico_usb_installer_process({"/usr/bin/python3","-c",code+"; sys.exit(2)",argument});} catch(...) {refused=true;}
    CHECK(refused);
    const auto public_state=pico_usb_public_task({{"running",false},{"result",{{"ok",false},{"stage","failed"},{"backup_sha256","private"},{"persistent_sha256","private"},{"transaction","/private/transaction"},{"future_private_field","private"},{"error","write failed /private/transaction"}}}});
    CHECK(!public_state.at("result").contains("backup_sha256") && !public_state.at("result").contains("transaction"));
    CHECK(public_state.dump().find("future_private_field")==std::string::npos && public_state.dump().find("/private")==std::string::npos);
    CHECK(pico_usb_public_task({{"result",{{"ok",false},{"error","HTTPS download failed"},{"transaction","/private/transaction"}}}}).at("result").at("error")=="HTTPS download failed");
    const auto rate=pico_usb_public_task({{"result",{{"ok",false},{"error","GitHub API rate limit reached. Retry after 2026-10-18 21:00:00 UTC."},
        {"error_code","github_rate_limited"},{"retry_at",1792357200},{"headers","private"}}}}).at("result");
    CHECK(rate.at("error_code")=="github_rate_limited" && rate.at("retry_at")==1792357200);
    CHECK(!rate.contains("headers") && rate.at("error").get<std::string>().find("UTC")!=std::string::npos);
    for(const nlohmann::json& retry:{nlohmann::json(-1),nlohmann::json("1792357200"),nlohmann::json(1792357200.5),nlohmann::json(true),nlohmann::json(999999999999ULL)}) {
      const auto invalid=pico_usb_public_task({{"result",{{"error_code","github_rate_limited"},{"retry_at",retry}}}}).at("result");
      CHECK(!invalid.contains("retry_at") && !invalid.contains("error_code"));
    }
    CHECK(pico_usb_reconnect_allowed({{"ok",false},{"recovery_required",false}},true));
    CHECK(!pico_usb_reconnect_allowed({{"ok",false},{"recovery_required",false}},false));
    CHECK(!pico_usb_reconnect_allowed({{"ok",true},{"stage","awaiting_bootsel"}},true));
    CHECK(pico_usb_reconnect_allowed({{"ok",true},{"stage","complete"}},false));
    CHECK(pico_usb_public_task({{"result",{{"manual_bootsel_required",true},{"preflight_sha256","private"}}}}).at("result").at("manual_bootsel_required")==true);
    const nlohmann::json reviewed={{"serial","1545B6EFBF78028C"},{"source",std::string(40,'a')},{"version","checked"},{"device_id",std::string(32,'c')}};
    const nlohmann::json waiting={{"ok",true},{"stage","awaiting_bootsel"},{"serial",reviewed.at("serial")},{"source",reviewed.at("source")},
        {"version","checked"},{"manual_bootsel_required",true},{"recovery_required",true},{"transaction","/private/transaction.json"}};
    auto pending=reviewed; pico_usb_record_result(pending,waiting,true);
    CHECK(pending.at("manual_bootsel_required")==true && !pending.value("verified",false));
    pico_usb_save_hold(marker,pending,true); pending=pico_usb_read_hold(marker);
    CHECK(pending.at("transaction")=="/private/transaction.json");
    auto failed_wait=waiting; failed_wait["ok"]=false; failed_wait.erase("manual_bootsel_required");
    pico_usb_record_result(pending,failed_wait,false); CHECK(pending.at("manual_bootsel_required")==true);
    for(const auto* field:{"serial","source","version","transaction","recovery_required"}) {
      auto invalid=waiting; invalid.erase(field); auto held=reviewed; refused=false;
      try {pico_usb_record_result(held,invalid,true);} catch(...) {refused=true;}
      CHECK(refused);
    }
    refused=false;try {pico_usb_record_result(pending,waiting,false);} catch(...) {refused=true;} CHECK(refused);
    auto completed=waiting; completed["stage"]="complete";completed["manual_bootsel_required"]=false;
    completed["target"]={{"scope","member"},{"device_id",std::string(32,'c')},{"boot_id",std::string(32,'b')}};
    pico_usb_record_result(pending,completed,false);CHECK(pending.at("verified")==true && pending.at("manual_bootsel_required")==false);
    pico_usb_remove_hold(marker);
    PicoUsbInstallerTask task;
    CHECK(task.start([] {std::this_thread::sleep_for(std::chrono::milliseconds(100));return nlohmann::json{{"ok",true}};},{{"operation","fixture"}}));
    CHECK(!task.start([] {return nlohmann::json{};},{}));
    for(int i=0;i<100 && task.status().at("running")==true;++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(task.status().at("result").at("ok")==true);
    std::filesystem::remove_all(root);
    std::cout<<"Shared installer argv/result, private durable holds and task exclusion passed\n";
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n'; return 1;}
}
