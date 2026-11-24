// SPDX-License-Identifier: MIT
#include "pi_pair_catalog_store.hpp"
#include "pi_pair_records.hpp"
#include "pi_pair_operations.hpp"
#include "pi_pair_peers.hpp"
#include "pi_pair_history_record.hpp"
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <set>
#ifdef _WIN32
#include "native_windows_directory_owner.hpp"
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <stdexcept>
namespace wsprrypi {
namespace {
void require(bool value){if(!value)throw std::runtime_error("pairing_storage_unavailable");}
bool same(const PiManagementJournal::Observation &a,const PiManagementJournal::Observation &b){return a.bytes==b.bytes&&a.signature==b.signature;}
bool same(const PiPairCatalogStore::Snapshot &a,const PiPairCatalogStore::Snapshot &b){return same(a.catalog,b.catalog)&&same(a.operations,b.operations)&&
    bool(a.authority)==bool(b.authority)&&(!a.authority||same(*a.authority,*b.authority))&&
    bool(a.peers)==bool(b.peers)&&(!a.peers||same(*a.peers,*b.peers));}
std::uint64_t number(const nlohmann::json &value){
    const auto text=value.get<std::string>();std::uint64_t result{};
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);require(parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size());return result;
}
void coherent(const nlohmann::json &catalog,const nlohmann::json &operations,const std::string &self,const nlohmann::json *peers=nullptr){
    require(valid_pi_pair_catalog(catalog,self)&&valid_pi_pair_operations(operations,self));
    if(peers)require(valid_pi_pair_peers(*peers,self));
    std::map<std::string,unsigned> rotations,registrations;
    const auto &active=catalog["active"];
    if(peers){
        std::set<std::string> identities;
        if(!active.is_null())require(identities.insert(active["pair_id"].get<std::string>()).second);
        for(const auto &row:catalog["retired"])require(identities.insert(row["pair_id"].get<std::string>()).second);
        for(const auto *group:{"profiles","retired","uncertain"})for(const auto &row:(*peers)[group])
            require(identities.insert(row["pair"]["pair_id"].get<std::string>()).second);
    }
    for(const auto &entry:operations["entries"]){
        if(entry["continuation"].is_null())continue;
        const auto &intent=entry["continuation"];const nlohmann::json *record=&active,*selected_profile=nullptr;
        if(peers&&entry["scope"]=="device"){
            record=nullptr;
            for(const auto &profile:(*peers)["profiles"])if(profile["profile_id"]==intent["profile_id"]){
                require(profile["stage"]=="active");record=&profile["pair"];selected_profile=&profile;break;
            }
            require(record);
        }
        const auto &pair=*record;
        require(!pair.is_null()&&pair["state"]=="active"&&pair["pair_id"]==entry["pair_id"]&&
            pair["authorization_generation"]==entry["authorization_generation"]&&pair["peer_id"]==intent["peer_id"]);
        if(intent["kind"]=="authority-registration-intent"){
            require(selected_profile&&entry["scope"]=="device"&&(*selected_profile)["profile_generation"]==intent["profile_generation"]&&
                (*selected_profile)["target_id"]==entry["target_id"]&&++registrations[entry["pair_id"].get<std::string>()]==1);
            continue;
        }
        require(++rotations[entry["pair_id"].get<std::string>()]==1);
        const auto current=number(pair["current"]["epoch"]),before=number(intent["expected_epoch"]);
        if(intent["stage"]=="awaiting_prepare")require(current==before&&pair["pending"].is_null()&&pair["previous"].is_null());
        else if(current==before)require(!pair["pending"].is_null()&&number(pair["pending"]["epoch"])==before+1&&pair["previous"].is_null());
        else require(entry["state"]=="committed"&&current==before+1&&pair["pending"].is_null()&&
            !pair["previous"].is_null()&&number(pair["previous"]["epoch"])==before&&!pair["receipt"].is_null()&&
            pair["receipt"]["operation_id"]==entry["operation_id"]&&pair["receipt"]["request_digest"]==entry["request_digest"]&&
            pair["receipt"]["result_digest"]==entry["result_digest"]);
    }
    const auto check_rotation=[&](const auto &pair){
        if(!pair.is_null()&&pair["state"]=="active"&&(!pair["pending"].is_null()||!pair["previous"].is_null()))
            require(rotations[pair["pair_id"].template get<std::string>()]==1);
    };
    check_rotation(active);
    if(peers)for(const auto *group:{"profiles","uncertain"})for(const auto &profile:(*peers)[group]){
        check_rotation(profile["pair"]);
        if(profile["stage"]!="awaiting_commit")continue;
        bool reserved=false;
        for(const auto &entry:operations["entries"])if(entry["operation_id"]==profile["initial_operation_id"]){
            require(entry["scope"]=="device"&&entry["principal_id"]==self&&entry["target_id"]==profile["target_id"]&&
                entry["pair_id"]==profile["pair"]["pair_id"]&&entry["authorization_generation"]==profile["pair"]["authorization_generation"]&&
                (entry["state"]=="pending"||entry["state"]=="unknown")&&entry["unresolved"]==true&&entry["continuation"].is_null());
            if(!entry["receipt"].is_null())require(entry["receipt"]["binding"]["profile_generation"]==profile["profile_generation"]);
            reserved=true;break;
        }
        require(reserved);
    }
}
}
struct PiPairCatalogStore::Lock {
#ifdef _WIN32
    std::unique_ptr<WindowsDirectoryOwner> owner;
#else
    int fd{-1};
    ~Lock(){if(fd>=0)::close(fd);}
#endif
};
PiPairCatalogStore::PiPairCatalogStore(std::string directory,std::string self,
    std::function<void(PiPairCatalogWritePoint)> checkpoint,std::optional<Authority> authority)
    :lock_(std::make_unique<Lock>()),self_(std::move(self)),checkpoint_(std::move(checkpoint)),authority_(std::move(authority)){
#ifdef _WIN32
    lock_->owner=std::make_unique<WindowsDirectoryOwner>(windows_file::path_from_utf8(directory));
    const auto normalized=lock_->owner->path().u8string();directory_.assign(normalized.begin(),normalized.end());verify_directory();
#else
    const auto requested=std::filesystem::path(directory),name=requested.filename();
    require(!name.empty()&&name!="."&&name!="..");
    const auto parent=std::filesystem::canonical(requested.has_parent_path()?requested.parent_path():".");
    struct stat owner{};require(!lstat(parent.c_str(),&owner)&&S_ISDIR(owner.st_mode)&&owner.st_uid==geteuid()&&!(owner.st_mode&0022));
    directory_=(parent/name).string();
    lock_->fd=open(directory_.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    require(lock_->fd>=0&&!flock(lock_->fd,LOCK_EX|LOCK_NB));verify_directory();
#endif
    std::map<std::string,std::string> destinations{{"pair",directory_+"/authority.json"},{"operations",directory_+"/operations.json"}};
    if(authority_){require(!authority_->path.empty());destinations["authority"]=authority_->path;destinations["peers"]=directory_+"/peers.json";}
    journal_=std::make_unique<PiManagementJournal>(directory_+"/transactions",self_,std::move(destinations));
    validate_decision();(void)journal_->recover();observed_=read();
}
PiPairCatalogStore::~PiPairCatalogStore()=default;
void PiPairCatalogStore::verify_directory() const {
#ifdef _WIN32
    require(bool(lock_->owner));lock_->owner->check();
#else
    struct stat descriptor{},named{};
    require(!fstat(lock_->fd,&descriptor)&&!lstat(directory_.c_str(),&named)&&S_ISDIR(named.st_mode)&&
        named.st_uid==geteuid()&&!(named.st_mode&0077)&&descriptor.st_dev==named.st_dev&&descriptor.st_ino==named.st_ino);
#endif
}
void PiPairCatalogStore::validate_decision(){
    const auto decision=journal_->decision();if(!decision)return;
    require(decision->at("records").size()<=(authority_?4:2));
    for(const auto suffix:{".old",".new"}){
        auto pair=journal_->inspect("pair"),operations=journal_->inspect("operations");
        auto authority=authority_?journal_->inspect("authority"):std::nullopt;
        auto peers=authority_?journal_->inspect("peers"):std::nullopt;
        for(const auto &image:decision->at("records")){
            require(!image["old_digest"].is_null());
            const auto path=directory_+"/transactions/"+image["staged_name"].get<std::string>()+suffix;
            const auto value=PiManagementJournal::read_private_record(path);require(value.has_value());
            require(PiManagementJournal::digest(value->bytes)==image[std::string(suffix)==".old"?"old_digest":"new_digest"].get_ref<const std::string &>());
            if(image["kind"]=="pair")pair=value;
            else if(image["kind"]=="operations")operations=value;
            else if(image["kind"]=="peers"){require(authority_.has_value());peers=value;}
            else {require(authority_&&image["kind"]=="authority");authority=value;}
        }
        require(pair&&operations);validate_snapshot({*pair,*operations,authority,peers});
    }
}
void PiPairCatalogStore::validate_snapshot(const Snapshot &value) const {
    const auto catalog=parse_pi_pair_catalog(value.catalog.bytes,self_),operations=parse_pi_pair_operations(value.operations.bytes,self_);
    const auto peers=value.peers?std::optional(parse_pi_pair_peers(value.peers->bytes,self_)):std::nullopt;
    coherent(catalog,operations,self_,peers?&*peers:nullptr);
    if(authority_){
        require(value.authority.has_value()&&value.peers.has_value());const auto record=parse_pi_pair_history_record(value.authority->bytes,self_);
        require(record["namespace_id"]==authority_->namespace_id&&record["authorization_generation"]==catalog["authorization_generation"]&&
            record["catalog_digest"]==PiManagementJournal::digest(value.catalog.bytes)&&record["operations_digest"]==PiManagementJournal::digest(value.operations.bytes)&&
            record["peers_digest"]==PiManagementJournal::digest(value.peers->bytes));
    }else require(!value.authority&&!value.peers);
}
PiPairCatalogStore::Snapshot PiPairCatalogStore::read(){
    verify_directory();require(!journal_->decision());const auto pair=journal_->inspect("pair"),operations=journal_->inspect("operations");require(pair&&operations);
    Snapshot value{*pair,*operations,authority_?journal_->inspect("authority"):std::nullopt,authority_?journal_->inspect("peers"):std::nullopt};validate_snapshot(value);verify_directory();return value;
}
PiPairCatalogStore::Snapshot PiPairCatalogStore::snapshot(){
    std::lock_guard guard(mutex_);require(!held_);
    try{auto value=read();require(observed_&&same(value,*observed_));return value;}
    catch(...){held_=true;throw;}
}
void PiPairCatalogStore::publish(const Snapshot &expected,const nlohmann::json &catalog){
    publish(expected,catalog,parse_pi_pair_operations(expected.operations.bytes,self_));
}
void PiPairCatalogStore::publish(const Snapshot &expected,const nlohmann::json &catalog,const nlohmann::json &operations,Checkpoint checkpoint){
    const auto peers=expected.peers?parse_pi_pair_peers(expected.peers->bytes,self_):nlohmann::json(nullptr);
    publish(expected,catalog,operations,peers,checkpoint);
}
void PiPairCatalogStore::publish(const Snapshot &expected,const nlohmann::json &catalog,const nlohmann::json &operations,const nlohmann::json &peers,Checkpoint checkpoint,const std::function<void()> &before_commit){
    std::lock_guard guard(mutex_);require(!held_);
    require(checkpoint==Checkpoint::Preserve||checkpoint==Checkpoint::Running||checkpoint==Checkpoint::CleanShutdown);
    require(authority_||checkpoint==Checkpoint::Preserve);
    // Malformed proposed data is a caller error, not permission to write or to
    // reinterpret existing authority. Failure after preparing holds this owner
    // until a fresh open performs the complete existing WAL recovery.
    require(authority_?!peers.is_null():peers.is_null());
    coherent(catalog,operations,self_,authority_?&peers:nullptr);const auto next=catalog.dump(),next_operations=operations.dump();
    Snapshot current;
    try{
        current=read();require(observed_&&same(current,*observed_));
    }catch(...){held_=true;throw;}
    // A stale caller does not poison otherwise sound storage.
    if(!same(current,expected))throw std::invalid_argument("pairing_catalog_conflict");
    owned_decision_.reset();owned_completed_.reset();
    try{
        std::vector<PiManagementJournal::Image> images{{"pair",PiManagementJournal::digest(current.catalog.bytes),next,current.catalog.signature},
            {"operations",PiManagementJournal::digest(current.operations.bytes),next_operations,current.operations.signature}};
        if(authority_){
            require(current.authority.has_value());const auto old=parse_pi_pair_history_record(current.authority->bytes,self_);
            const auto clean=checkpoint==Checkpoint::Preserve?old["clean_shutdown"].get<bool>():checkpoint==Checkpoint::CleanShutdown;
            require(current.peers.has_value());
            images.push_back({"peers",PiManagementJournal::digest(current.peers->bytes),peers.dump(),current.peers->signature});
            const auto next_authority=pi_pair_history_record(self_,authority_->namespace_id,old["initialization"],catalog,operations,peers,clean);
            images.push_back({"authority",PiManagementJournal::digest(current.authority->bytes),next_authority.dump(),current.authority->signature});
        }else require(checkpoint==Checkpoint::Preserve);
        journal_->prepare(images);
        owned_decision_=journal_->decision();require(owned_decision_&&(*owned_decision_)["state"]=="prepared");
        if(checkpoint_)checkpoint_(PiPairCatalogWritePoint::Prepared);
        if(before_commit)before_commit();
        journal_->commit();if(checkpoint_)checkpoint_(PiPairCatalogWritePoint::Committed);
        require(journal_->recover());observed_=read();owned_completed_=observed_;
        if(checkpoint_)checkpoint_(PiPairCatalogWritePoint::Published);
        owned_decision_.reset();owned_completed_.reset();
    }catch(...){held_=true;throw;}
}
bool PiPairCatalogStore::recover_own_failure() noexcept {
    std::lock_guard guard(mutex_);
    if(!held_)return false;
    try{
        require(held_&&owned_decision_.has_value());verify_directory();const auto actual=journal_->decision();
        if(actual){
            auto expected=*owned_decision_;
            if((*actual)["state"]=="committed")expected["state"]="committed";
            require(*actual==expected);validate_decision();require(journal_->recover());observed_=read();
        }else {
            require(owned_completed_.has_value());const auto current=read();require(same(current,*owned_completed_));observed_=current;
        }
        owned_decision_.reset();owned_completed_.reset();held_=false;return true;
    }catch(...){held_=true;return false;}
}
} // namespace wsprrypi
