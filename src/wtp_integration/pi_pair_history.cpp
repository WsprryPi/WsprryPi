// SPDX-License-Identifier: MIT
#include <system_error>
#include "pi_pair_history.hpp"
#include "pi_pair_records.hpp"
#include "pi_pair_operations.hpp"
#include "pi_pair_peers.hpp"
#include "pi_pair_history_record.hpp"
#include "browser_api.hpp"
#include "owned_state_directory.hpp"
#include "local_browser_principal.hpp"
#include "management_browser_session.hpp"
#include "controller_contract.hpp"
#include "pi_pair_window.hpp"
#include "pi_pair_contract.hpp"
#include "pi_pair_rotation.hpp"
#include "pi_pair_usage.hpp"
#include "pi_pair_policy_admission.hpp"
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <array>
#include <charconv>
#include <cerrno>
#include <set>
#include <filesystem>
#include <fcntl.h>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw std::runtime_error("pairing_storage_unavailable");}
bool id(const Json &value){return value.is_string()&&value.get_ref<const std::string &>().size()==32&&
    value.get_ref<const std::string &>().find_first_not_of("0123456789abcdef")==std::string::npos;}
std::string normalized(const std::string &requested){
    const auto path=std::filesystem::path(requested),name=path.filename();require(!name.empty()&&name!="."&&name!="..");
    const auto parent=std::filesystem::canonical(path.has_parent_path()?path.parent_path():".");
    struct stat info{};require(!lstat(parent.c_str(),&info)&&S_ISDIR(info.st_mode)&&info.st_uid==geteuid()&&!(info.st_mode&0022));
    return (parent/name).string();
}
std::string fresh(){
    std::array<unsigned char,16> bytes{};require(RAND_bytes(bytes.data(),bytes.size())==1);
    std::string result;for(const auto byte:bytes){result+="0123456789abcdef"[byte>>4];result+="0123456789abcdef"[byte&15];}return result;
}
Json selection(const std::string &bytes,const std::string &self){
    return parse_pi_pair_history_record(bytes,self);
}
std::string namespace_path(const std::string &directory,const Json &record){return directory+".ns-"+record["namespace_id"].get<std::string>();}
Json empty_catalog(const std::string &self){return {{"schema","pi-management-pairs/1"},{"self_id",self},
    {"authorization_generation","0"},{"active",nullptr},{"retired",Json::array()}};}
void namespace_capacity(const std::string &directory){
    const auto path=std::filesystem::path(directory);const auto prefix=path.filename().string()+".ns-";unsigned count=0;
    for(const auto &entry:std::filesystem::directory_iterator(path.parent_path())){
        const auto name=entry.path().filename().string();if(!name.starts_with(prefix))continue;
        require(++count<32);
    }
}
}
struct PiPairHistory::Lock {
    int fd{-1};
    ~Lock(){if(fd>=0)::close(fd);}
};
bool PiPairHistory::reopen_service_history() noexcept{
    std::lock_guard guard(mutex_);
    if(!service_owner_.expired())return false;
    try{
        store_.reset();lock_=std::make_unique<Lock>();selected_.reset();
        running_marked_=false;unclean_start_=false;usage_=std::make_unique<PiPairUsage>(self_);
        open_directory();load_selection();return bool(store_);
    }catch(...){store_.reset();return false;}
}
PiPairHistory::PiPairHistory(std::string directory,std::string self,std::function<void(PiPairCatalogWritePoint)> checkpoint):lock_(std::make_unique<Lock>()){
    require(id(self));directory=normalized(directory);
    directory_=directory;self_=self;
    usage_=std::make_unique<PiPairUsage>(self_);
    struct stat existing{};
    if(lstat(directory_.c_str(),&existing)){require(errno==ENOENT);return;}
    open_directory();load_selection(std::move(checkpoint));
}
void PiPairHistory::open_directory(){
    require(lock_->fd<0);
    auto candidate=std::make_unique<Lock>();candidate->fd=open(directory_.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    require(candidate->fd>=0&&!flock(candidate->fd,LOCK_EX|LOCK_NB));struct stat opened{},named{};
    require(!fstat(candidate->fd,&opened)&&!lstat(directory_.c_str(),&named)&&S_ISDIR(named.st_mode)&&
        named.st_uid==geteuid()&&!(named.st_mode&0077)&&opened.st_dev==named.st_dev&&opened.st_ino==named.st_ino);
    lock_=std::move(candidate);
}
void PiPairHistory::load_selection(std::function<void(PiPairCatalogWritePoint)> checkpoint){
    // Invalid or lost selected state holds access. It is never replaced by an
    // empty catalog or an alternate sibling merely because opening failed.
    try{
        const auto observed=PiManagementJournal::read_private_record(directory_+"/authority.json");require(observed.has_value());
        const auto record=selection(observed->bytes,self_);
        store_=std::make_unique<PiPairCatalogStore>(namespace_path(directory_,record),self_,std::move(checkpoint),
            PiPairCatalogStore::Authority{directory_+"/authority.json",record["namespace_id"].get<std::string>()});
        selected_=store_->snapshot().authority;verify_selection();
        unclean_start_=selection(selected_->bytes,self_)["clean_shutdown"]==false;
    }catch(...){store_.reset();}
}
PiPairHistory::~PiPairHistory()=default;
std::unique_ptr<PiPairHistory> PiPairHistory::initialize(std::string directory,PiPairInitializationProof proof,
    const InitializationCheckpoint &checkpoint){
    require(proof.valid()&&id(proof.device_id())&&id(proof.installation_id()));directory=normalized(directory);
    const auto self=proof.device_id();
    (void)prepare_owned_state_directory(directory,[&]{
        namespace_capacity(directory);
        const auto empty=empty_catalog(self),operations=pi_pair_operations(self,Json::array()).document,peers=empty_pi_pair_peers(self);
        const auto record=pi_pair_history_record(self,fresh(),Json{{"kind","first"},{"operation_id",fresh()},
            {"installation_id",proof.installation_id()},{"source",proof.source()==PiPairInitializationProof::Source::TransmitterInstallation?
                "transmitter-installation":"controller-establishment"}},empty,operations,peers,true);
        const auto storage=prepare_owned_state_directory(namespace_path(directory,record),[&]{return empty.dump();},[&](const auto &bytes){
            require(parse_pi_pair_catalog(bytes,self)==empty);
        },[&](auto point){if(checkpoint)checkpoint(true,point);});
        require(!storage.existed);
        (void)PiManagementJournal::publish_private_record(namespace_path(directory,record)+"/operations.json",{},operations.dump());
        (void)PiManagementJournal::publish_private_record(namespace_path(directory,record)+"/peers.json",{},peers.dump());
        return record.dump();
    },[&](const auto &bytes){
        const auto record=selection(bytes,self);require(record["initialization"]["installation_id"]==proof.installation_id());
        const auto candidate=PiManagementJournal::read_private_record(namespace_path(directory,record)+"/authority.json");require(candidate.has_value());
        require(parse_pi_pair_catalog(candidate->bytes,self)==empty_catalog(self));
        const auto operations=PiManagementJournal::read_private_record(namespace_path(directory,record)+"/operations.json");require(operations.has_value());
        const auto peers=PiManagementJournal::read_private_record(namespace_path(directory,record)+"/peers.json");require(peers.has_value());
        require(parse_pi_pair_operations(operations->bytes,self)==pi_pair_operations(self,Json::array()).document);
        require(parse_pi_pair_peers(peers->bytes,self)==empty_pi_pair_peers(self));
        require(record["catalog_digest"]==PiManagementJournal::digest(candidate->bytes)&&record["operations_digest"]==PiManagementJournal::digest(operations->bytes)&&
            record["peers_digest"]==PiManagementJournal::digest(peers->bytes));
    },[&](auto point){if(checkpoint)checkpoint(false,point);});
    // Existing history is opened without reinitialization, even when it is
    // corrupt/unavailable. The positive event cannot become a reset capability.
    return std::make_unique<PiPairHistory>(directory,self);
}
void PiPairHistory::verify_selection() const {
    require(bool(store_)&&selected_.has_value());struct stat descriptor{},named{};
    require(!fstat(lock_->fd,&descriptor)&&!lstat(directory_.c_str(),&named)&&S_ISDIR(named.st_mode)&&
        named.st_uid==geteuid()&&!(named.st_mode&0077)&&descriptor.st_dev==named.st_dev&&descriptor.st_ino==named.st_ino);
    const auto current=PiManagementJournal::read_private_record(directory_+"/authority.json");
    require(current&&current->bytes==selected_->bytes&&current->signature==selected_->signature);
    const auto record=selection(current->bytes,self_);const auto &initial=record["initialization"];
    if(initial["kind"]=="access-recovery"&&!initial["previous_selector_digest"].is_null()){
        const auto evidence=PiManagementJournal::read_private_record(namespace_path(directory_,record)+"/recovery-selector.json");
        require(evidence&&initial["previous_selector_digest"]==PiManagementJournal::digest(evidence->bytes));
    }
}
bool PiPairHistory::available() noexcept {
    try{(void)snapshot();return true;}catch(...){return false;}
}
std::optional<std::string> PiPairHistory::recovery_generation() noexcept {
    std::lock_guard guard(mutex_);
    try{
        struct stat named{},descriptor{};
        if(lock_->fd<0){require(lstat(directory_.c_str(),&named)&&errno==ENOENT);return "0";}
        require(!fstat(lock_->fd,&descriptor)&&!lstat(directory_.c_str(),&named)&&S_ISDIR(named.st_mode)&&
            named.st_uid==geteuid()&&!(named.st_mode&0077)&&descriptor.st_dev==named.st_dev&&descriptor.st_ino==named.st_ino);
        const auto observed=PiManagementJournal::read_private_record(directory_+"/authority.json");
        if(!observed)return "0";
        try{return selection(observed->bytes,self_)["authorization_generation"].get<std::string>();}
        catch(...){return "0";}
    }catch(...){return {};}
}
PiPairCatalogStore::Snapshot PiPairHistory::snapshot(){
    std::lock_guard guard(mutex_);
    try{verify_selection();return store_->snapshot();}catch(...){store_.reset();throw;}
}
void PiPairHistory::with_backup_files(const std::function<void(const std::map<std::string,std::string> &)> &capture){
    std::unique_lock guard(mutex_,std::try_to_lock);
    if(!guard.owns_lock())throw std::system_error(std::make_error_code(std::errc::device_or_resource_busy));
    require(bool(capture));
    if(!store_){
        struct stat info{};require(::lstat(directory_.c_str(),&info)&&errno==ENOENT);
        capture({});return;
    }
    verify_selection();const auto before=store_->snapshot();
    require(before.authority&&before.peers);
    const auto record=selection(before.authority->bytes,self_);
    const auto selected=namespace_path(directory_,record);
    capture({{"pairing/selector.json",directory_+"/authority.json"},
        {"pairing/authority.json",selected+"/authority.json"},{"pairing/operations.json",selected+"/operations.json"},
        {"pairing/peers.json",selected+"/peers.json"}});
    verify_selection();const auto after=store_->snapshot();
    require(after.catalog.signature==before.catalog.signature&&after.operations.signature==before.operations.signature&&
        after.authority->signature==before.authority->signature&&after.peers->signature==before.peers->signature);
}
void PiPairHistory::publish(const PiPairCatalogStore::Snapshot &expected,const Json &value){
    std::lock_guard guard(mutex_);
    try{verify_selection();}catch(...){store_.reset();throw;}
    require(expected.peers.has_value());auto catalog=value,peers=parse_pi_pair_peers(expected.peers->bytes,self_);prepare_usage(expected,catalog,peers);
    store_->publish(expected,catalog,parse_pi_pair_operations(expected.operations.bytes,self_),peers);
    selected_=store_->snapshot().authority;
    try{verify_selection();reconcile_usage(store_->snapshot());}catch(...){store_.reset();throw;}
}
void PiPairHistory::publish(const PiPairCatalogStore::Snapshot &expected,const Json &value,const Json &operations){
    std::lock_guard guard(mutex_);
    try{verify_selection();}catch(...){store_.reset();throw;}
    require(expected.peers.has_value());auto catalog=value,peers=parse_pi_pair_peers(expected.peers->bytes,self_);prepare_usage(expected,catalog,peers);
    store_->publish(expected,catalog,operations,peers);
    selected_=store_->snapshot().authority;
    try{verify_selection();reconcile_usage(store_->snapshot());}catch(...){store_.reset();throw;}
}
void PiPairHistory::publish(const PiPairCatalogStore::Snapshot &expected,const Json &value,const Json &operations,const Json &peers){
    std::lock_guard guard(mutex_);
    try{verify_selection();}catch(...){store_.reset();throw;}
    auto catalog=value,outgoing=peers;prepare_usage(expected,catalog,outgoing);
    store_->publish(expected,catalog,operations,outgoing);selected_=store_->snapshot().authority;
    try{verify_selection();reconcile_usage(store_->snapshot());}catch(...){store_.reset();throw;}
}
void PiPairHistory::recover_access(const BrowserRequest &request,const LocalBrowserPrincipal &principal,
    ManagementBrowserSessions &sessions,const std::function<void(PiPairCatalogWritePoint)> &checkpoint){
    require(principal.matches(request)&&sessions.owns(principal)&&request.method=="POST"&&request.path=="/api/control/v1/host/pairing/window"&&request.body.size()<=4096);
    const auto intent=strict_browser_json(request.body,8);
    require(valid_controller_document(intent,"pair-window-request")&&intent["action"]=="recover");
    const auto digest=PiManagementJournal::digest(canonical_pi_management_json(intent));
    PiPairPolicyAdmission policy;require(policy.allowed());
    std::unique_lock guard(mutex_,std::try_to_lock);require(guard.owns_lock()&&service_permits()&&principal.matches(request));
    // Do not repair unsafe files, follow symlinks or replace a live competing
    // owner. A missing root is represented as a hold until this explicit action.
    std::optional<PiManagementJournal::Observation> before;
    std::optional<Json> old;
    if(lock_->fd>=0){
        struct stat descriptor{},named{};
        require(!fstat(lock_->fd,&descriptor)&&!lstat(directory_.c_str(),&named)&&S_ISDIR(named.st_mode)&&
            named.st_uid==geteuid()&&!(named.st_mode&0077)&&descriptor.st_dev==named.st_dev&&descriptor.st_ino==named.st_ino);
        before=PiManagementJournal::read_private_record(directory_+"/authority.json");
        if(before)try{old=selection(before->bytes,self_);}catch(...){/* Explicit recovery retains these bytes, never follows them. */}
    }else {
        struct stat named{};require(lstat(directory_.c_str(),&named)&&errno==ENOENT);
    }
    if(old&&(*old)["initialization"]["kind"]=="access-recovery"){
        const auto &initial=(*old)["initialization"];
        if(initial["operation_id"]==intent["operation_id"]){
            require(initial["principal_id"]==principal.session_scope()&&initial["request_digest"]==digest);
            verify_selection();(void)store_->snapshot();return;
        }
    }
    bool sound=false;
    try{verify_selection();(void)store_->snapshot();sound=true;}catch(...){store_.reset();}
    require(!sound);
    const auto expected=old?(*old)["authorization_generation"].get<std::string>():"0";
    require(intent["expected_authorization_generation"]==expected);
    std::uint64_t generation{};const auto parsed=std::from_chars(expected.data(),expected.data()+expected.size(),generation);
    require(parsed.ec==std::errc{}&&parsed.ptr==expected.data()+expected.size()&&generation<UINT64_MAX);
    // With verifiable history advance its counter. Entirely irrecoverable
    // counters use zero only in the new empty namespace; first pair commits 1.
    if(old)++generation;
    namespace_capacity(directory_);
    auto catalog=empty_catalog(self_);catalog["authorization_generation"]=std::to_string(generation);
    const Json initial{{"kind","access-recovery"},{"operation_id",intent["operation_id"]},{"principal_id",principal.session_scope()},
        {"request_digest",digest},{"previous_namespace",old?(*old)["namespace_id"]:Json(nullptr)},
        {"previous_selector_digest",before?Json(PiManagementJournal::digest(before->bytes)):Json(nullptr)}};
    const Json entry{{"scope","local"},{"principal_id",principal.session_scope()},{"target_id",self_},{"pair_id",nullptr},
        {"authorization_generation",std::to_string(generation)},{"operation_id",intent["operation_id"]},{"request_digest",digest},
        {"state","committed"},{"result_digest",PiManagementJournal::digest(canonical_pi_management_json(initial))},
        {"unresolved",false},{"receipt",nullptr},{"continuation",nullptr}};
    const auto operations=pi_pair_operations(self_,Json::array({entry})).document;
    const auto peers=empty_pi_pair_peers(self_);
    const auto record=pi_pair_history_record(self_,fresh(),initial,catalog,operations,peers,!running_marked_);
    const auto ns=namespace_path(directory_,record);
    const auto created=prepare_owned_state_directory(ns,[&]{return catalog.dump();},[&](const auto &bytes){
        require(parse_pi_pair_catalog(bytes,self_)==catalog);
    });require(!created.existed);
    (void)PiManagementJournal::publish_private_record(ns+"/operations.json",{},operations.dump());
    (void)PiManagementJournal::publish_private_record(ns+"/peers.json",{},peers.dump());
    if(before)(void)PiManagementJournal::publish_private_record(ns+"/recovery-selector.json",{},before->bytes);
    if(checkpoint)checkpoint(PiPairCatalogWritePoint::Prepared);
    require(principal.matches(request));
    try{
        if(lock_->fd>=0){
            selected_=PiManagementJournal::publish_private_record(directory_+"/authority.json",before,record.dump());
        }else {
            const auto root=prepare_owned_state_directory(directory_,[&]{return record.dump();},[&](const auto &bytes){require(bytes==record.dump());});
            require(!root.existed);open_directory();
        }
        sessions.retire();
        if(checkpoint)checkpoint(PiPairCatalogWritePoint::Committed);
        load_selection();require(bool(store_));unclean_start_=false;
        usage_=std::make_unique<PiPairUsage>(self_);if(running_marked_)usage_->begin(catalog,peers,false);
        if(checkpoint)checkpoint(PiPairCatalogWritePoint::Published);
    }catch(...){sessions.retire();store_.reset();throw;}
}
bool PiPairHistory::service_permits() const noexcept{
    if(!service_bound_)return true;
    const auto closed=service_closed_.lock();
    return closed&&!closed->load();
}
std::shared_ptr<const PiPairTlsCredential> PiPairHistory::management_credential(const PiPairTlsIdentity &identity){
    std::lock_guard guard(mutex_);
    try{
        if(!service_permits()||identity.purpose!=PiPairTlsPurpose::Management)return {};
        (void)pi_pair_tls_identity(identity);verify_selection();const auto before=store_->snapshot();
        const auto catalog=parse_pi_pair_catalog(before.catalog.bytes,self_);const auto &active=catalog["active"];
        if(active.is_null()||active["state"]!="active"||active["pair_id"]!=identity.pair_id||
            active["authorization_generation"]!=identity.authorization_generation)return {};
        const Json *slot=nullptr;
        if(active["current"]["epoch"]==identity.epoch)slot=&active["current"];
        else if(!active["previous"].is_null()&&active["previous"]["epoch"]==identity.epoch)slot=&active["previous"];
        if(!slot)return {};
        return std::make_shared<PiPairTlsCredential>(PiPairTlsCredential{identity,self_,active["peer_id"].get<std::string>(),
            PiPairSecret::from_storage((*slot)["tls_psk"].get_ref<const std::string &>())});
    }catch(...){return {};}
}
PiPairCommitResult PiPairHistory::commit_pair(const PiPairCommitApproval &approval){
    using Reason=PiPairCommitRefusal::Reason;
    const auto admit=[](bool value,Reason reason){if(!value)throw PiPairCommitRefusal(reason);};
    PiPairPolicyAdmission policy;admit(policy.allowed(),Reason::Unavailable);
    std::unique_lock guard(mutex_,std::try_to_lock);admit(guard.owns_lock()&&service_permits(),Reason::Unavailable);
    admit(approval.valid(),Reason::Expired);verify_selection();const auto before=store_->snapshot();
    auto catalog=parse_pi_pair_catalog(before.catalog.bytes,self_),operations=parse_pi_pair_operations(before.operations.bytes,self_);
    const auto &hello=approval.hello();const auto pair=hello["pair_id"].get<std::string>(),peer=hello["controller_id"].get<std::string>();
    admit(hello["target_id"]==self_&&peer!=self_,Reason::Proof);
    const auto retained=[&](const Json &book)->std::optional<PiPairCommitResult>{
        for(const auto &entry:book["entries"])if(entry["operation_id"]==pair&&entry["scope"]=="paired"&&entry["principal_id"]==peer&&
            entry["target_id"]==self_&&entry["pair_id"]==pair&&entry["authorization_generation"]==catalog["authorization_generation"]&&
            entry["request_digest"]==approval.request_digest()&&entry["state"]=="committed")
            return PiPairCommitResult{entry["authorization_generation"].get<std::string>(),entry["result_digest"].get<std::string>()};
        return {};
    };
    if(!catalog["active"].is_null()&&catalog["active"]["pair_id"]==pair&&catalog["active"]["peer_id"]==peer){
        const auto key=PiPairSecret::from_storage(catalog["active"]["retirement_verifier"].get<std::string>());
        admit(CRYPTO_memcmp(key.bytes().data(),approval.retirement_key().bytes().data(),32)==0,Reason::Proof);
        if(const auto replay=retained(operations))return *replay;
        throw PiPairCommitRefusal(Reason::Proof);
    }
    admit(catalog["authorization_generation"]==hello["authorization_generation"],Reason::Generation);
    const auto expected=catalog["authorization_generation"].get<std::string>();std::uint64_t number{};
    const auto parsed=std::from_chars(expected.data(),expected.data()+expected.size(),number);
    admit(parsed.ec==std::errc{}&&parsed.ptr==expected.data()+expected.size()&&number<UINT64_MAX,Reason::Capacity);
    const bool replace=hello["action"]=="replace";
    admit(replace?!catalog["active"].is_null():catalog["active"].is_null(),Reason::Generation);
    admit(catalog["retired"].size()<(replace?31U:32U),Reason::Capacity);
    if(hello["action"]=="recover")admit(selection(before.authority->bytes,self_)["initialization"]["kind"]=="access-recovery",Reason::Proof);
    if(replace){
        admit(catalog["retired"].size()<32&&catalog["active"]["state"]=="active",Reason::Capacity);
        auto revoked=catalog["active"];const auto old_pair=revoked["pair_id"];
        revoked["state"]="revoked";revoked["current"]=nullptr;revoked["pending"]=nullptr;revoked["previous"]=nullptr;
        catalog["retired"].push_back(std::move(revoked));
        std::set<std::string> newly_completed;
        for(auto &entry:operations["entries"]){
            if(entry["scope"]!="paired"||entry["pair_id"]!=old_pair||entry["continuation"].is_null())continue;
            newly_completed.insert(entry["operation_id"].get<std::string>());
            // A coherent current+pending target rotation has not committed its
            // final epoch. A current+previous rotation is already committed.
            // Preserve completed history; reject only the unfinished rotation.
            entry["continuation"]=nullptr;entry["unresolved"]=false;
            if(entry["state"]!="committed"){
                entry["state"]="rejected";entry["result_digest"]=nullptr;
                if(!entry["receipt"].is_null()){
                    entry["receipt"]["state"]="rejected";entry["receipt"]["revision"]=nullptr;
                    entry["receipt"]["error"]={{"code","unauthorized"},{"retryable",false}};
                }
            }
        }
        Json ordered=Json::array(),completed=Json::array();
        for(const auto &entry:operations["entries"]){
            if(newly_completed.contains(entry["operation_id"].get<std::string>()))
                completed.push_back(entry);
            else ordered.push_back(entry);
        }
        for(const auto &entry:completed)ordered.push_back(entry);
        operations["entries"]=std::move(ordered);
    }
    // Pure projections can each retire at most one newly terminal record. Do
    // the old-rotation resolution and new pair result separately, then publish
    // their single complete after-image with session retirement if necessary.
    const auto resolved=pi_pair_operations(self_,operations["entries"]);
    auto entries=resolved.document["entries"];const auto generation=std::to_string(number+1);
    const Json outcome{{"contract","Wsprry-Pair/1"},{"schema","operation"},{"operation_id",pair},{"pair_id",pair},
        {"authorization_generation",generation},{"request_digest",approval.request_digest()},{"state","committed"}};
    const auto digest=PiManagementJournal::digest(canonical_pi_management_json(outcome));
    entries.push_back({{"scope","paired"},{"principal_id",peer},{"target_id",self_},{"pair_id",pair},{"authorization_generation",generation},
        {"operation_id",pair},{"request_digest",approval.request_digest()},{"state","committed"},{"result_digest",digest},
        {"unresolved",false},{"receipt",nullptr},{"continuation",nullptr}});
    const auto bounded=pi_pair_operations(self_,entries);catalog["authorization_generation"]=generation;
    catalog["active"]={{"schema","pi-management-pair/1"},{"self_id",self_},{"peer_id",peer},{"pair_id",pair},{"authorization_generation",generation},
        {"role","manager"},{"state","active"},{"pending",nullptr},{"previous",nullptr},
        {"current",{{"epoch","1"},{"tls_psk",approval.tls_key().storage_encoding()},{"service_ms","0"},{"connections","0"}}},
        {"retirement_verifier",approval.retirement_key().storage_encoding()},
        {"receipt",{{"operation_id",pair},{"request_digest",approval.request_digest()},{"result_digest",digest}}}};
    if(resolved.evicted||bounded.evicted)approval.sessions_.retire();
    auto peers=parse_pi_pair_peers(before.peers->bytes,self_);prepare_usage(before,catalog,peers);
    const auto recover_owner=[&]{require(store_&&store_->recover_own_failure());selected_=store_->snapshot().authority;verify_selection();reconcile_usage(store_->snapshot());};
    try{
        store_->publish(before,catalog,bounded.document,peers,PiPairCatalogStore::Checkpoint::Preserve,
            [&]{admit(approval.valid(),Reason::Expired);});
        selected_=store_->snapshot().authority;verify_selection();reconcile_usage(store_->snapshot());return {generation,digest};
    }catch(const PiPairCommitRefusal &){
        recover_owner();
        const auto recovered=store_->snapshot();require(!retained(parse_pi_pair_operations(recovered.operations.bytes,self_)));
        throw;
    }catch(...){
        const auto failure=std::current_exception();recover_owner();const auto recovered=store_->snapshot();
        const auto complete=retained(parse_pi_pair_operations(recovered.operations.bytes,self_));
        const auto state=parse_pi_pair_catalog(recovered.catalog.bytes,self_);
        if(complete&&!state["active"].is_null()&&state["active"]["pair_id"]==pair&&state["active"]["peer_id"]==peer)return *complete;
        std::rethrow_exception(failure);
    }
}
std::optional<Json> PiPairHistory::pair_operation(const PiPairPrincipal &principal,const std::string &operation){
    std::lock_guard guard(mutex_);
    try{
        if(!id(operation)||principal.self_id()!=self_||principal.identity().purpose!=PiPairTlsPurpose::Management)return {};
        verify_selection();const auto before=store_->snapshot();const auto catalog=parse_pi_pair_catalog(before.catalog.bytes,self_);
        const auto &active=catalog["active"];const auto &identity=principal.identity();
        if(active.is_null()||active["state"]!="active"||active["peer_id"]!=principal.peer_id()||active["pair_id"]!=identity.pair_id||
            active["authorization_generation"]!=identity.authorization_generation)return {};
        const bool current=active["current"]["epoch"]==identity.epoch;
        if(!current&&(active["previous"].is_null()||active["previous"]["epoch"]!=identity.epoch||active["receipt"].is_null()||
            active["receipt"]["operation_id"]!=operation))return {};
        const auto book=parse_pi_pair_operations(before.operations.bytes,self_);
        for(const auto &entry:book["entries"]){
            if(entry["operation_id"]!=operation||entry["scope"]!="paired"||entry["principal_id"]!=principal.peer_id()||
                entry["pair_id"]!=identity.pair_id||entry["authorization_generation"]!=identity.authorization_generation)continue;
            if(!current&&(entry["continuation"].is_null()||entry["unresolved"]!=true))return {};
            Json result{{"contract","Wsprry-Pair/1"},{"schema","operation"},{"operation_id",operation},{"pair_id",identity.pair_id},
                {"authorization_generation",identity.authorization_generation},{"request_digest",entry["request_digest"]},
                {"state",entry["state"]},{"result_digest",entry["result_digest"]}};
            require(valid_pi_pair_document(result,"operation"));return result;
        }
    }catch(...){}
    return {};
}
std::optional<Json> PiPairHistory::rotate(const PiPairPrincipal &principal,const Json &request,ManagementBrowserSessions &sessions){
    const bool prepare=valid_pi_pair_document(request,"rotate-prepare");
    if(!prepare&&!valid_pi_pair_document(request,"rotate-commit"))return {};
    PiPairPolicyAdmission policy;if(!policy.allowed())return {};
    std::unique_lock guard(mutex_,std::try_to_lock);if(!guard.owns_lock()||!service_permits())return {};
    try{
        require(principal.self_id()==self_&&principal.identity().purpose==PiPairTlsPurpose::Management);
        verify_selection();const auto before=store_->snapshot();auto catalog=parse_pi_pair_catalog(before.catalog.bytes,self_);
        auto operations=parse_pi_pair_operations(before.operations.bytes,self_);auto &active=catalog["active"];const auto &identity=principal.identity();
        require(!active.is_null()&&active["state"]=="active"&&active["peer_id"]==principal.peer_id()&&active["pair_id"]==identity.pair_id&&
            active["authorization_generation"]==identity.authorization_generation&&request["pair_id"]==identity.pair_id);
        const bool current=active["current"]["epoch"]==identity.epoch;
        require(current||(!prepare&&!active["previous"].is_null()&&active["previous"]["epoch"]==identity.epoch&&
            !active["receipt"].is_null()&&active["receipt"]["operation_id"]==request["operation_id"]));
        Json *entry=nullptr;
        for(auto &row:operations["entries"])if(row["operation_id"]==request["operation_id"]){entry=&row;break;}
        if(entry)require((*entry)["scope"]=="paired"&&(*entry)["principal_id"]==principal.peer_id()&&
            (*entry)["pair_id"]==identity.pair_id&&(*entry)["authorization_generation"]==identity.authorization_generation);
        Json response;
        if(prepare){
            require(current&&request["expected_epoch"]==identity.epoch&&PiPairRotationKey::valid_public_point(request["pA"].get_ref<const std::string &>()));
            const auto digest=PiManagementJournal::digest(canonical_pi_management_json(request));
            if(entry){
                require((*entry)["request_digest"]==digest&&(*entry)["state"]=="pending"&&(*entry)["unresolved"]==true&&
                    !(*entry)["continuation"].is_null()&&(*entry)["continuation"]["prepare"]==request);
                return (*entry)["continuation"]["prepared"];
            }
            require(active["pending"].is_null()&&active["previous"].is_null());
            const auto &text=request["expected_epoch"].get_ref<const std::string &>();std::uint64_t epoch{};
            const auto parsed=std::from_chars(text.data(),text.data()+text.size(),epoch);
            require(parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()&&epoch&&epoch<UINT64_MAX);
            auto key=PiPairRotationKey::generate();std::array<unsigned char,32> nonce{};require(RAND_bytes(nonce.data(),nonce.size())==1);
            response={{"contract","Wsprry-Pair/1"},{"schema","rotate-prepared"},{"operation_id",request["operation_id"]},
                {"next_epoch",std::to_string(epoch+1)},{"pB",key.public_point()},{"nonce",PiPairSecret(nonce).storage_encoding()}};
            auto next=key.derive_next(PiPairSecret::from_storage(active["current"]["tls_psk"].get_ref<const std::string &>()),epoch,request,response,PiPairRotationKey::Role::Target);
            active["pending"]={{"epoch",response["next_epoch"]},{"tls_psk",next.storage_encoding()},{"service_ms","0"},{"connections","0"}};
            Json intent{{"kind","rotation-intent"},{"self_id",self_},{"peer_id",principal.peer_id()},{"profile_id",identity.pair_id},
                {"pair_id",identity.pair_id},{"authorization_generation",identity.authorization_generation},{"operation_id",request["operation_id"]},
                {"expected_epoch",request["expected_epoch"]},{"stage","awaiting_commit"},{"prepare",request},{"prepared",response},{"private_key",nullptr}};
            operations["entries"].push_back({{"scope","paired"},{"principal_id",principal.peer_id()},{"target_id",self_},{"pair_id",identity.pair_id},
                {"authorization_generation",identity.authorization_generation},{"operation_id",request["operation_id"]},{"request_digest",digest},
                {"state","pending"},{"result_digest",nullptr},{"unresolved",true},{"receipt",nullptr},{"continuation",std::move(intent)}});
        }else {
            require(entry);
            response={{"contract","Wsprry-Pair/1"},{"schema","rotate-committed"},{"operation_id",request["operation_id"]},
                {"pair_id",identity.pair_id},{"epoch",request["next_epoch"]}};
            const auto digest=PiManagementJournal::digest(canonical_pi_management_json(response));
            if((*entry)["state"]=="committed"){
                require((*entry)["result_digest"]==digest);
                if(!current)require((*entry)["unresolved"]==true&&!(*entry)["continuation"].is_null()&&
                    (*entry)["continuation"]["expected_epoch"]==request["expected_epoch"]);
                return response;
            }
            require(current&&(*entry)["state"]=="pending"&&(*entry)["unresolved"]==true&&!(*entry)["continuation"].is_null()&&
                (*entry)["continuation"]["expected_epoch"]==request["expected_epoch"]&&request["expected_epoch"]==identity.epoch&&
                (*entry)["continuation"]["prepared"]["next_epoch"]==request["next_epoch"]&&!active["pending"].is_null()&&
                active["pending"]["epoch"]==request["next_epoch"]&&active["previous"].is_null());
            active["previous"]=active["current"];active["current"]=active["pending"];active["pending"]=nullptr;
            (*entry)["state"]="committed";(*entry)["result_digest"]=digest;
            active["receipt"]={{"operation_id",request["operation_id"]},{"request_digest",(*entry)["request_digest"]},{"result_digest",digest}};
        }
        require(valid_pi_pair_document(response,prepare?"rotate-prepared":"rotate-committed"));
        const auto bounded=pi_pair_operations(self_,operations["entries"]);if(bounded.evicted)sessions.retire();
        auto peers=parse_pi_pair_peers(before.peers->bytes,self_);prepare_usage(before,catalog,peers);
        try{store_->publish(before,catalog,bounded.document,peers);}
        catch(...){
            require(store_->recover_own_failure());selected_=store_->snapshot().authority;verify_selection();reconcile_usage(store_->snapshot());
            const auto recovered=store_->snapshot();
            // A prepared rollback is safe but has not published this reply.
            // Only the exact committed after-image can be returned as success.
            if(parse_pi_pair_catalog(recovered.catalog.bytes,self_)!=catalog||parse_pi_pair_operations(recovered.operations.bytes,self_)!=bounded.document)return {};
        }
        selected_=store_->snapshot().authority;verify_selection();reconcile_usage(store_->snapshot());return response;
    }catch(...){return {};}
}
PiPairHistory::ManagementAccess PiPairHistory::accept_finished(const PiPairPrincipal &principal,ManagementBrowserSessions *sessions){
    std::lock_guard guard(mutex_);
    try{
        if(!service_permits()||principal.self_id()!=self_||principal.identity().purpose!=PiPairTlsPurpose::Management)return ManagementAccess::Denied;
        verify_selection();const auto before=store_->snapshot();auto catalog=parse_pi_pair_catalog(before.catalog.bytes,self_);
        auto operations=parse_pi_pair_operations(before.operations.bytes,self_);auto &active=catalog["active"];const auto &identity=principal.identity();
        if(active.is_null()||active["state"]!="active"||active["peer_id"]!=principal.peer_id()||active["pair_id"]!=identity.pair_id||
            active["authorization_generation"]!=identity.authorization_generation)return ManagementAccess::Denied;
        if(active["current"]["epoch"]!=identity.epoch)
            return !active["previous"].is_null()&&active["previous"]["epoch"]==identity.epoch?ManagementAccess::RotationRecovery:ManagementAccess::Denied;
        if(running_marked_){reconcile_usage(before);usage_->authenticated(identity);}
        if(active["previous"].is_null())return ManagementAccess::Current;
        // Catalog coherence already requires exactly this committed unresolved
        // target rotation. Erase its old TLS slot and continuation together;
        // a retained historical receipt grants no new mutation.
        bool found=false;
        for(auto &entry:operations["entries"]){
            if(entry["continuation"].is_null()||entry["scope"]!="paired")continue;
            require(!found&&entry["scope"]=="paired"&&entry["state"]=="committed"&&entry["unresolved"]==true&&
                entry["operation_id"]==active["receipt"]["operation_id"]&&entry["pair_id"]==identity.pair_id&&
                entry["authorization_generation"]==identity.authorization_generation);
            entry["continuation"]=nullptr;entry["unresolved"]=false;found=true;
        }
        require(found);active["previous"]=nullptr;
        // Terminal history can grow by one when the previously unresolved
        // rotation is acknowledged. Enforce the same bounded projection.
        Json ordered=Json::array();std::optional<Json> completed;
        for(const auto &entry:operations["entries"]){
            if(entry["scope"]=="paired"&&entry["operation_id"]==active["receipt"]["operation_id"]&&entry["pair_id"]==identity.pair_id)
                completed=entry;
            else ordered.push_back(entry);
        }
        require(completed.has_value());ordered.push_back(*completed);
        const auto bounded=pi_pair_operations(self_,ordered);
        // The production service supplies its actual local session owner when
        // a newly terminal rotation evicts a receipt. A primitive without that
        // owner must hold rather than silently weaken local replay protection.
        require(!bounded.evicted||sessions);
        if(bounded.evicted)sessions->retire();
        auto peers=parse_pi_pair_peers(before.peers->bytes,self_);prepare_usage(before,catalog,peers);
        try{store_->publish(before,catalog,bounded.document,peers);}
        catch(...){
            require(store_->recover_own_failure());selected_=store_->snapshot().authority;verify_selection();reconcile_usage(store_->snapshot());
            const auto recovered=store_->snapshot();const auto state=parse_pi_pair_catalog(recovered.catalog.bytes,self_);
            if(state["active"].is_null()||state["active"]["pair_id"]!=identity.pair_id||state["active"]["current"]["epoch"]!=identity.epoch||
                !state["active"]["previous"].is_null())return ManagementAccess::Denied;
        }
        selected_=store_->snapshot().authority;verify_selection();reconcile_usage(store_->snapshot());
        return ManagementAccess::Current;
    }catch(...){return ManagementAccess::Denied;}
}
void PiPairHistory::checkpoint(PiPairCatalogStore::Checkpoint kind){
    verify_selection();const auto before=store_->snapshot();
    auto catalog=parse_pi_pair_catalog(before.catalog.bytes,self_),peers=parse_pi_pair_peers(before.peers->bytes,self_);prepare_usage(before,catalog,peers);
    store_->publish(before,catalog,parse_pi_pair_operations(before.operations.bytes,self_),peers,kind);
    selected_=store_->snapshot().authority;verify_selection();reconcile_usage(store_->snapshot());
}
bool PiPairHistory::begin_service(){
    std::lock_guard guard(mutex_);if(running_marked_)return false;
    verify_selection();const auto before=store_->snapshot();usage_->begin(parse_pi_pair_catalog(before.catalog.bytes,self_),parse_pi_pair_peers(before.peers->bytes,self_),unclean_start_);
    try{checkpoint(PiPairCatalogStore::Checkpoint::Running);running_marked_=true;reconcile_usage(store_->snapshot());}
    catch(...){running_marked_=false;usage_->stop();throw;}
    return true;
}
void PiPairHistory::finish_service(){
    std::lock_guard guard(mutex_);if(!running_marked_)return;
    const bool forced=usage_->unclean_checkpoint_required();
    checkpoint(forced?PiPairCatalogStore::Checkpoint::Running:PiPairCatalogStore::Checkpoint::CleanShutdown);
    running_marked_=false;unclean_start_=forced;usage_->stop();
}
} // namespace wsprrypi
