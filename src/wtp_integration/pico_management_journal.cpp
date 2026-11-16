// SPDX-License-Identifier: MIT
#include "pico_management_journal.hpp"
#include "browser_api.hpp"
#include "control_contract.hpp"
#include "controller_contract.hpp"
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <fcntl.h>
#include <filesystem>
#include <set>
#include <map>
#include <tuple>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
constexpr std::size_t maximum_bytes=16*1024*1024;
struct FileStamp {
    std::uint64_t device{},inode{},size{},mode{},uid{},links{};
    std::int64_t modified{},modified_ns{},changed{},changed_ns{};
    bool operator==(const FileStamp &) const=default;
};
FileStamp stamp(const std::string &path) {
    struct stat s{};if(lstat(path.c_str(),&s))throw std::runtime_error("management_recovery_file_changed");
    FileStamp value{static_cast<std::uint64_t>(s.st_dev),static_cast<std::uint64_t>(s.st_ino),static_cast<std::uint64_t>(s.st_size),s.st_mode,s.st_uid,static_cast<std::uint64_t>(s.st_nlink),s.st_mtime,0,s.st_ctime,0};
#ifdef __APPLE__
    value.modified_ns=s.st_mtimespec.tv_nsec;value.changed_ns=s.st_ctimespec.tv_nsec;
#else
    value.modified_ns=s.st_mtim.tv_nsec;value.changed_ns=s.st_ctim.tv_nsec;
#endif
    return value;
}
struct Authority {
    struct File {FileStamp stamp;unsigned unresolved;std::string epoch;bool nonempty;};
    std::string controller;
    std::map<std::string,File> files;
    bool loaded{};
};
std::recursive_mutex recovery_mutex;
std::map<std::string,Authority> authorities;
std::string authority_key(const std::string &directory){return std::filesystem::weakly_canonical(directory).string();}
unsigned unresolved(const Json &data) {
    unsigned result=0;for(const auto &entry:data["receipts"])
        if((entry["receipt"]["state"]=="pending"||entry["receipt"]["state"]=="unknown")&&entry["reconciled_revision"].is_null())++result;
    return result;
}
bool prune_terminal(Json &data) {
    const auto terminal=[](const Json &entry){return entry["receipt"]["state"]!="pending"&&
        (entry["receipt"]["state"]!="unknown"||!entry["reconciled_revision"].is_null());};
    unsigned count=0;for(const auto &entry:data["receipts"])if(terminal(entry))++count;
    bool evicted=false;
    while(count>256){
        auto &entries=data["receipts"];const auto first=std::find_if(entries.begin(),entries.end(),terminal);
        if(first==entries.end())throw std::runtime_error("invalid_management_recovery_state");
        entries.erase(first);--count;evicted=true;
    }
    return evicted;
}
bool hex(const Json &v,std::size_t n) {return v.is_string() && v.get_ref<const std::string&>().size()==n && v.get_ref<const std::string&>().find_first_not_of("0123456789abcdef")==std::string::npos;}
bool fields(const Json &v,std::initializer_list<const char *> names) {
    if(!v.is_object()||v.size()!=names.size())return false;
    for(auto name:names)if(!v.contains(name))return false;
    return true;
}
bool decimal(const Json &v) {
    if(!v.is_string())return false;
    const auto &s=v.get_ref<const std::string&>();std::uint64_t n{};
    if(s.empty()||s[0]=='0')return false;
    const auto r=std::from_chars(s.data(),s.data()+s.size(),n);
    return r.ec==std::errc{}&&r.ptr==s.data()+s.size()&&n;
}
bool binding(const Json &v,const std::string &device) {
    return fields(v,{"device_id","boot_id","profile_generation","connection_generation"})&&v["device_id"]==device&&
        hex(v["boot_id"],32)&&decimal(v["profile_generation"])&&decimal(v["connection_generation"]);
}
bool public_value(const Json &v,unsigned depth=0) {
    if(depth>16)return false;
    if(v.is_object())for(const auto &[key,value]:v.items()) {
        if((key=="password"||key=="private_key"||key=="psk"||key=="pairing_code"||key=="secret")&&!value.is_null())return false;
        if(!public_value(value,depth+1))return false;
    }
    if(v.is_array())for(const auto &value:v)if(!public_value(value,depth+1))return false;
    return true;
}
void require(bool value) {if(!value)throw std::runtime_error("invalid_management_recovery_state");}
bool directory(const std::string &path,bool create) {
    struct stat existing{};
    if(lstat(path.c_str(),&existing)&&errno==ENOENT&&!create)return false;
    if(create&&mkdir(path.c_str(),0700)&&errno!=EEXIST)throw std::runtime_error("management_storage_unavailable");
    struct stat s{};
    require(!lstat(path.c_str(),&s)&&S_ISDIR(s.st_mode)&&s.st_uid==geteuid()&&!(s.st_mode&0077));
    if(create) {
        auto parent=std::filesystem::path(path).parent_path();if(parent.empty())parent=".";
        const int fd=open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);require(fd>=0);
        const auto synced=fsync(fd);close(fd);require(!synced);
    }
    return true;
}
}
std::string PicoManagementJournal::digest(const std::string &text) {
    std::array<unsigned char,SHA256_DIGEST_LENGTH> bytes{};
    SHA256(reinterpret_cast<const unsigned char *>(text.data()),text.size(),bytes.data());
    std::string result;for(auto b:bytes){result+="0123456789abcdef"[b>>4];result+="0123456789abcdef"[b&15];}return result;
}
PicoManagementJournal::PicoManagementJournal(std::string dir,std::string controller,std::string device,
    PicoManagementHistoryAuthority authority)
    :directory_(std::move(dir)),controller_(std::move(controller)),device_(std::move(device)),history_(std::move(authority)) {
    require(hex(controller_,32)&&hex(device_,32));path_=directory_+"/"+device_+".json";
    unsigned char bytes[16];require(RAND_bytes(bytes,sizeof(bytes))==1);
    for(auto b:bytes){initial_epoch_+="0123456789abcdef"[b>>4];initial_epoch_+="0123456789abcdef"[b&15];}
}
void PicoManagementJournal::validate(const Json &data) const {
    require(fields(data,{"schema","controller_id","device_id","history_id","receipts","checksum"})&&hex(data["history_id"],32)&&data["schema"]=="pico-management-operations/1"&&
        data["controller_id"]==controller_&&data["device_id"]==device_&&data["receipts"].is_array()&&data["receipts"].size()<=288);
    auto checked=data;checked.erase("checksum");require(hex(data["checksum"],64)&&data["checksum"]==digest(checked.dump()));
    unsigned terminal=0,unresolved=0;std::set<std::string> ids;
    for(const auto &entry:data["receipts"]) {
        require(fields(entry,{"principal_id","route","method","authorization_generation","receipt","response","reconciled_revision"}));
        require(hex(entry["principal_id"],32)&&decimal(entry["authorization_generation"]));
        require(entry["route"].is_string()&&entry["method"].is_string());
        const auto route=entry["route"].get<std::string>();
        const auto prefix="/api/control/v1/targets/"+device_+"/pico/";
        require(route.starts_with(prefix)&&route.size()<=prefix.size()+64&&route.size()>prefix.size());
        const auto resource=route.substr(prefix.size());
        require(resource!="management"&&resource!="pins/validate"&&entry["method"]!="GET"&&
            controller_pico_route(resource,entry["method"].get<std::string>()));
        const auto &r=entry["receipt"];
        require(fields(r,{"contract","schema","binding","operation_id","request_digest","state","revision","error"})&&
            valid_controller_document(r,"target-operation")&&r["contract"]=="Wsprry-Control/1"&&r["schema"]=="target-operation"&&binding(r["binding"],device_)&&
            hex(r["operation_id"],32)&&ids.insert(r["operation_id"]).second&&hex(r["request_digest"],64)&&
            (r["revision"].is_null()||hex(r["revision"],64)));
        const auto state=r["state"];
        require(state=="pending"||state=="unknown"||state=="committed"||state=="rejected");
        require(r["error"].is_null()||(fields(r["error"],{"code","retryable"})&&
            r["error"]["code"]=="operation_unknown"&&r["error"]["retryable"]==false));
        const auto &reconciled=entry["reconciled_revision"];
        require(reconciled.is_null()||(hex(reconciled,64)&&state=="unknown"));
        if((state=="pending"||state=="unknown")&&reconciled.is_null())++unresolved;else ++terminal;
        const auto &response=entry["response"];
        if(state=="committed"||state=="rejected") {
            require(r["error"].is_null());
            require(fields(response,{"status","body","etag"})&&response["status"].is_number_unsigned()&&response["status"]>=200&&response["status"]<500&&
                (state=="committed"?response["status"]<300:response["status"]>=400)&&
                response["body"].is_object()&&response["body"].dump().size()<=32768&&public_value(response["body"])&&
                response["etag"].is_string()&&response["etag"].get_ref<const std::string&>().size()<=128&&response["etag"].get_ref<const std::string&>().find_first_of("\r\n")==std::string::npos);
        } else require(response.is_null());
    }
    require(terminal<=256&&unresolved<=32);
}
nlohmann::json PicoManagementJournal::load() {
    const auto empty=[&]{Json result{{"schema","pico-management-operations/1"},{"controller_id",controller_},{"device_id",device_},{"history_id",initial_epoch_},{"receipts",Json::array()}};result["checksum"]=digest(result.dump());return result;};
    if(!directory(directory_,false))return empty();
    const int fd=open(path_.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if(fd<0) {if(errno==ENOENT)return empty();throw std::runtime_error("management_storage_unavailable");}
    struct Close {int fd;~Close(){close(fd);}} close_fd{fd};struct stat s{};
    require(!fstat(fd,&s)&&S_ISREG(s.st_mode)&&s.st_nlink==1&&s.st_uid==geteuid()&&!(s.st_mode&0077)&&s.st_size>=0&&static_cast<std::size_t>(s.st_size)<=maximum_bytes);
    std::string bytes;std::array<char,4096> block{};
    for(;;) {auto n=read(fd,block.data(),block.size());if(n<0&&errno==EINTR)continue;require(n>=0);if(!n)break;bytes.append(block.data(),n);require(bytes.size()<=maximum_bytes);}
    std::vector<std::set<std::string>> keys;
    auto data=Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json &v) {
        require(depth<=24);if(event==Json::parse_event_t::object_start)keys.emplace_back();
        if(event==Json::parse_event_t::key)require(keys.back().insert(v.get<std::string>()).second);
        if(event==Json::parse_event_t::object_end)keys.pop_back();
        return true;
    });
    validate(data);return data;
}
void PicoManagementJournal::save(const Json &data) {
    auto sealed=data;sealed.erase("checksum");sealed["checksum"]=digest(sealed.dump());
    validate(sealed);const auto bytes=sealed.dump();require(bytes.size()<=maximum_bytes);directory(directory_,true);
    auto pattern=path_+".tmp.XXXXXX";std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);
    int fd=mkstemp(name.data());if(fd<0)throw std::runtime_error("management_storage_unavailable");
    try {
        require(!fchmod(fd,0600));
        std::size_t offset=0;while(offset<bytes.size()){auto n=write(fd,bytes.data()+offset,bytes.size()-offset);if(n<0&&errno==EINTR)continue;require(n>0);offset+=n;}
        require(!fsync(fd));const auto closed=close(fd);fd=-1;require(!closed);
        require(!rename(name.data(),path_.c_str()));
        const int dir=open(directory_.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);require(dir>=0);
        const int synced=fsync(dir);close(dir);require(!synced);record_authority(sealed);
    } catch(...) {if(fd>=0)close(fd);unlink(name.data());throw;}
}
void PicoManagementJournal::check_authority() {
    const auto key=authority_key(directory_);
    auto found=authorities.find(key);
    if(found==authorities.end()) {
        if(authorities.size()>=64)throw std::runtime_error("management_storage_capacity");
        found=authorities.emplace(key,Authority{controller_,{},false}).first;
    }
    auto &authority=found->second;require(authority.controller==controller_);
    const auto enrollment=[&]{
        if(!history_.complete())return;
        const auto view=history_.read();
        for(const auto &[id,epoch]:view.epochs)
            require(authority.files.contains(id)&&authority.files.at(id).epoch==epoch);
        for(const auto &[id,file]:authority.files)if(!view.epochs.contains(id)&&file.nonempty)
            require(view.retired_epochs.contains(id)&&view.retired_epochs.at(id)==file.epoch&&
                !view.devices.contains(id)&&file.unresolved==0);
        for(const auto &[id,epoch]:view.retired_epochs)
            require(!view.epochs.contains(id)&&!view.devices.contains(id)&&
                (!authority.files.contains(id)||(authority.files.at(id).epoch==epoch&&authority.files.at(id).unresolved==0)));
    };
    std::map<std::string,FileStamp> observed;
    if(directory(directory_,false)) {
        unsigned temporary=0;
        for(const auto &entry:std::filesystem::directory_iterator(directory_)) {
            const auto name=entry.path().filename().string();
            const bool journal=name.size()==37&&name.ends_with(".json")&&hex(name.substr(0,32),32);
            const bool staging=name.size()==48&&name.substr(32,10)==".json.tmp."&&hex(name.substr(0,32),32)&&
                name.substr(42).find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")==std::string::npos;
            require(journal||staging);
            const auto current=stamp(entry.path().string());
            require(S_ISREG(current.mode)&&current.uid==geteuid()&&!(current.mode&0077)&&current.links==1&&current.size<=maximum_bytes);
            if(staging) {require(++temporary<=64);continue;}
            observed.emplace(name.substr(0,32),current);require(observed.size()<=96);
        }
    }
    if(authority.loaded) {
        require(observed.size()==authority.files.size());
        for(const auto &[id,current]:observed)require(authority.files.contains(id)&&authority.files.at(id).stamp==current);
        enrollment();
        return;
    }
    std::map<std::string,Authority::File> files;unsigned total=0;
    for(const auto &[id,current]:observed) {
        PicoManagementJournal peer(directory_,controller_,id);const auto data=peer.load();
        require(stamp(peer.path_)==current);const auto count=unresolved(data);total+=count;
        files.emplace(id,Authority::File{current,count,data["history_id"].get<std::string>(),!data["receipts"].empty()});
    }
    require(total<=32);authority.files=std::move(files);authority.loaded=true;enrollment();
}
void PicoManagementJournal::record_authority(const Json &data) {
    auto &authority=authorities.at(authority_key(directory_));require(authority.loaded&&authority.controller==controller_);
    authority.files[device_]={stamp(path_),unresolved(data),data["history_id"].get<std::string>(),!data["receipts"].empty()};
}
void PicoManagementJournal::prune_retired() {
    std::lock_guard lock(mutex_);std::lock_guard recovery(recovery_mutex);
    (void)load();check_authority();if(!history_.complete())return;
    const auto view=history_.read();auto &known=authorities.at(authority_key(directory_)).files;
    const auto files=known;
    const auto sync_absence=[&]{
        auto selected=std::filesystem::path(directory_);
        if(!directory(directory_,false)){selected=selected.parent_path();if(selected.empty())selected=".";}
        selected=std::filesystem::canonical(selected);
        const int fd=::open(selected.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);require(fd>=0);
        const auto synced=::fsync(fd);::close(fd);require(!synced);
    };
    for(const auto &[id,file]:files) {
        const bool empty_unpublished=!view.epochs.contains(id)&&!file.nonempty;
        if(!empty_unpublished&&(view.devices.contains(id)||file.unresolved))continue;
        if(!empty_unpublished)history_.retire(id,file.epoch);
        const auto path=directory_+"/"+id+".json";
        require(!::unlink(path.c_str())||errno==ENOENT);
        sync_absence();known.erase(id);
        if(!empty_unpublished)history_.finish_retirement(id,file.epoch);
    }
    for(const auto &[id,epoch]:view.retired_epochs)if(!files.contains(id)) {
        sync_absence();history_.finish_retirement(id,epoch);
    }
    check_authority();
}
nlohmann::json PicoManagementJournal::snapshot(){std::lock_guard lock(mutex_);std::lock_guard recovery(recovery_mutex);auto data=load();check_authority();return data;}
std::optional<nlohmann::json> PicoManagementJournal::reserve(const std::string &principal,const std::string &route,
    const std::string &method,const Json &bound,const std::string &authorization,const std::string &operation,const std::string &request_digest) {
    std::lock_guard lock(mutex_);std::lock_guard recovery(recovery_mutex);auto data=load();check_authority();
    require(hex(operation,32)&&hex(request_digest,64)&&binding(bound,device_));
    for(const auto &entry:data["receipts"])if(entry["receipt"]["operation_id"]==operation) {
        if(entry["principal_id"]!=principal||entry["route"]!=route||entry["method"]!=method||entry["authorization_generation"]!=authorization||
           entry["receipt"]["request_digest"]!=request_digest||entry["receipt"]["binding"]!=bound)throw std::runtime_error("operation_id_conflict");
        return entry;
    }
    const auto &known=authorities.at(authority_key(directory_)).files;
    if(!known.contains(device_)&&known.size()>=96)throw std::runtime_error("management_storage_capacity");
    unsigned total=0;for(const auto &[name,entry]:known){(void)name;total+=entry.unresolved;}
    if(total>=32)throw std::runtime_error("management_operation_capacity");
    for(const auto &entry:data["receipts"]) {
        const auto state=entry["receipt"]["state"];
        if((state=="pending"||state=="unknown")&&entry["reconciled_revision"].is_null())throw std::runtime_error("management_readback_review_required");
    }
    if(history_.complete()) {
        // Empty journal first, catalog anchor second, reservation last. No
        // native mutation can run until all three publications are durable.
        require(history_.read().devices.contains(device_));
        if(!known.contains(device_))save(data);
        history_.publish(device_,data["history_id"].get<std::string>());
        check_authority();
    }
    data["receipts"].push_back({{"principal_id",principal},{"route",route},{"method",method},{"authorization_generation",authorization},
        {"receipt",{{"contract","Wsprry-Control/1"},{"schema","target-operation"},{"binding",bound},{"operation_id",operation},
        {"request_digest",request_digest},{"state","pending"},{"revision",nullptr},{"error",nullptr}}},{"response",nullptr},{"reconciled_revision",nullptr}});
    save(data);return {};
}
bool PicoManagementJournal::finish(const std::string &operation,const PicoHttpResponse &response) {
    std::lock_guard lock(mutex_);std::lock_guard recovery(recovery_mutex);auto data=load();check_authority();bool found=false;
    for(auto &entry:data["receipts"])if(entry["receipt"]["operation_id"]==operation) {
        require(entry["receipt"]["state"]=="pending");found=true;
        Json body;
        try {body=strict_browser_json(response.body);}catch(...){body=nullptr;}
        const bool certain=((response.status>=200&&response.status<300)||(response.status>=400&&response.status<500))&&
            body.is_object()&&body.dump().size()<=32768&&public_value(body)&&
            !(body.contains("error")&&body["error"].is_object()&&body["error"].value("code",Json(nullptr))=="management_outcome_unknown");
        entry["receipt"]["state"]=certain?(response.status<300?"committed":"rejected"):"unknown";
        if(certain) {
            entry["response"]={{"status",response.status},{"body",body},{"etag",response.etag}};
            if(response.etag.size()==66&&response.etag.front()=='"'&&hex(response.etag.substr(1,64),64))entry["receipt"]["revision"]=response.etag.substr(1,64);
        } else entry["receipt"]["error"]={{"code","operation_unknown"},{"retryable",false}};
    }
    require(found);const auto evicted=prune_terminal(data);save(data);return evicted;
}
nlohmann::json PicoManagementJournal::receipt(const std::string &operation) {
    std::lock_guard lock(mutex_);std::lock_guard recovery(recovery_mutex);const auto data=load();check_authority();for(const auto &entry:data["receipts"])if(entry["receipt"]["operation_id"]==operation) {
        auto result=entry["receipt"];if(result["state"]=="pending"){result["state"]="unknown";result["error"]={{"code","operation_unknown"},{"retryable",false}};}return result;
    }
    return nullptr;
}
bool PicoManagementJournal::reconcile(const std::string &operation,const Json &bound,const std::string &revision) {
    std::lock_guard lock(mutex_);std::lock_guard recovery(recovery_mutex);auto data=load();check_authority();bool found=false;require(binding(bound,device_)&&hex(revision,64));
    for(auto &entry:data["receipts"])if(entry["receipt"]["operation_id"]==operation) {
        const auto state=entry["receipt"]["state"];require(state=="pending"||state=="unknown");
        // Reconciliation can observe a new boot; it never replays the old write.
        require(entry["receipt"]["binding"]["device_id"]==bound["device_id"]);
        entry["receipt"]["state"]="unknown";entry["receipt"]["error"]={{"code","operation_unknown"},{"retryable",false}};
        entry["reconciled_revision"]=revision;found=true;
    }
    require(found);const auto evicted=prune_terminal(data);save(data);return evicted;
}
} // namespace wsprrypi
