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
    const std::string code="import json,sys; print(json.dumps(dict(schema='wsprrypico-usb-result/1',installer_version='0.1.0',ok=True,argument=sys.argv[1])))";
    const auto argument="path with spaces; $(literal)";
    const auto result=pico_usb_installer_process({"/usr/bin/python3","-c",code,argument});
    CHECK(result.at("argument")==argument && result.at("ok")==true);
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
    PicoUsbInstallerTask task;
    CHECK(task.start([] {std::this_thread::sleep_for(std::chrono::milliseconds(100));return nlohmann::json{{"ok",true}};},{{"operation","fixture"}}));
    CHECK(!task.start([] {return nlohmann::json{};},{}));
    for(int i=0;i<100 && task.status().at("running")==true;++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(task.status().at("result").at("ok")==true);
    std::filesystem::remove_all(root);
    std::cout<<"Shared installer argv/result, private durable holds and task exclusion passed\n";
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n'; return 1;}
}
