// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include <chrono>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <future>
#include <functional>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace wsprrypi {
inline bool pico_usb_reconnect_allowed(const nlohmann::json& result,bool preparing) {
  return result.value("ok",false) ? result.value("stage","")=="complete" :
      preparing && !result.value("recovery_required",true);
}
inline void pico_usb_record_result(nlohmann::json& expected,const nlohmann::json& result,bool preparing) {
  if(result.contains("transaction")) expected["transaction"]=result.at("transaction");
  if(result.contains("manual_bootsel_required")) expected["manual_bootsel_required"]=result.at("manual_bootsel_required");
  if(!result.value("ok",false)) return;
  const bool waiting=preparing && result.value("stage","")=="awaiting_bootsel" &&
      result.value("manual_bootsel_required",false) && result.value("recovery_required",false) && result.contains("transaction");
  if((result.value("stage","")!="complete" && !waiting) || result.value("serial","")!=expected.at("serial").get<std::string>() ||
      result.value("source","")!=expected.at("source").get<std::string>() || result.value("version","")!=expected.at("version").get<std::string>())
    throw std::runtime_error("Shared installer did not verify the reviewed Pico image.");
  if(waiting) return;
  const auto& target=result.at("target");
  const auto boot=target.at("boot_id").get<std::string>();
  if(target.value("scope","")!="member" || target.at("device_id")!=expected.at("device_id") ||
      boot.size()!=32 || boot.find_first_not_of("0123456789abcdef")!=std::string::npos)
    throw std::runtime_error("Shared installer target binding is invalid.");
  expected["verified"]=true;
  expected["verified_target"]=target;
}
inline nlohmann::json pico_usb_public_task(const nlohmann::json& state) {
  nlohmann::json result=nlohmann::json::object();
  for(const auto* key:{"running","operation","plan_id","branch","bound_serial","bound_device","bound_selection"})
    if(state.contains(key)) result[key]=state.at(key);
  result["result"]=nullptr;
  if(state.contains("result") && state.at("result").is_object()) {
    const auto& value=state.at("result"); auto& projected=result["result"];
    projected=nlohmann::json::object();
    for(const auto* key:{"schema","installer_version","ok","stage","serial","version","branch","source","workflow","run","uf2","preserved","recovery_required","recent_ci","manual_bootsel_required"})
      if(value.contains(key)) projected[key]=value.at(key);
    if(value.contains("error")) {
      const auto text=value.value("error",std::string{});
      projected["error"]=(text.size()<=512 && text.find('/')==std::string::npos && text.find('\\')==std::string::npos && text.find('\n')==std::string::npos)
          ?text:"Firmware work is unconfirmed. Inspect the private host transaction and recover this selected Pico.";
    }
  }
  return result;
}
// Adapter only: device/release/image/backup/programming authority stays in the
// identified WsprryPico installer. No command shell or browser-supplied paths.
inline nlohmann::json pico_usb_installer_process(const std::vector<std::string>& arguments) {
  int descriptors[2];
  if (pipe(descriptors)) throw std::runtime_error("installer_pipe_failed");
  std::vector<char*> argv;
  for (const auto& argument:arguments) argv.push_back(const_cast<char*>(argument.c_str()));
  argv.push_back(nullptr);
  const auto child=fork();
  if (child<0) { close(descriptors[0]); close(descriptors[1]); throw std::runtime_error("installer_spawn_failed"); }
  if (!child) {
    (void)setpgid(0,0);
    dup2(descriptors[1],STDOUT_FILENO);
    const int null=open("/dev/null",O_RDWR);
    if (null>=0) { dup2(null,STDIN_FILENO); dup2(null,STDERR_FILENO); close(null); }
    close(descriptors[0]); close(descriptors[1]);
    execv(argv[0],argv.data());
    _exit(127);
  }
  (void)setpgid(child,child);
  close(descriptors[1]);
  fcntl(descriptors[0],F_SETFL,O_NONBLOCK);
  std::string output;
  int status=0;
  bool finished=false;
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(360);
  try {
    while (std::chrono::steady_clock::now()<deadline) {
      pollfd descriptor{descriptors[0],POLLIN,0};
      (void)poll(&descriptor,1,100);
      char bytes[4096];
      for (;;) {
        const auto count=read(descriptors[0],bytes,sizeof(bytes));
        if (count<=0) break;
        output.append(bytes,static_cast<std::size_t>(count));
        if (output.size()>32768) throw std::runtime_error("installer_output_limit");
      }
      const auto observed=waitpid(child,&status,WNOHANG);
      if (observed==child) {
        finished=true;
        for (;;) {
          const auto count=read(descriptors[0],bytes,sizeof(bytes));
          if (count<=0) break;
          output.append(bytes,static_cast<std::size_t>(count));
          if (output.size()>32768) throw std::runtime_error("installer_output_limit");
        }
        break;
      }
      if (observed<0) throw std::runtime_error("installer_wait_failed");
    }
    if (!finished) throw std::runtime_error("installer_deadline_recovery_required");
    close(descriptors[0]); descriptors[0]=-1;
    auto value=nlohmann::json::parse(output);
    if (!value.is_object() || value.value("schema","")!="wsprrypico-usb-result/1" ||
        value.value("installer_version","")!="0.1.0" || !value.contains("ok") || !value["ok"].is_boolean() ||
        !WIFEXITED(status) || (value["ok"].get<bool>() && WEXITSTATUS(status)!=0) ||
        (!value["ok"].get<bool>() && WEXITSTATUS(status)==0))
      throw std::runtime_error("invalid_shared_installer_result");
    return value;
  } catch (...) {
    if (descriptors[0]>=0) close(descriptors[0]);
    // The entry can die before a USB-writing descendant. Its process group
    // remains ours even after waitpid reaps the leader.
    kill(-child,SIGKILL);
    if (!finished) { while(waitpid(child,&status,0)<0 && errno==EINTR) {} }
    throw;
  }
}

class PicoUsbInstallerTask {
public:
  nlohmann::json status() const { std::lock_guard lock(mutex_); return status_; }
  bool start(std::function<nlohmann::json()> operation, const nlohmann::json& initial) {
    std::lock_guard lock(mutex_);
    if (running_) return false;
    if (worker_.valid()) worker_.get();
    running_=true; status_=initial; status_["running"]=true;
    worker_=std::async(std::launch::async,[this,operation=std::move(operation)] {
      nlohmann::json result;
      try { result=operation(); }
      catch (const std::exception& error) { result={{"ok",false},{"error",error.what()},{"recovery_required",true}}; }
      std::lock_guard lock(mutex_);
      status_["result"]=std::move(result); status_["running"]=false; running_=false;
    });
    return true;
  }
private:
  mutable std::mutex mutex_;
  bool running_{};
  nlohmann::json status_={{"running",false},{"result",nullptr}};
  std::future<void> worker_;
};
} // namespace wsprrypi
