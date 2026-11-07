// SPDX-License-Identifier: MIT
#include "pico_usb_installer.hpp"
#include "pico_usb_installer_state.hpp"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <thread>
#include <future>
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
    const auto locks=root/"locks";std::filesystem::create_directory(locks);
    const auto lock_path=locks/"1545B6EFBF78028C.lock";
    {std::ofstream file(lock_path);file<<"existing-owner";}
    struct stat original_lock{};CHECK(stat(lock_path.c_str(),&original_lock)==0);
    auto lease=pico_usb_runtime_lease("1545B6EFBF78028C",locks);CHECK(lease);
    struct stat opened_lock{};CHECK(stat(lock_path.c_str(),&opened_lock)==0);
    CHECK(original_lock.st_ino==opened_lock.st_ino && opened_lock.st_size==14);
    const int writer=open(lock_path.c_str(),O_RDWR|O_NOFOLLOW);CHECK(writer>=0);
    CHECK(flock(writer,LOCK_EX|LOCK_NB)!=0);lease.reset();
    CHECK(flock(writer,LOCK_EX|LOCK_NB)==0);
    bool busy=false;try{(void)pico_usb_runtime_lease("1545B6EFBF78028C",locks);}catch(...){busy=true;}
    CHECK(busy);close(writer);
    std::filesystem::remove(lock_path);std::filesystem::create_symlink(marker,lock_path);
    bool unsafe=false;try{(void)pico_usb_runtime_lease("1545B6EFBF78028C",locks);}catch(...){unsafe=true;}
    CHECK(unsafe);std::filesystem::remove(lock_path);
    CHECK(pico_usb_runtime_lease("1545B6EFBF78028C",locks));
    const auto controls=root/"control"; std::filesystem::create_directory(controls);
    CHECK(!pico_usb_any_update_blocked(controls));
    pico_usb_save_hold(controls/"pending.json",record,true);CHECK(pico_usb_any_update_blocked(controls));
    pico_usb_remove_hold(controls/"pending.json");CHECK(!pico_usb_any_update_blocked(controls));
    { std::ofstream file(root/"not-a-directory"); file<<"invalid"; }
    CHECK(pico_usb_any_update_blocked(root/"not-a-directory"));
    CHECK(!pico_usb_http_mutation("GET","/api/control/v1/status"));
    CHECK(!pico_usb_http_mutation("POST","/api/control/v1/member/firmware"));
    for(const auto* path:{"/config","/config/repair","/api/control/v1/jobs","/api/control/v1/host/devices/use","/api/control/v1/member/config"})
      CHECK(pico_usb_http_mutation("POST",path));
    CHECK(pico_usb_http_mutation("PUT","/api/control/v1/member/firmware"));
    CHECK(pico_usb_http_mutation("POST","/api/v1/member/firmware"));
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
          "import subprocess,sys; p=subprocess.Popen(['/usr/bin/python3','-c','import signal; signal.pause()'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL); open(sys.argv[1],'w').write(str(p.pid)); print('{}')",
          orphan.string()});
    } catch(...) {refused=true;}
    CHECK(refused);
    std::ifstream child_record(orphan); int descendant=0; child_record>>descendant; CHECK(descendant>0);
    bool gone=false;
    const auto cleanup_started = std::chrono::steady_clock::now();
    const auto cleanup_guard = cleanup_started + std::chrono::seconds(1);
    std::string last_state = "not observed";
    while (std::chrono::steady_clock::now() < cleanup_guard) {
      std::ifstream state("/proc/"+std::to_string(descendant)+"/stat"); std::string line; std::getline(state,line);
      const auto end=line.rfind(')');
      last_state = !state ? "absent" : end!=std::string::npos ? line.substr(end+2,1) : "malformed";
      if(!state || (end!=std::string::npos && line.substr(end+2,1)=="Z")) {gone=true;break;}
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!gone) throw std::runtime_error("stage=descendant-cleanup; expected=absent or zombie; last="+last_state+
      "; pid="+std::to_string(descendant)+"; hang_guard=1s; elapsed_ms="+
      std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-cleanup_started).count()));
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
    const nlohmann::json erase_reviewed={{"operation","erase"},{"serial",reviewed.at("serial")}};
    const nlohmann::json erase_waiting={{"ok",true},{"operation","erase"},{"serial",reviewed.at("serial")},
        {"stage","awaiting_bootsel"},{"manual_bootsel_required",true},{"recovery_required",true},{"transaction","/private/erase/transaction.json"}};
    auto erase_pending=erase_reviewed; pico_usb_record_result(erase_pending,erase_waiting,true);
    CHECK(!erase_pending.value("erase_verified",true));
    const nlohmann::json erased={{"ok",true},{"operation","erase"},{"serial",reviewed.at("serial")},
        {"stage","complete"},{"erase_verified",true},{"erased_bytes",0x400000},{"recovery_required",false}};
    pico_usb_record_result(erase_pending,erased,false);CHECK(erase_pending.at("erase_verified")==true);
    pico_usb_record_result(erase_pending,{{"ok",false},{"recovery_required",true}},false);
    CHECK(!erase_pending.value("erase_verified",true)); // A failed retry cannot reuse old success.
    for(const auto* field:{"operation","serial","erase_verified","erased_bytes","recovery_required"}) {
      auto invalid=erased;invalid.erase(field);auto held=erase_reviewed;refused=false;
      try {pico_usb_record_result(held,invalid,false);} catch(...) {refused=true;} CHECK(refused);
    }
    for(const auto& patch:{nlohmann::json{{"erased_bytes",0x3ff000}},nlohmann::json{{"serial","0000000000000000"}},nlohmann::json{{"recovery_required",true}}}) {
      auto invalid=erased;invalid.update(patch);auto held=erase_reviewed;refused=false;
      try {pico_usb_record_result(held,invalid,false);} catch(...) {refused=true;} CHECK(refused);
    }
    const auto erase_public=pico_usb_public_task({{"result",erased}}).at("result");
    CHECK(erase_public.at("erase_verified")==true && erase_public.at("erased_bytes")==0x400000);
    const auto erase_running=pico_usb_public_task({{"operation","recover"},{"running",true},
        {"recovery_operation","erase"},{"result",nullptr}});
    CHECK(erase_running.at("recovery_operation")=="erase" && erase_running.at("result").is_null());
    // Entered/release acknowledgments establish exclusion, independent of host speed.
    std::promise<void> entered, release;
    auto started = entered.get_future();
    auto released = release.get_future().share();
    PicoUsbInstallerTask task;
    CHECK(task.start([&] {
      entered.set_value();
      if (released.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
        throw std::runtime_error("stage=fixture-release; expected=release acknowledgment; hang_guard=5s");
      return nlohmann::json{{"ok",true}};
    },{{"operation","fixture"}}));
    CHECK(started.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK(task.status().at("running") == true);
    CHECK(!task.start([] {return nlohmann::json{};},{}));
    release.set_value();
    const auto completion_guard = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (task.status().at("running") == true) {
      if (std::chrono::steady_clock::now() >= completion_guard)
        throw std::runtime_error("stage=task-completion; expected=not running; last=" + task.status().dump() + "; hang_guard=5s");
      std::this_thread::yield();
    }
    CHECK(task.status().at("result").at("ok")==true);
    std::filesystem::remove_all(root);
    std::cout<<"Shared installer argv/result, private durable holds and task exclusion passed\n";
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n'; return 1;}
}
