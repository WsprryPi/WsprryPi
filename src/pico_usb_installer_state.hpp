// SPDX-License-Identifier: MIT
#pragma once
#include <cerrno>
#include <cstdio>
#include "json.hpp"
#include <filesystem>
#include <fcntl.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace wsprrypi {
inline bool pico_usb_entry_protected(const std::filesystem::path& path) {
  auto current=path;
  bool first=true;
  for(;;) {
    struct stat info{};
    if(lstat(current.c_str(),&info) || info.st_uid!=0 || (info.st_mode&0022) ||
        S_ISLNK(info.st_mode) || (first?!S_ISREG(info.st_mode):!S_ISDIR(info.st_mode))) return false;
    if(current==current.root_path()) break;
    current=current.parent_path(); first=false;
  }
  return true;
}
inline void pico_usb_sync_directory(const std::filesystem::path& path) {
  const int fd=open(path.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
  if (fd<0) throw std::runtime_error("pico_maintenance_directory_unavailable");
  const int outcome=fsync(fd); close(fd);
  if (outcome) throw std::runtime_error("pico_maintenance_directory_sync_failed");
}
inline void pico_usb_save_hold(const std::filesystem::path& path,const nlohmann::json& record,bool create=false) {
  const auto temporary=path.string()+".pending";
  const int fd=open(temporary.c_str(),O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);
  if (fd<0) throw std::runtime_error("pico_maintenance_record_unavailable");
  const auto bytes=record.dump();
  const auto count=write(fd,bytes.data(),bytes.size());
  const bool saved=count==static_cast<ssize_t>(bytes.size()) && fsync(fd)==0;
  close(fd);
  if (!saved) { unlink(temporary.c_str()); throw std::runtime_error("pico_maintenance_record_sync_failed"); }
  // link provides no-replace publication for the first admission. A stale
  // .pending file fails closed and is never mistaken for a completed update.
  const int outcome=create?link(temporary.c_str(),path.c_str()):rename(temporary.c_str(),path.c_str());
  if (create || outcome) unlink(temporary.c_str());
  if (outcome) throw std::runtime_error("pico_maintenance_record_publish_failed");
  pico_usb_sync_directory(path.parent_path());
}
inline nlohmann::json pico_usb_read_hold(const std::filesystem::path& path) {
  const int fd=open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
  if(fd<0) throw std::runtime_error("pico_maintenance_record_missing");
  struct stat state{};
  if(fstat(fd,&state) || !S_ISREG(state.st_mode) || (state.st_mode&0077) || state.st_size>32768) {
    close(fd); throw std::runtime_error("unsafe_pico_maintenance_record");
  }
  std::string bytes(static_cast<std::size_t>(state.st_size),'\0');
  const auto count=read(fd,bytes.data(),bytes.size()); close(fd);
  if(count!=static_cast<ssize_t>(bytes.size())) throw std::runtime_error("incomplete_pico_maintenance_record");
  return nlohmann::json::parse(bytes);
}
inline void pico_usb_remove_hold(const std::filesystem::path& path) {
  if(unlink(path.c_str())) throw std::runtime_error("pico_maintenance_record_remove_failed");
  pico_usb_sync_directory(path.parent_path());
}
inline bool pico_usb_serial(const std::string& serial) {
  return serial.size()==16 && serial.find_first_not_of("0123456789ABCDEF")==std::string::npos;
}
inline std::filesystem::path pico_usb_hold_path(const std::string& serial) {
  if (!pico_usb_serial(serial)) throw std::runtime_error("unsupported_pico_usb_serial");
  return std::filesystem::path("/var/lib/wsprrypi/pico-usb-control")/(serial+".json");
}
inline bool pico_usb_update_blocked(const std::string& serial) {
  if (!pico_usb_serial(serial)) return false;
  struct stat observed{};
  if (lstat(pico_usb_hold_path(serial).c_str(),&observed)==0) return true;
  return errno!=ENOENT;
}
inline std::shared_ptr<void> pico_usb_runtime_lease(const std::string& serial) {
  if (!pico_usb_serial(serial)) return {};
  const std::filesystem::path root("/run/lock/wsprrypico");
  if (!std::filesystem::exists(root)) return {}; // Optional updater is not installed.
  const int fd=open((root/(serial+".lock")).c_str(),O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0660);
  if (fd<0) throw std::runtime_error("pico_updater_lock_permissions");
  struct stat info{};
  if(fstat(fd,&info) || !S_ISREG(info.st_mode)) {close(fd);throw std::runtime_error("unsafe_pico_updater_lock");}
  if(info.st_uid==getuid() && fchmod(fd,0660)) {close(fd);throw std::runtime_error("pico_updater_lock_permissions");}
  if (flock(fd,LOCK_SH|LOCK_NB)) { close(fd); throw std::runtime_error("pico_updater_owns_device"); }
  return std::shared_ptr<void>(new int(fd),[](void* value){ const auto p=static_cast<int*>(value); close(*p); delete p; });
}
} // namespace wsprrypi
