// SPDX-License-Identifier: MIT
#include "pi_management_journal.hpp"
#ifdef _WIN32
#include "native_windows_file.hpp"
#else
#include "native_private_acl.hpp"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <array>
#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <mutex>
#include <set>
#include <stdexcept>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
constexpr std::size_t maximum_bytes=1048576;
std::recursive_mutex mutex;
void require(bool condition){if(!condition)throw std::runtime_error("pi_management_storage_hold");}
bool hex(const Json &value,std::size_t length){return value.is_string()&&value.get_ref<const std::string&>().size()==length&&value.get_ref<const std::string&>().find_first_not_of("0123456789abcdef")==std::string::npos;}
bool fields(const Json &value,std::initializer_list<const char *> names){if(!value.is_object()||value.size()!=names.size())return false;for(auto name:names)if(!value.contains(name))return false;return true;}
std::string fresh(){std::array<unsigned char,16> data{};require(RAND_bytes(data.data(),static_cast<int>(data.size()))==1);std::string result;for(auto byte:data){result+="0123456789abcdef"[byte>>4];result+="0123456789abcdef"[byte&15];}return result;}
#ifdef _WIN32
#include "pi_management_files_windows.hpp"
#else
using FileMode=mode_t;
std::filesystem::path disk_path(const std::string &text){return std::filesystem::path(text);}
std::string path_text(const std::filesystem::path &path){return path.string();}
std::string path_key(const std::filesystem::path &path){return path.string();}
std::filesystem::path owned_parent(const std::string &text){
    const auto directory=std::filesystem::canonical(disk_path(text).parent_path().empty()?".":disk_path(text).parent_path());
    struct stat owner{};
    require(!::lstat(directory.c_str(),&owner)&&S_ISDIR(owner.st_mode)&&owner.st_uid==geteuid()&&!(owner.st_mode&0022));
    require(native_private_acl_path(directory.c_str(),owner.st_uid));return directory;
}
std::filesystem::path normalized_destination(const std::string &text){return owned_parent(text)/disk_path(text).filename();}
void remove_file(const std::filesystem::path &path,bool missing=false){if(::unlink(path.c_str()))require(missing&&errno==ENOENT);}
struct File {std::string bytes;FileMode mode;std::string signature;};
bool same_file(const struct stat &before,const struct stat &after){
#ifdef __APPLE__
    const auto modified_before=before.st_mtimespec,modified_after=after.st_mtimespec;
    const auto changed_before=before.st_ctimespec,changed_after=after.st_ctimespec;
#else
    const auto modified_before=before.st_mtim,modified_after=after.st_mtim;
    const auto changed_before=before.st_ctim,changed_after=after.st_ctim;
#endif
    return before.st_dev==after.st_dev&&before.st_ino==after.st_ino&&
        before.st_size==after.st_size&&before.st_mode==after.st_mode&&
        before.st_uid==after.st_uid&&before.st_nlink==after.st_nlink&&
        modified_before.tv_sec==modified_after.tv_sec&&modified_before.tv_nsec==modified_after.tv_nsec&&
        changed_before.tv_sec==changed_after.tv_sec&&changed_before.tv_nsec==changed_after.tv_nsec;
}
std::string signature(const struct stat &info,const std::string &bytes){
#ifdef __APPLE__
    const auto modified=info.st_mtimespec,changed=info.st_ctimespec;
#else
    const auto modified=info.st_mtim,changed=info.st_ctim;
#endif
    return PiManagementJournal::digest(Json::array({info.st_dev,info.st_ino,info.st_size,info.st_mode,
        info.st_uid,info.st_nlink,modified.tv_sec,modified.tv_nsec,changed.tv_sec,changed.tv_nsec,
        PiManagementJournal::digest(bytes)}).dump());
}
std::optional<File> read_file(const std::string &path,bool private_file,bool stabilize=false){
    if(private_file)require(native_private_acl_ancestors(path.c_str()));
    const int fd=::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if(fd<0){require(errno==ENOENT);return {};}
    struct Close{int fd;~Close(){::close(fd);}} close{fd};struct stat before{},after{};
    require(!::fstat(fd,&before)&&S_ISREG(before.st_mode)&&before.st_nlink==1&&before.st_uid==geteuid()&&before.st_size>=0&&static_cast<std::size_t>(before.st_size)<=maximum_bytes&&!(before.st_mode&(private_file?0077:0022)));
    if(private_file)require(native_private_acl(fd,before.st_uid));
    File result{{},static_cast<mode_t>(before.st_mode&0777),{}};std::array<char,4096> block{};
    for(;;){const auto count=::read(fd,block.data(),block.size());if(count<0&&errno==EINTR)continue;require(count>=0);if(!count)break;result.bytes.append(block.data(),count);require(result.bytes.size()<=maximum_bytes);}
    struct stat descriptor_after{};
    require(!::fstat(fd,&descriptor_after)&&!::lstat(path.c_str(),&after)&&same_file(before,descriptor_after)&&same_file(before,after));
    result.signature=signature(after,result.bytes);
    if(stabilize)require(!::fsync(fd));
    return result;
}
std::optional<std::string> hash(const std::optional<File> &file){return file?std::optional<std::string>(PiManagementJournal::digest(file->bytes)):std::nullopt;}
void write_file(const std::string &path,const std::string &bytes,FileMode mode,bool exclusive){
    require(bytes.size()<=maximum_bytes);
    const auto temporary=exclusive?path:path+".tmp."+fresh();
    int fd=::open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);require(fd>=0);
    try{
        require(!::fchmod(fd,mode));if(!(mode&0077))require(native_private_acl(fd,::geteuid()));std::size_t offset=0;
        while(offset<bytes.size()){const auto count=::write(fd,bytes.data()+offset,bytes.size()-offset);if(count<0&&errno==EINTR)continue;require(count>0);offset+=count;}
        require(!::fsync(fd));const auto closed=::close(fd);fd=-1;require(!closed);
        if(!exclusive)require(!::rename(temporary.c_str(),path.c_str()));
    }catch(...){if(fd>=0)::close(fd);::unlink(temporary.c_str());throw;}
}
#endif
std::string parent(const std::string &path){const auto value=disk_path(path).parent_path();return value.empty()?".":path_text(value);}
}
std::string PiManagementJournal::digest(const std::string &value){std::array<unsigned char,SHA256_DIGEST_LENGTH> bytes{};SHA256(reinterpret_cast<const unsigned char *>(value.data()),value.size(),bytes.data());std::string result;for(auto byte:bytes){result+="0123456789abcdef"[byte>>4];result+="0123456789abcdef"[byte&15];}return result;}
PiManagementJournal::PiManagementJournal(std::string dir,std::string self,std::map<std::string,std::string> paths):directory_(std::move(dir)),self_(std::move(self)),wal_(directory_+"/transaction.json"),destinations_(std::move(paths)){
    // The fixed owner namespace may contain seven destinations; each complete
    // decision still contains at most six images. Initialization adds progress
    // proof only to the no-INI enable transaction, never a seventh image.
    require(!directory_.empty()&&hex(self_,32)&&!destinations_.empty()&&destinations_.size()<=7);
    const auto directory_parent=owned_parent(directory_);
    const auto name=disk_path(directory_).filename();
    require(!name.empty()&&name!="."&&name!="..");
    directory_=path_text(normalized_destination(path_text(directory_parent/name)));wal_=directory_+"/transaction.json";
    std::set<std::string> unique;
    const auto storage=path_key(disk_path(directory_));
    for(auto &[kind,path]:destinations_){
        require(kind=="ini"||kind=="settings"||kind=="operations"||kind=="authority"||kind=="recurrence"||kind=="retirement-anchor"||kind=="legacy"||kind=="progress"||kind=="pair"||kind=="peers"||kind=="assignments");
        require(!path.empty());
        const auto canonical=normalized_destination(path);
        // Final-file reparse points are never followed by either adapter.
        for(auto ancestor=canonical;!ancestor.empty();ancestor=ancestor.parent_path()){require(path_key(ancestor)!=storage);if(ancestor==ancestor.parent_path())break;}
        require(unique.insert(path_key(canonical)).second);
        path=path_text(canonical);
    }
    // P62-05 adds this image only to the new pairing owner. It cannot be
    // substituted for settings/progress or mixed into a transmitter save.
    if(destinations_.contains("peers"))require(destinations_.size()==4&&destinations_.contains("pair")&&
        destinations_.contains("operations")&&destinations_.contains("authority")&&
        parent(destinations_.at("peers"))==parent(destinations_.at("pair"))&&
        parent(destinations_.at("peers"))==parent(destinations_.at("operations")));
}
namespace {
void sync_owned_directory(const std::string &path){
#ifdef _WIN32
    wf::flush_directory(disk_path(path),wf::current_sid());
#else
    const int fd=::open(path.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);require(fd>=0);const auto result=::fsync(fd);::close(fd);require(!result);
#endif
}
}
void PiManagementJournal::sync_directory(const std::string &path) const {sync_owned_directory(path);}
void PiManagementJournal::directory(bool create){
#ifdef _WIN32
    const auto path=disk_path(directory_);
    if(!exists_file(path)){
        if(!create)return;
        sync_directory(parent(directory_));wf::PrivateSecurity security(wf::current_sid());
        require(::CreateDirectoryW(wf::api_path(path).c_str(),security.attributes()));sync_directory(parent(directory_));
    }
    (void)private_directory_handle(path);
#else
    struct stat info{};
    if(::lstat(directory_.c_str(),&info)){
        require(errno==ENOENT);if(!create)return;
        require(!::mkdir(directory_.c_str(),0700));sync_directory(parent(directory_));require(!::lstat(directory_.c_str(),&info));
    }
    require(S_ISDIR(info.st_mode)&&info.st_uid==geteuid()&&!(info.st_mode&0077));
    require(native_private_acl_path(directory_.c_str(),info.st_uid));
#endif
}
void PiManagementJournal::validate(const Json &data) const {
    require(fields(data,{"schema","transaction_id","device_id","state","records"})&&data["schema"]=="pi-management-transaction/1"&&hex(data["transaction_id"],32)&&data["device_id"]==self_&&(data["state"]=="prepared"||data["state"]=="committed")&&data["records"].is_array()&&!data["records"].empty()&&data["records"].size()<=6);
    std::set<std::string> kinds,names;
    for(const auto &row:data["records"]){
        require(fields(row,{"kind","old_digest","new_digest","staged_name"})&&row["kind"].is_string()&&destinations_.contains(row["kind"].get<std::string>())&&(row["old_digest"].is_null()||hex(row["old_digest"],64))&&hex(row["new_digest"],64)&&hex(row["staged_name"],32));
        require(kinds.insert(row["kind"].get<std::string>()).second&&names.insert(row["staged_name"].get<std::string>()).second);
    }
}
std::optional<Json> PiManagementJournal::decision(){
    std::lock_guard lock(mutex);directory(false);const auto file=read_file(wal_,true);if(!file)return {};
    std::vector<std::set<std::string>> objects;
    const auto data=Json::parse(file->bytes,[&](int depth,Json::parse_event_t event,Json &value){require(depth<=8);if(event==Json::parse_event_t::object_start)objects.emplace_back();if(event==Json::parse_event_t::key)require(objects.back().insert(value.get<std::string>()).second);if(event==Json::parse_event_t::object_end)objects.pop_back();return true;});
    validate(data);return data;
}
std::optional<PiManagementJournal::Observation> PiManagementJournal::inspect(const std::string &kind) const {
    std::lock_guard lock(mutex);
    const auto destination=destinations_.find(kind);require(destination!=destinations_.end());
    const auto file=read_file(destination->second,kind!="ini");
    if(!file)return {};
    return Observation{file->bytes,file->signature};
}
PiManagementJournal::Observation PiManagementJournal::read_ini_input(const std::string &path){
    std::lock_guard lock(mutex);
    const auto file=read_file(path,false);require(file.has_value());
    return {file->bytes,file->signature};
}
std::optional<PiManagementJournal::Observation> PiManagementJournal::read_private_record(const std::string &path){
    std::lock_guard lock(mutex);
    const auto file=read_file(path,true);if(!file)return {};
    return Observation{file->bytes,file->signature};
}
PiManagementJournal::Observation PiManagementJournal::publish_private_record(const std::string &path,
    const std::optional<Observation> &expected,const std::string &bytes){
    std::lock_guard lock(mutex);
    const auto owner_directory=owned_parent(path);
    const auto name=disk_path(path).filename();require(!name.empty()&&name!="."&&name!="..");
    const auto destination=path_text(normalized_destination(path_text(owner_directory/name)));
    const auto before=read_file(destination,true);
    require(bool(before)==bool(expected));
    if(before)require(before->bytes==expected->bytes&&before->signature==expected->signature);
    write_file(destination,bytes,0600,false);
    sync_owned_directory(path_text(owner_directory));
    const auto after=read_file(destination,true);require(after&&after->bytes==bytes);
    return {after->bytes,after->signature};
}
void PiManagementJournal::write_decision(const Json &data){validate(data);write_file(wal_,data.dump(),0600,false);sync_directory(directory_);}
void PiManagementJournal::clean_images(const std::optional<Json> &retained){
    if(retained)require(decision()==retained&&retained->at("state")=="prepared");
    else require(!read_file(wal_,true));
    std::set<std::string> keep;
    if(retained)for(const auto &row:retained->at("records")){
        const auto name=row.at("staged_name").get<std::string>();keep.insert(name+".old");keep.insert(name+".new");
    }
    // Without a WAL these are exclusively uncommitted staging or cleanup after
    // a fully applied, durably retired decision. They never authorize mutation.
    unsigned count=0;
    for(const auto &entry:std::filesystem::directory_iterator(disk_path(directory_))){
        const auto name=path_text(entry.path().filename());
        if(retained&&name=="transaction.json")continue;
        // Two complete six-image decisions plus an interrupted atomic WAL
        // replacement fit this bound. Retained referenced images never retire
        // before their replacement WAL is durable.
        require(++count<=32);
        require((name.size()==36&&(name.ends_with(".old")||name.ends_with(".new"))&&hex(name.substr(0,32),32))||
                (name.starts_with("transaction.json.tmp.")&&name.size()==53&&hex(name.substr(21),32)));
        require(read_file(path_text(entry.path()),true).has_value());
        if(!keep.contains(name))remove_file(entry.path());
    }
    sync_directory(directory_);
}
void PiManagementJournal::prepare(const std::vector<Image> &images,const std::optional<Json> &replace_rollback){
    std::lock_guard lock(mutex);require(!images.empty()&&images.size()<=6);
    const auto previous=decision();require(previous==replace_rollback);
    if(previous){
        require(previous->at("state")=="prepared");validate_prepared_inputs(*previous);
        const bool add_recurrence=previous->at("records").size()+1==images.size()&&
            std::none_of(previous->at("records").begin(),previous->at("records").end(),[](const auto &row){return row.at("kind")=="recurrence";});
        require(previous->at("records").size()==images.size()||add_recurrence);
        for(const auto &image:images){
            bool found=false;
            for(const auto &row:previous->at("records"))if(row["kind"]==image.kind){
                require(row["old_digest"]==(image.expected_old_digest?Json(*image.expected_old_digest):Json(nullptr)));found=true;
            }
            // The current schema may extend an exact retained first-write
            // proof only by the newly introduced recurrence image. Its current
            // absent/bootstrap bytes are independently checked below; no old
            // row may change its original before-image.
            require(found||(add_recurrence&&image.kind=="recurrence"));
        }
    }
    Json data{{"schema","pi-management-transaction/1"},{"transaction_id",fresh()},{"device_id",self_},{"state","prepared"},{"records",Json::array()}};
    std::vector<std::optional<File>> old;std::set<std::string> kinds;
    for(const auto &image:images){
        require(destinations_.contains(image.kind)&&kinds.insert(image.kind).second&&image.bytes.size()<=maximum_bytes);
        auto file=read_file(destinations_.at(image.kind),image.kind!="ini",true);require(hash(file)==image.expected_old_digest);
        if(image.expected_old_signature)require(file&&file->signature==*image.expected_old_signature);
        data["records"].push_back({{"kind",image.kind},{"old_digest",file?Json(digest(file->bytes)):Json(nullptr)},{"new_digest",digest(image.bytes)},{"staged_name",fresh()}});old.push_back(std::move(file));
    }
    validate(data);require(data.dump().size()<=maximum_bytes);
    directory(true);clean_images(previous);
    for(std::size_t i=0;i<images.size();++i){
        const auto stem=directory_+"/"+data["records"][i]["staged_name"].get<std::string>();
        if(old[i])write_file(stem+".old",old[i]->bytes,0600,true);
        write_file(stem+".new",images[i].bytes,0600,true);
    }
    sync_directory(directory_);
    // Establish the original present/absent directory entries durably before
    // a commit can authorize replacement, including files from legacy writers.
    for(const auto &image:images)sync_directory(parent(destinations_.at(image.kind)));
    write_decision(data);
    if(previous)clean_images(data);
    prepared_observations_.clear();
    for(std::size_t i=0;i<images.size();++i)
        prepared_observations_[images[i].kind]=old[i]?std::optional<Observation>(Observation{old[i]->bytes,old[i]->signature}):std::nullopt;
    prepared_transaction_=data.at("transaction_id").get<std::string>();
    ini_published_=false;
}
void PiManagementJournal::validate_prepared_inputs(const Json &data) const {
    require(data.at("state")=="prepared");
    // Check the original final signatures again at the linearization point.
    for(const auto &row:data.at("records")){
        const auto expected=row["old_digest"].is_null()?std::optional<std::string>{}:std::optional<std::string>(row["old_digest"].get<std::string>());
        const auto stem=directory_+"/"+row["staged_name"].get<std::string>();
        require(hash(read_file(stem+".old",true))==expected&&hash(read_file(stem+".new",true))==std::optional<std::string>(row["new_digest"].get<std::string>()));
        require(hash(read_file(destinations_.at(row["kind"].get<std::string>()),row["kind"]!="ini"))==expected);
    }
}
void PiManagementJournal::verify_prepared(const Json &expected){
    std::lock_guard lock(mutex);require(decision()==std::optional<Json>(expected));
    validate_prepared_inputs(expected);
}
void PiManagementJournal::commit(){
    std::lock_guard lock(mutex);auto data=decision();require(data.has_value());
    validate_prepared_inputs(*data);
    // A cold prepared decision is rollback evidence, never permission to
    // commit. The live preparer also fences equal-byte external replacements.
    require(prepared_transaction_==data->at("transaction_id").get<std::string>()&&prepared_observations_.size()==data->at("records").size());
    for(const auto &[kind,expected]:prepared_observations_){
        const auto current=inspect(kind);
        require(current.has_value()==expected.has_value()&&(!current||current->signature==expected->signature));
    }
    (*data)["state"]="committed";write_decision(*data);
}
void PiManagementJournal::write_committed_ini(const std::string &bytes){
    std::lock_guard lock(mutex);require(bytes.size()<=maximum_bytes&&!ini_published_);
    const auto data=decision();require(data&&data->at("state")=="committed"&&
        !prepared_transaction_.empty()&&prepared_transaction_==data->at("transaction_id").get<std::string>());
    // This callback is the transaction's first final-path publication. Check
    // every retained image and original observation before changing the INI.
    require(prepared_observations_.size()==data->at("records").size());
    for(const auto &row:data->at("records")){
        const auto kind=row.at("kind").get<std::string>(),stem=directory_+"/"+row.at("staged_name").get<std::string>();
        const auto expected_hash=row.at("old_digest").is_null()?std::optional<std::string>{}:std::optional<std::string>(row.at("old_digest").get<std::string>());
        require(hash(read_file(stem+".old",true))==expected_hash&&
            hash(read_file(stem+".new",true))==std::optional<std::string>(row.at("new_digest").get<std::string>()));
        const auto current=inspect(kind);const auto &expected=prepared_observations_.at(kind);
        require(current.has_value()==expected.has_value()&&(!current||current->signature==expected->signature));
    }
    const Json *record=nullptr;
    for(const auto &row:data->at("records"))if(row.at("kind")=="ini")record=&row;
    require(record&&prepared_observations_.contains("ini")&&digest(bytes)==record->at("new_digest").get<std::string>());
    const auto image=read_file(directory_+"/"+record->at("staged_name").get<std::string>()+".new",true);
    require(image&&image->bytes==bytes);
    const auto &path=destinations_.at("ini");const auto current=read_file(path,false);
    const auto &expected=prepared_observations_.at("ini");
    require(current.has_value()==expected.has_value()&&(!current||current->signature==expected->signature));
    ini_published_=true;
    write_file(path,bytes,current?current->mode:static_cast<FileMode>(0600),false);
    sync_directory(parent(path));
    const auto published=read_file(path,false);require(published&&published->bytes==bytes);
}
bool PiManagementJournal::recover(bool retain_prepared){
    std::lock_guard lock(mutex);const auto data=decision();if(!data)return false;
    prepared_transaction_.clear();prepared_observations_.clear();ini_published_=false;
    const bool committed=(*data)["state"]=="committed";
    require(!retain_prepared||!committed);
    struct Plan {std::string path;std::optional<File> image;FileMode mode;};std::vector<Plan> plans;
    for(const auto &row:(*data)["records"]){
        const auto kind=row["kind"].get<std::string>(),stem=directory_+"/"+row["staged_name"].get<std::string>();
        const auto old=read_file(stem+".old",true),next=read_file(stem+".new",true);
        require(hash(old)==(row["old_digest"].is_null()?std::optional<std::string>{}:std::optional<std::string>(row["old_digest"].get<std::string>()))&&next&&digest(next->bytes)==row["new_digest"].get<std::string>());
        const auto path=destinations_.at(kind);const auto current=read_file(path,kind!="ini");const auto current_hash=hash(current);
        require(current_hash==hash(old)||current_hash==hash(next));
        plans.push_back({path,committed?next:old,current?current->mode:static_cast<FileMode>(0600)});
    }
    for(const auto &plan:plans){
        if(plan.image)write_file(plan.path,plan.image->bytes,plan.mode,false);
        else remove_file(disk_path(plan.path),true);
        sync_directory(parent(plan.path));
        require(hash(read_file(plan.path,false))==hash(plan.image));
    }
    if(retain_prepared){clean_images(data);return true;}
    remove_file(disk_path(wal_));sync_directory(directory_);clean_images();return true;
}
} // namespace wsprrypi
