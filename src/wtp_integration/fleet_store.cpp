// SPDX-License-Identifier: MIT
#include "fleet_store.hpp"
#include "fleet_schedule.hpp"
#include "catalog.hpp"
#include "wtp_settings_json.hpp"
#include <array>
#include <charconv>
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace wsprrypi {
std::uint64_t wtp_fleet_decimal(const nlohmann::json& value) {
    const auto text = value.get<std::string>();
    std::uint64_t result{};
    const auto parsed = std::from_chars(text.data(), text.data()+text.size(), result);
    if (text.empty() || (text.size() > 1 && text.front() == '0') || parsed.ec != std::errc{} ||
        parsed.ptr != text.data()+text.size()) throw std::runtime_error("Invalid fleet decimal integer");
    return result;
}
WtpFleetStore::WtpFleetStore(std::string path, std::string self_id)
    : path_(std::move(path)), self_id_(std::move(self_id)) {}
void WtpFleetStore::validate(const nlohmann::json& data, const std::string& self_id) {
    if (!data.is_object() || data.size() != 3 || data.at("version") != 1 ||
        !data.at("assignments").is_array() || data.at("assignments").size() > 8 ||
        !data.at("removals").is_array() || data.at("removals").size() > 32)
        throw std::runtime_error("Invalid fleet assignment document");
    std::set<std::string> identities;
    for (const auto& row : data.at("assignments")) {
        if (!row.is_object() || row.size() != 10) throw std::runtime_error("Invalid output assignment fields");
        const auto settings = parse_wtp_settings(row.at("settings"), true);
        const auto id = row.at("device_id").get<std::string>();
        const auto name = row.at("name").get<std::string>();
        if (id.size() != 32 || id != settings.device_id || id == self_id ||
            !identities.insert(id).second || name.empty() || name.size() > 80 ||
            std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
            throw std::runtime_error("Duplicate, self, or invalid fleet identity/name");
        const auto product = row.at("product").get<std::string>();
        if (product != "WsprryPi" && product != "WsprryPico")
            throw std::runtime_error("Unsupported fleet target product");
        if (!row.at("management_port").is_number_integer())
            throw std::runtime_error("Management port must be an integer");
        const auto port = row.at("management_port").get<std::int64_t>();
        if (port <= 0 || port > 65535 || !row.at("enabled").is_boolean() ||
            !row.at("in_flight").is_boolean()) throw std::runtime_error("Invalid fleet enable or management port");
        if (product == "WsprryPi") {
            if (settings.transport != "network_plain")
                throw std::runtime_error("Pi fleet revocation currently requires explicit Plain LAN");
            (void)wtp_fleet_decimal(row.at("revocation_generation"));
        } else if (!row.at("revocation_generation").is_null())
            throw std::runtime_error("Unexpected Pico revocation generation");
        (void)wtp_fleet_decimal(row.at("last_start_ns"));
        validate_wtp_fleet_schedule(row.at("schedule"));
    }
    for (const auto& notice : data.at("removals")) {
        if (!notice.is_object() || notice.size() != 2 ||
            notice.at("device_id").get<std::string>().size() != 32)
            throw std::runtime_error("Invalid fleet removal record");
        (void)wtp_fleet_decimal(notice.at("revocation_generation"));
    }
}
void WtpFleetStore::load() {
    if (loaded_) return;
    const int fd = open(path_.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        if (errno != ENOENT) throw std::runtime_error("Cannot read fleet assignments");
        data_ = {{"version",1},{"assignments",nlohmann::json::array()},{"removals",nlohmann::json::array()}};
    } else {
        struct Guard { int fd; ~Guard() { close(fd); } } guard{fd};
        struct stat info{};
        if (fstat(fd,&info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != geteuid() ||
            (info.st_mode & 0077) != 0 || info.st_size > 131072)
            throw std::runtime_error("Fleet assignments must be a private host-owned regular file under 128 KiB");
        std::string bytes;
        std::array<char,4096> buffer{};
        for (;;) {
            const auto n = read(fd,buffer.data(),buffer.size());
            if (n == 0) break;
            if (n < 0) { if (errno == EINTR) continue; throw std::runtime_error("Fleet assignment read failed"); }
            bytes.append(buffer.data(),static_cast<std::size_t>(n));
            if (bytes.size() > 131072) throw std::runtime_error("Fleet assignments exceed size limit");
        }
        std::vector<std::set<std::string>> keys;
        data_ = nlohmann::json::parse(bytes,[&](int, nlohmann::json::parse_event_t e,nlohmann::json& v) {
            if (e == nlohmann::json::parse_event_t::object_start) keys.emplace_back();
            if (e == nlohmann::json::parse_event_t::key && !keys.back().insert(v.get<std::string>()).second)
                throw std::runtime_error("Duplicate fleet assignment key");
            if (e == nlohmann::json::parse_event_t::object_end) keys.pop_back();
            return true;
        });
    }
    validate(data_, self_id_);
    loaded_ = true;
}
void WtpFleetStore::save(const nlohmann::json& data) {
    std::string pattern = path_ + ".tmp.XXXXXX";
    std::vector<char> filename(pattern.begin(),pattern.end()); filename.push_back(0);
    int fd = mkstemp(filename.data());
    if (fd < 0) throw std::runtime_error("Cannot create fleet assignment file");
    try {
        const auto bytes = data.dump(2) + "\n";
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto n = write(fd,bytes.data()+offset,bytes.size()-offset);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) throw std::runtime_error("Cannot write fleet assignments");
            offset += static_cast<std::size_t>(n);
        }
        if (fsync(fd) != 0) throw std::runtime_error("Cannot sync fleet assignments");
        const int closed = close(fd); fd = -1;
        if (closed != 0 || rename(filename.data(),path_.c_str()) != 0)
            throw std::runtime_error("Cannot publish fleet assignments");
        auto directory = std::filesystem::path(path_).parent_path();
        if (directory.empty()) directory = ".";
        const int dir = open(directory.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
        if (dir < 0) throw std::runtime_error("Cannot open fleet state directory");
        const int synced = fsync(dir); close(dir);
        if (synced != 0) throw std::runtime_error("Cannot sync fleet state directory");
    } catch (...) {
        if (fd >= 0) close(fd);
        unlink(filename.data());
        // Rename may already have succeeded. Reload authoritative disk contents
        // on the next request; the failed caller must never dispatch work.
        loaded_ = false;
        throw;
    }
}
std::pair<nlohmann::json,std::string> WtpFleetStore::snapshot() {
    std::lock_guard lock(mutex_); load(); return {data_,wtp_catalog_revision(data_)};
}
std::pair<nlohmann::json,std::string> WtpFleetStore::update(const std::string& revision,
    const std::function<void(nlohmann::json&)>& mutation) {
    std::lock_guard lock(mutex_); load();
    if (!revision.empty() && revision != wtp_catalog_revision(data_)) throw std::runtime_error("revision_conflict");
    auto candidate = data_; mutation(candidate); validate(candidate,self_id_);
    if(candidate != data_) {save(candidate); data_=std::move(candidate);}
    return {data_,wtp_catalog_revision(data_)};
}
void WtpFleetStore::revoke(const std::string& id,const std::string& generation) {
    update({},[&](nlohmann::json& data) {
        auto& rows=data["assignments"];
        auto row=std::find_if(rows.begin(),rows.end(),[&](const auto& r){return r.at("device_id")==id;});
        if (row==rows.end()) return;
        rows.erase(row);
        auto& notices=data["removals"];
        if (notices.size()==32) notices.erase(notices.begin());
        notices.push_back({{"device_id",id},{"revocation_generation",generation}});
    });
}
}
