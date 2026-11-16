// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "catalog.hpp"
#include "wtp_settings_json.hpp"
#include <openssl/sha.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <set>
#include <random>
#include <charconv>
#include <cerrno>
#include <filesystem>
#include <limits>

namespace wsprrypi {
namespace {
bool identity(const std::string &id) {
  return id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}
void guard_missing_management_catalog(const std::string &path) {
  const std::string suffix=".wtp-devices.json";
  if(!path.ends_with(suffix))return;
  const auto ini=path.substr(0,path.size()-suffix.size());
  for(const auto &state:{ini+".management",ini+".pi-management.json"}){
    struct stat information{};
    if(!::lstat(state.c_str(),&information))throw std::runtime_error("Management catalog is missing beside existing authority state");
    if(errno!=ENOENT)throw std::runtime_error("Management authority state is unreadable");
  }
}
std::string local_id() {
  std::random_device source;
  std::string id(32, '0');
  for (auto &c : id) c = "0123456789abcdef"[source() & 15];
  return id;
}
bool safe_name(const std::string &name) {
  return !name.empty() && name.size() <= 80 &&
      std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
nlohmann::json serialize(const WtpCatalogProfile &p,bool private_profile=true) {
  nlohmann::json result={{"id", p.id}, {"name", p.name}, {"method", p.method},
      {"discovery_id", p.discovery_id}, {"legacy_unverified", p.legacy_unverified},
      {"settings", wtp_settings_json(p.settings)}};
  if(private_profile)result["generation"]=p.generation;
  return result;
}
bool generation(const nlohmann::json &v){
  if(!v.is_string())return false;
  const auto &s=v.get_ref<const std::string&>();std::uint64_t n{};
  const auto r=std::from_chars(s.data(),s.data()+s.size(),n);
  return !s.empty()&&s[0]!='0'&&n&&r.ec==std::errc{}&&r.ptr==s.data()+s.size();
}
WtpCatalogProfile parse(const nlohmann::json &value,bool private_profile=true) {
  if (!value.is_object() || value.size() != (private_profile?7:6))
    throw std::runtime_error("Invalid WTP catalog profile");
  WtpCatalogProfile p;
  p.id = value.at("id").get<std::string>();
  p.name = value.at("name").get<std::string>();
  p.method = value.at("method").get<std::string>();
  p.discovery_id = value.at("discovery_id").get<std::string>();
  p.legacy_unverified = value.at("legacy_unverified").get<bool>();
  if(private_profile){if(!value.contains("generation")||!generation(value["generation"]))throw std::runtime_error("Invalid profile generation");p.generation=value["generation"].get<std::string>();}
  const auto allowed_settings = wtp_settings_json(WtpSettings{});
  if (!value.at("settings").is_object())
    throw std::runtime_error("Invalid WTP catalog settings");
  for (auto it = value.at("settings").begin(); it != value.at("settings").end(); ++it)
    if (!allowed_settings.contains(it.key()))
      throw std::runtime_error("Unexpected WTP catalog setting");
  p.settings = parse_wtp_settings(value.at("settings"), true);
  if (!identity(p.id) || !safe_name(p.name) ||
      (p.method != "manual" && p.method != "dns_sd") ||
      (p.method == "dns_sd" && (p.discovery_id.empty() || p.discovery_id.size() > 64)) ||
      (p.method == "manual" && !p.discovery_id.empty()) ||
      (!p.legacy_unverified && !identity(p.settings.device_id)) ||
      (p.legacy_unverified && (p.settings.transport != "network_plain" ||
                               !p.settings.device_id.empty())))
    throw std::runtime_error("Invalid WTP catalog identity or connection method");
  // All TLS material is referenced by absolute host paths, never embedded.
  return p;
}
void write_all(int fd, const std::string &bytes) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto n = ::write(fd, bytes.data() + offset, bytes.size() - offset);
    if(n<0&&errno==EINTR)continue;
    if (n <= 0) throw std::runtime_error("Could not write WTP catalog");
    offset += static_cast<std::size_t>(n);
  }
}
nlohmann::json public_catalog(const nlohmann::json &value){
  nlohmann::json rows=nlohmann::json::array();for(const auto &row:value.at("profiles"))rows.push_back(serialize(parse(row),false));
  return {{"version",1},{"profiles",rows}};
}
std::string next_generation(nlohmann::json &value){
  const auto current=value.at("generation").get<std::string>();std::uint64_t n{};
  const auto r=std::from_chars(current.data(),current.data()+current.size(),n);
  if(r.ec!=std::errc{}||n==std::numeric_limits<std::uint64_t>::max())throw std::runtime_error("Catalog generation exhausted");
  auto next=std::to_string(n+1);value["generation"]=next;return next;
}
} // namespace
std::string wtp_catalog_revision(const nlohmann::json &value) {
  auto document=value;document.erase("checksum");
  const auto bytes = document.dump();
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(), digest);
  std::string result = "\"";
  for (auto c : digest) { result += "0123456789abcdef"[c >> 4]; result += "0123456789abcdef"[c & 15]; }
  return result + "\"";
}
namespace {
std::string catalog_storage_bytes(const nlohmann::json &value) {
  auto sealed=value;sealed.erase("checksum");const auto revision=wtp_catalog_revision(sealed);sealed["checksum"]=revision.substr(1,64);
  const auto bytes=sealed.dump(2)+"\n";if(bytes.size()>262144)throw std::runtime_error("WTP catalog exceeds size limit");
  return bytes;
}
}
void WtpCatalog::save(const nlohmann::json &value) {
  const auto bytes=catalog_storage_bytes(value);
  const auto temp = path_ + ".tmp." + local_id();
  const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (fd < 0) throw std::runtime_error("Could not create WTP catalog");
  bool open = true;
  try {
    if(::fchmod(fd,0600))throw std::runtime_error("Could not protect WTP catalog");
    write_all(fd, bytes);
    if (::fsync(fd) != 0) throw std::runtime_error("Could not sync WTP catalog");
    const int closed = ::close(fd); open = false;
    if (closed != 0) throw std::runtime_error("Could not close WTP catalog");
    if (::rename(temp.c_str(), path_.c_str()) != 0) throw std::runtime_error("Could not publish WTP catalog");
    auto parent=std::filesystem::path(path_).parent_path();if(parent.empty())parent=".";
    parent=std::filesystem::canonical(parent);
    const int directory=::open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(directory<0)throw std::runtime_error("Could not open catalog directory");
    const auto synced=::fsync(directory);::close(directory);
    if(synced)throw std::runtime_error("Could not sync catalog directory");
  } catch (...) {
    if (open) ::close(fd);
    ::unlink(temp.c_str()); throw;
  }
}
nlohmann::json WtpCatalog::load(const std::optional<WtpSettings> &active,std::string *source_bytes) {
  const int fd=::open(path_.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
  if(fd<0) {
    if(errno!=ENOENT)throw std::runtime_error("WTP catalog is unreadable");
    guard_missing_management_catalog(path_);
    nlohmann::json fresh={{"version",2},{"generation","1"},{"profiles",nlohmann::json::array()},
        {"management_history",nlohmann::json::object()},{"management_retired",nlohmann::json::object()},{"pi_management_history",nullptr}};
    if(active) {
      WtpCatalogProfile legacy{local_id(),"Current WTP device","manual","",*active,active->device_id.empty()};
      fresh["profiles"].push_back(serialize(legacy));
    }
    save(fresh);if(source_bytes)*source_bytes=catalog_storage_bytes(fresh);return fresh;
  }
  struct Close {int fd;~Close(){::close(fd);}} close{fd};
  struct stat information{};
  if(::fstat(fd,&information)||!S_ISREG(information.st_mode)||information.st_nlink!=1||
      (information.st_mode&0077)||information.st_uid!=::geteuid()||information.st_size<0||information.st_size>262144)
    throw std::runtime_error("WTP catalog must be a bounded private regular host-owned file");
  std::string bytes;char block[4096];
  for(;;){const auto n=::read(fd,block,sizeof(block));if(n<0&&errno==EINTR)continue;
    if(n<0)throw std::runtime_error("WTP catalog is unreadable");
    if(!n)break;
    bytes.append(block,static_cast<std::size_t>(n));if(bytes.size()>262144)throw std::runtime_error("WTP catalog exceeds size limit");}
  std::vector<std::set<std::string>> objects;
  auto value=nlohmann::json::parse(bytes,[&](int depth,nlohmann::json::parse_event_t event,nlohmann::json &v){
    if(depth>16)throw std::runtime_error("WTP catalog nesting limit");
    if(event==nlohmann::json::parse_event_t::object_start)objects.emplace_back();
    if(event==nlohmann::json::parse_event_t::key&&!objects.back().insert(v.get<std::string>()).second)throw std::runtime_error("Duplicate catalog field");
    if(event==nlohmann::json::parse_event_t::object_end)objects.pop_back();
    return true;
  });
  if(!value.is_object()||!value.contains("version")||!value["version"].is_number_integer()||
      !value.contains("profiles")||!value["profiles"].is_array()||value["profiles"].size()>64)
    throw std::runtime_error("Unsupported WTP catalog version or size");
  if(value["version"]==1) {
    // One legacy-storage migration, never a second public control view. Reads
    // preserve the old file; the next explicit mutation writes current storage.
    if(value.size()!=2)throw std::runtime_error("Invalid legacy catalog");
    nlohmann::json rows=nlohmann::json::array();std::uint64_t index=0;
    for(const auto &row:value["profiles"]){auto p=parse(row,false);p.generation=std::to_string(++index);rows.push_back(serialize(p));}
    value={{"version",2},{"generation",std::to_string(std::max<std::uint64_t>(1,index))},
        {"profiles",rows},{"management_history",nlohmann::json::object()},{"management_retired",nlohmann::json::object()},{"pi_management_history",nullptr}};
  } else {
    if(value["version"]!=2||value.size()!=7||!value.contains("generation")||!generation(value["generation"])||
        !value.contains("management_history")||!value["management_history"].is_object()||value["management_history"].size()>96||
        !value.contains("management_retired")||!value["management_retired"].is_object()||
        value["management_history"].size()+value["management_retired"].size()>96||
        !value.contains("pi_management_history")||
        !value.contains("checksum")||!value["checksum"].is_string())throw std::runtime_error("Invalid current catalog");
    const auto checksum=value["checksum"].get<std::string>();
    if(checksum.size()!=64||checksum.find_first_not_of("0123456789abcdef")!=std::string::npos||
        wtp_catalog_revision(value)!="\""+checksum+"\"")throw std::runtime_error("Catalog checksum mismatch");
    for(const auto &[device,epoch]:value["management_history"].items())
      if(!identity(device)||!epoch.is_string()||!identity(epoch.get<std::string>()))throw std::runtime_error("Invalid management history anchor");
    for(const auto &[device,epoch]:value["management_retired"].items())
      if(!identity(device)||!epoch.is_string()||!identity(epoch.get<std::string>())||value["management_history"].contains(device))throw std::runtime_error("Invalid management history retirement");
    const auto &local=value["pi_management_history"];
    if(!local.is_null()&&(!local.is_object()||local.size()!=2||!local.contains("device_id")||!local["device_id"].is_string()||
        !identity(local["device_id"].get<std::string>())||!local.contains("history_id")||!local["history_id"].is_string()||
        !identity(local["history_id"].get<std::string>())))throw std::runtime_error("Invalid Pi management history anchor");
  }
  std::set<std::string> ids;std::vector<WtpSettings> settings;
  const auto counter=value["generation"].get<std::string>();
  for(const auto &item:value["profiles"]) {
    const auto p=parse(item);
    if(p.generation.size()>counter.size()||(p.generation.size()==counter.size()&&p.generation>counter)||
        !ids.insert(p.id).second||std::find(settings.begin(),settings.end(),p.settings)!=settings.end())
      throw std::runtime_error("Invalid profile generation or duplicate catalog identity");
    settings.push_back(p.settings);
  }
  if(source_bytes)*source_bytes=bytes;
  return value;
}
std::pair<nlohmann::json, std::string> WtpCatalog::snapshot(
    const std::optional<WtpSettings> &active) {
  std::lock_guard lock(mutex_);
  auto value = load(active);
  std::string active_id;
  if (active) for (const auto &item : value.at("profiles")) {
    const auto p = parse(item);
    if (p.settings == *active) { active_id = p.id; break; }
  }
  auto result = public_catalog(value);
  result["scope"] = "wsprrypi-wtp-catalog/1";
  result["active_id"] = active_id;
  result["active_unmatched"] = active.has_value() && active_id.empty();
  return {result, wtp_catalog_revision(value)};
}
std::pair<nlohmann::json, std::string> WtpCatalog::mutate(
    const nlohmann::json &request, const std::string &revision,
    const std::optional<WtpSettings> &active) {
  std::lock_guard lock(mutex_);
  auto value = load(active);
  if (revision != wtp_catalog_revision(value)) throw std::runtime_error("revision_conflict");
  if (!request.is_object() || !request.contains("operation") ||
      !request.at("operation").is_string()) throw std::runtime_error("Invalid catalog request");
  const auto operation = request.at("operation").get<std::string>();
  auto &profiles = value["profiles"];
  if (operation == "add") {
    if (request.size() != 6 || profiles.size() >= 64) throw std::runtime_error("Invalid or full catalog");
    WtpCatalogProfile p{local_id(), request.at("name").get<std::string>(),
        request.at("method").get<std::string>(), "", parse_wtp_settings(request.at("settings"), true)};
    if (p.method == "dns_sd") p.discovery_id = request.at("discovery_id").get<std::string>();
    else if (request.at("discovery_id") != "") throw std::runtime_error("Manual profile cannot bind discovery");
    if (p.settings.transport == "network_plain" && request.at("consent_plain") != true)
      throw std::runtime_error("Plain LAN requires explicit consent");
    p.generation=next_generation(value);
    profiles.push_back(serialize(parse(serialize(p))));
  } else if (operation == "edit") {
    if (request.size() != 5) throw std::runtime_error("Invalid edit request");
    bool found = false;
    for (auto &item : profiles) if (item.at("id") == request.at("id")) {
      auto p = parse(item);
      p.name = request.at("name").get<std::string>();
      p.settings = parse_wtp_settings(request.at("settings"), true);
      if (p.settings.transport == "network_plain" && request.at("consent_plain") != true)
        throw std::runtime_error("Plain LAN requires explicit consent");
      p.legacy_unverified = p.settings.device_id.empty() && p.legacy_unverified;
      p.generation=next_generation(value);
      item = serialize(parse(serialize(p)));
      found = true; break;
    }
    if (!found) throw std::runtime_error("Unknown profile");
  } else if (operation == "rename") {
    if (request.size() != 3) throw std::runtime_error("Invalid rename request");
    bool found = false;
    for (auto &item : profiles) if (item.at("id") == request.at("id")) {
      auto p = parse(item); p.name = request.at("name").get<std::string>();
      p.generation=next_generation(value);
      item = serialize(parse(serialize(p))); found = true; break;
    }
    if (!found) throw std::runtime_error("Unknown profile");
  } else if (operation == "remove") {
    if (request.size() != 2) throw std::runtime_error("Invalid remove request");
    const auto before = profiles.size();
    for (auto it = profiles.begin(); it != profiles.end(); ++it)
      if (it->at("id") == request.at("id")) { profiles.erase(it); break; }
    if (profiles.size() == before) throw std::runtime_error("Unknown profile");
    (void)next_generation(value);
  } else throw std::runtime_error("Unknown catalog operation");
  for (std::size_t i = 0; i < profiles.size(); ++i)
    for (std::size_t j = i + 1; j < profiles.size(); ++j)
      if (parse(profiles[i]).settings == parse(profiles[j]).settings)
        throw std::runtime_error("An identical endpoint is already saved");
  save(value);
  return {public_catalog(value), wtp_catalog_revision(value)};
}
WtpCatalogProfile WtpCatalog::profile(const std::string &id,
    const std::string &revision, const std::optional<WtpSettings> &active) {
  std::lock_guard lock(mutex_);
  auto value = load(active);
  if (revision != wtp_catalog_revision(value)) throw std::runtime_error("revision_conflict");
  for (const auto &item : value.at("profiles")) {
    auto p = parse(item);
    if (p.id == id) return p;
  }
  throw std::runtime_error("Unknown profile");
}
PicoManagementHistoryAuthority WtpCatalog::management_history_authority(
    const std::optional<WtpSettings> &active) {
  PicoManagementHistoryAuthority authority;
  authority.read=[this,active]{
    std::lock_guard lock(mutex_);PicoManagementHistoryView view;struct stat information{};
    if(::lstat(path_.c_str(),&information)){
      if(errno==ENOENT)return view;
      throw std::runtime_error("Management catalog is unreadable");
    }
    const auto value=load(active);
    for(const auto &[device,epoch]:value["management_history"].items())view.epochs.emplace(device,epoch.get<std::string>());
    for(const auto &[device,epoch]:value["management_retired"].items())view.retired_epochs.emplace(device,epoch.get<std::string>());
    for(const auto &row:value["profiles"]){const auto p=parse(row);if(identity(p.settings.device_id))view.devices.insert(p.settings.device_id);}
    return view;
  };
  authority.publish=[this,active](const std::string &device,const std::string &epoch){
    if(!identity(device)||!identity(epoch))throw std::runtime_error("Invalid management history anchor");
    std::lock_guard lock(mutex_);auto value=load(active);bool enrolled=false;
    for(const auto &row:value["profiles"]){const auto p=parse(row);if(p.settings.device_id==device&&!p.legacy_unverified)enrolled=true;}
    if(!enrolled)throw std::runtime_error("Management target is not enrolled");
    auto &epochs=value["management_history"];
    if(epochs.contains(device)){if(epochs[device]!=epoch)throw std::runtime_error("Management history changed");return;}
    if(value["management_retired"].contains(device))throw std::runtime_error("Management history retirement pending");
    if(epochs.size()+value["management_retired"].size()>=96)throw std::runtime_error("Management history capacity");
    epochs[device]=epoch;save(value);
  };
  authority.retire=[this,active](const std::string &device,const std::string &epoch){
    if(!identity(device)||!identity(epoch))throw std::runtime_error("Invalid management history anchor");
    std::lock_guard lock(mutex_);auto value=load(active);
    for(const auto &row:value["profiles"])if(parse(row).settings.device_id==device)throw std::runtime_error("Management target is still enrolled");
    auto &epochs=value["management_history"];
    if(!epochs.contains(device)){
      if(value["management_retired"].contains(device)&&value["management_retired"][device]==epoch)return;
      throw std::runtime_error("Management history anchor is missing");
    }
    if(epochs[device]!=epoch)throw std::runtime_error("Management history changed");
    epochs.erase(device);value["management_retired"][device]=epoch;save(value);
  };
  authority.finish_retirement=[this,active](const std::string &device,const std::string &epoch){
    if(!identity(device)||!identity(epoch))throw std::runtime_error("Invalid management history retirement");
    std::lock_guard lock(mutex_);auto value=load(active);
    for(const auto &row:value["profiles"])if(parse(row).settings.device_id==device)throw std::runtime_error("Management target is still enrolled");
    if(value["management_history"].contains(device))throw std::runtime_error("Management history is still anchored");
    auto &retired=value["management_retired"];if(!retired.contains(device))return;
    if(retired[device]!=epoch)throw std::runtime_error("Management history changed");
    retired.erase(device);save(value);
  };
  return authority;
}
PiManagementAnchor WtpCatalog::pi_management_anchor(const std::string &self,
    const std::optional<WtpSettings> &active) {
  if(!identity(self))throw std::invalid_argument("Pi management identity required");
  const auto epoch=[self](const nlohmann::json &value)->std::optional<std::string>{
    const auto &anchor=value["pi_management_history"];
    if(anchor.is_null())return {};
    if(anchor["device_id"]!=self)throw std::runtime_error("Pi management history identity changed");
    return anchor["history_id"].get<std::string>();
  };
  PiManagementAnchor authority;
  authority.read=[this,active,epoch]{
    std::lock_guard lock(mutex_);struct stat information{};
    if(::lstat(path_.c_str(),&information)){
      if(errno==ENOENT)return std::optional<std::string>{};
      throw std::runtime_error("Pi management catalog is unreadable");
    }
    return epoch(load(active));
  };
  authority.coordinate=[this,active,self,epoch](const std::string &history,const PiManagementAnchor::Commit &commit){
    if(!identity(history)||!commit)throw std::invalid_argument("Pi management history transaction required");
    std::lock_guard lock(mutex_);std::string before;auto value=load(active,&before);const auto existing=epoch(value);
    if(existing&&*existing!=history)throw std::runtime_error("Pi management history changed");
    bool changed=false;
    if(!existing){value["pi_management_history"]={{"device_id",self},{"history_id",history}};changed=true;}
    // The pre-parser authority boundary can precede validation of an existing
    // local WTP selection. Its owner supplies active only for that first
    // validated initialization, never for later reads or ordinary saves.
    if(active&&value["profiles"].empty()){
      WtpCatalogProfile legacy{local_id(),"Current WTP device","manual","",*active,active->device_id.empty()};
      legacy.generation=next_generation(value);value["profiles"].push_back(serialize(legacy));changed=true;
    }
    const std::string after=changed?catalog_storage_bytes(value):before;
    commit(before,after);
    struct stat published{};
    if(::lstat(path_.c_str(),&published))throw std::runtime_error("Pi management catalog disappeared during commit");
    std::string committed;const auto observed=load(active,&committed);
    if(epoch(observed)!=std::optional<std::string>(history)||committed!=after)
      throw std::runtime_error("Pi management history transaction was not committed");
  };
  return authority;
}
} // namespace wsprrypi
