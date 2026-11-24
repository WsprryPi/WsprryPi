// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "catalog.hpp"
#include "wtp_settings_json.hpp"
#include <openssl/sha.h>
#ifdef _WIN32
#include "native_windows_file.hpp"
#include "pi_management_journal.hpp"
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
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
nlohmann::json empty_catalog() {
  return {{"version",2},{"generation","1"},{"profiles",nlohmann::json::array()},
      {"management_history",nlohmann::json::object()},{"management_retired",nlohmann::json::object()},
      {"pi_management_history",nullptr},{"remote_management",nlohmann::json::object()}};
}
bool identity(const std::string &id) {
  return id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}
bool catalog_path_present(const std::string &path){
#ifdef _WIN32
  const auto native=windows_file::path_from_utf8(path);
  if(::GetFileAttributesW(windows_file::api_path(native).c_str())!=INVALID_FILE_ATTRIBUTES)return true;
  const auto error=::GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)return false;
#else
  struct stat information{};if(!::lstat(path.c_str(),&information))return true;
  if(errno==ENOENT)return false;
#endif
  throw std::runtime_error("Management authority state is unreadable");
}
void guard_missing_management_catalog(const std::string &path) {
  const std::string suffix=".wtp-devices.json";
  if(!path.ends_with(suffix))return;
  const auto ini=path.substr(0,path.size()-suffix.size());
  for(const auto &state:{ini+".management",ini+".pi-management.json"}){
    if(catalog_path_present(state))throw std::runtime_error("Management catalog is missing beside existing authority state");
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
      {"settings", private_profile?wtp_settings_json(p.settings):management_profile_settings_json(p.settings)}};
  if(private_profile)result["generation"]=p.generation;
  return result;
}
bool generation(const nlohmann::json &v){
  if(!v.is_string())return false;
  const auto &s=v.get_ref<const std::string&>();std::uint64_t n{};
  const auto r=std::from_chars(s.data(),s.data()+s.size(),n);
  return !s.empty()&&s[0]!='0'&&n&&r.ec==std::errc{}&&r.ptr==s.data()+s.size();
}
void validate_remote_management(const std::string &device,const nlohmann::json &row){
    if(!identity(device)||!row.is_object()||row.size()!=6)throw std::runtime_error("Invalid remote management enrollment");
    for(const auto *key:{"parent_id","profile_id","profile_generation","pair_id","authorization_generation","product"})
      if(!row.contains(key)||!row[key].is_string())throw std::runtime_error("Invalid remote management enrollment");
    if(!identity(row["parent_id"].get<std::string>())||!identity(row["profile_id"].get<std::string>())||!identity(row["pair_id"].get<std::string>())||
        !generation(row["profile_generation"])||!generation(row["authorization_generation"])||
        (row["product"]!="WsprryPi"&&row["product"]!="WsprryPico")||
        (row["product"]=="WsprryPi"?(row["parent_id"]!=device):(row["parent_id"]==device)))
      throw std::runtime_error("Invalid remote management binding");

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
#ifndef _WIN32
void write_all(int fd, const std::string &bytes) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto n = ::write(fd, bytes.data() + offset, bytes.size() - offset);
    if(n<0&&errno==EINTR)continue;
    if (n <= 0) throw std::runtime_error("Could not write WTP catalog");
    offset += static_cast<std::size_t>(n);
  }
}
#endif
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
void database_catalog(nlohmann::json &value){
  value["version"]=3;
  for(auto &row:value.at("profiles"))row["settings"]=management_profile_settings_json(parse(row).settings);
}
}
WtpCatalog::WtpCatalog(CatalogStorage storage) : storage_(std::move(storage)) {
  if(!storage_->read || !storage_->publish)throw std::invalid_argument("Complete catalog storage required");
}
std::string WtpCatalog::initial_storage_image() {
  auto value=empty_catalog();database_catalog(value);return catalog_storage_bytes(value);
}
std::string WtpCatalog::database_import_image(const std::string &source){
  auto value=parse_storage_image(source,false);database_catalog(value);return catalog_storage_bytes(value);
}
std::string WtpCatalog::current_storage_image() {
  std::lock_guard lock(mutex_);
  return catalog_storage_bytes(load({},nullptr,false));
}
void WtpCatalog::save(nlohmann::json &value) {
  if(storage_)database_catalog(value);
  const auto bytes=catalog_storage_bytes(value);
  if(storage_){
    storage_->publish(observed_storage_,bytes);
    observed_storage_=bytes;
    return;
  }
#ifdef _WIN32
  const auto before=PiManagementJournal::read_private_record(path_);
  if(bool(before)!=bool(observed_storage_)||(before&&before->bytes!=*observed_storage_))throw std::runtime_error("Catalog owner observation changed");
  (void)PiManagementJournal::publish_private_record(path_,before,bytes);observed_storage_=bytes;
#else
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
#endif
}
nlohmann::json WtpCatalog::load(const std::optional<WtpSettings> &active,std::string *source_bytes,bool initialize_missing) {
  std::string bytes;
  if(storage_){
    if(active)throw std::runtime_error("Database catalog cannot infer an active connection");
    const auto saved=storage_->read();
    if(!saved)throw std::runtime_error("Established catalog is missing");
    if(saved->size()>262144)throw std::runtime_error("Catalog exceeds size limit");
    bytes=*saved;
  }else{
#ifdef _WIN32
   const auto native=PiManagementJournal::read_private_record(path_);
   const bool missing=!native;
#else
   const int fd=::open(path_.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
   const bool missing=fd<0;
   if(missing&&errno!=ENOENT)throw std::runtime_error("WTP catalog is unreadable");
#endif
   if(missing) {
#ifdef _WIN32
    observed_storage_.reset();
#endif
    if(!initialize_missing)throw std::runtime_error("Import catalog is missing");
    guard_missing_management_catalog(path_);
    nlohmann::json fresh=empty_catalog();
    if(active) {
      WtpCatalogProfile legacy{local_id(),"Current WTP device","manual","",*active,active->device_id.empty()};
      fresh["profiles"].push_back(serialize(legacy));
    }
    save(fresh);if(source_bytes)*source_bytes=catalog_storage_bytes(fresh);return fresh;
  }
#ifdef _WIN32
  if(native->bytes.size()>262144)throw std::runtime_error("WTP catalog exceeds size limit");
  bytes=native->bytes;observed_storage_=bytes;
#else
  struct Close {int fd;~Close(){::close(fd);}} close{fd};
  struct stat information{};
  if(::fstat(fd,&information)||!S_ISREG(information.st_mode)||information.st_nlink!=1||
      (information.st_mode&0077)||information.st_uid!=::geteuid()||information.st_size<0||information.st_size>262144)
    throw std::runtime_error("WTP catalog must be a bounded private regular host-owned file");
  char block[4096];
  for(;;){const auto n=::read(fd,block,sizeof(block));if(n<0&&errno==EINTR)continue;
    if(n<0)throw std::runtime_error("WTP catalog is unreadable");
    if(!n)break;
    bytes.append(block,static_cast<std::size_t>(n));if(bytes.size()>262144)throw std::runtime_error("WTP catalog exceeds size limit");}
#endif
  }
  auto value=parse_storage_image(bytes,bool(storage_));
  if(source_bytes)*source_bytes=bytes;
  if(storage_)observed_storage_=bytes;
  return value;
}
nlohmann::json WtpCatalog::parse_storage_image(const std::string &bytes,bool database_format){
  if(bytes.size()>262144)throw std::runtime_error("Catalog exceeds size limit");
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
  if(database_format && value["version"]!=3)throw std::runtime_error("Unsupported database catalog version");
  if(!database_format && value["version"]==1) {
    // One legacy-storage migration, never a second public control view. Reads
    // preserve the old file; the next explicit mutation writes current storage.
    if(value.size()!=2)throw std::runtime_error("Invalid legacy catalog");
    nlohmann::json rows=nlohmann::json::array();std::uint64_t index=0;
    for(const auto &row:value["profiles"]){auto p=parse(row,false);p.generation=std::to_string(++index);rows.push_back(serialize(p));}
    value={{"version",2},{"generation",std::to_string(std::max<std::uint64_t>(1,index))},
        {"profiles",rows},{"management_history",nlohmann::json::object()},{"management_retired",nlohmann::json::object()},{"pi_management_history",nullptr},{"remote_management",nlohmann::json::object()}};
  } else {
    if(value["version"]!=(database_format?3:2)||(value.size()!=7&&value.size()!=8)||
        (value.size()==8&&!value.contains("remote_management"))||!value.contains("generation")||!generation(value["generation"])||
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
  // The previous current format receives an empty private projection only;
  // its exact bytes/checksum remain unchanged until an explicit publication.
  if(!value.contains("remote_management"))value["remote_management"]=nlohmann::json::object();
  const auto &remote=value["remote_management"];
  if(!remote.is_object()||remote.size()>96)throw std::runtime_error("Invalid remote management enrollment");
  for(const auto &[device,row]:remote.items())validate_remote_management(device,row);
  std::set<std::string> retained;
  for(const auto &[device,epoch]:value["management_history"].items()){(void)epoch;retained.insert(device);}
  for(const auto &[device,epoch]:value["management_retired"].items()){(void)epoch;retained.insert(device);}
  for(const auto &[device,row]:remote.items()){(void)row;retained.insert(device);}
  if(retained.size()>96)throw std::runtime_error("Management history capacity");
  std::set<std::string> ids;std::vector<WtpSettings> settings;
  const auto counter=value["generation"].get<std::string>();
  for(const auto &item:value["profiles"]) {
    if(database_format)(void)parse_management_profile_settings(item.at("settings"),true);
    const auto p=parse(item);
    if(p.generation.size()>counter.size()||(p.generation.size()==counter.size()&&p.generation>counter)||
        !ids.insert(p.id).second||std::find(settings.begin(),settings.end(),p.settings)!=settings.end())
      throw std::runtime_error("Invalid profile generation or duplicate catalog identity");
    settings.push_back(p.settings);
  }
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
        request.at("method").get<std::string>(), "", parse_management_profile_settings(request.at("settings"), true)};
    if (p.method == "dns_sd") p.discovery_id = request.at("discovery_id").get<std::string>();
    else if (request.at("discovery_id") != "") throw std::runtime_error("Manual profile cannot bind discovery");
    if (p.settings.transport == "network_plain" && request.at("consent_plain") != true)
      throw std::runtime_error("Consumer Wi-Fi requires an explicit connection choice");
    p.generation=next_generation(value);
    profiles.push_back(serialize(parse(serialize(p))));
  } else if (operation == "edit") {
    if (request.size() != 5) throw std::runtime_error("Invalid edit request");
    bool found = false;
    for (auto &item : profiles) if (item.at("id") == request.at("id")) {
      auto p = parse(item);
      p.name = request.at("name").get<std::string>();
      const auto legacy_uncertainty=p.settings.start_uncertainty_ns;
      const auto legacy_rounding=p.settings.allow_frequency_adjustment;
      p.settings = parse_management_profile_settings(request.at("settings"), true);
      p.settings.start_uncertainty_ns=legacy_uncertainty;
      p.settings.allow_frequency_adjustment=legacy_rounding;
      if (p.settings.transport == "network_plain" && request.at("consent_plain") != true)
        throw std::runtime_error("Consumer Wi-Fi requires an explicit connection choice");
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
      if (management_profile_settings_json(parse(profiles[i]).settings) == management_profile_settings_json(parse(profiles[j]).settings))
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
    std::lock_guard lock(mutex_);PicoManagementHistoryView view;
    if(!storage_&&!catalog_path_present(path_))return view;
    const auto value=load(active);
    for(const auto &[device,epoch]:value["management_history"].items())view.epochs.emplace(device,epoch.get<std::string>());
    for(const auto &[device,epoch]:value["management_retired"].items())view.retired_epochs.emplace(device,epoch.get<std::string>());
    for(const auto &row:value["profiles"]){const auto p=parse(row);if(identity(p.settings.device_id))view.devices.insert(p.settings.device_id);}
    for(const auto &[device,row]:value["remote_management"].items()){(void)row;view.devices.insert(device);view.remote_devices.insert(device);}
    return view;
  };
  authority.publish=[this,active](const std::string &device,const std::string &epoch){
    if(!identity(device)||!identity(epoch))throw std::runtime_error("Invalid management history anchor");
    std::lock_guard lock(mutex_);auto value=load(active);bool enrolled=value["remote_management"].contains(device);
    for(const auto &row:value["profiles"]){const auto p=parse(row);if(p.settings.device_id==device&&!p.legacy_unverified)enrolled=true;}
    if(!enrolled)throw std::runtime_error("Management target is not enrolled");
    auto &epochs=value["management_history"];
    if(epochs.contains(device)){if(epochs[device]!=epoch)throw std::runtime_error("Management history changed");return;}
    if(value["management_retired"].contains(device))throw std::runtime_error("Management history retirement pending");
    std::set<std::string> retained;
    for(const auto *group:{"management_history","management_retired","remote_management"})
      for(const auto &[known,row]:value[group].items()){(void)row;retained.insert(known);}
    retained.insert(device);if(retained.size()>96)throw std::runtime_error("Management history capacity");
    epochs[device]=epoch;save(value);
  };
  authority.retire=[this,active](const std::string &device,const std::string &epoch){
    if(!identity(device)||!identity(epoch))throw std::runtime_error("Invalid management history anchor");
    std::lock_guard lock(mutex_);auto value=load(active);
    for(const auto &row:value["profiles"])if(parse(row).settings.device_id==device)throw std::runtime_error("Management target is still enrolled");
    if(value["remote_management"].contains(device))throw std::runtime_error("Management target is still enrolled");
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
    if(value["remote_management"].contains(device))throw std::runtime_error("Management target is still enrolled");
    if(value["management_history"].contains(device))throw std::runtime_error("Management history is still anchored");
    auto &retired=value["management_retired"];if(!retired.contains(device))return;
    if(retired[device]!=epoch)throw std::runtime_error("Management history changed");
    retired.erase(device);save(value);
  };
  return authority;
}
nlohmann::json WtpCatalog::remote_management(const std::optional<WtpSettings> &active){
  std::lock_guard lock(mutex_);return load(active)["remote_management"];
}
bool WtpCatalog::enroll_remote_management(const RemoteManagementEnrollment &observed,const std::optional<WtpSettings> &active){
  validate_remote_management(observed.device_,observed.metadata_);
  std::lock_guard lock(mutex_);auto value=load(active);
  auto &entries=value["remote_management"];const auto previous=entries.find(observed.device_);
  if(previous!=entries.end()){
    if((*previous)["product"]!=observed.metadata_["product"])throw std::runtime_error("Remote target product changed");
    if(*previous==observed.metadata_)return false;
  }
  std::set<std::string> retained;
  for(const auto *group:{"management_history","management_retired","remote_management"})
    for(const auto &[device,row]:value[group].items()){(void)row;retained.insert(device);}
  retained.insert(observed.device_);if(retained.size()>96)throw std::runtime_error("Management history capacity");
  entries[observed.device_]=observed.metadata_;save(value);return true;
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
    std::lock_guard lock(mutex_);
    if(storage_){
      if(!storage_->read())return std::optional<std::string>{};
      return epoch(load(active));
    }
    if(!catalog_path_present(path_))return std::optional<std::string>{};
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
    if(!storage_){
      if(!catalog_path_present(path_))throw std::runtime_error("Pi management catalog disappeared during commit");
    }
    std::string committed;const auto observed=load(active,&committed);
    if(epoch(observed)!=std::optional<std::string>(history)||committed!=after)
      throw std::runtime_error("Pi management history transaction was not committed");
  };
  return authority;
}
} // namespace wsprrypi
