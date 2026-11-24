// SPDX-License-Identifier: MIT
#include "managed_state_journal.hpp"
#include <openssl/rand.h>
#include <array>
#include <set>
#include <stdexcept>

namespace wsprrypi {
namespace {
void require(bool value){if(!value)throw std::runtime_error("pi_management_storage_hold");}
std::string signature(const std::string &kind,const std::string &bytes,const std::string &revision){
    return PiManagementJournal::digest("sqlite-record/1:"+kind+":"+revision+":"+PiManagementJournal::digest(bytes));
}
std::string fresh(){
    std::array<unsigned char,16> bytes{};require(RAND_bytes(bytes.data(),static_cast<int>(bytes.size()))==1);
    std::string result;for(const auto byte:bytes){result+="0123456789abcdef"[byte>>4];result+="0123456789abcdef"[byte&15];}return result;
}
}
ManagedStateJournal::ManagedStateJournal(std::string directory,std::string self,std::map<std::string,std::string> paths)
    :files_(std::make_unique<PiManagementJournal>(std::move(directory),std::move(self),std::move(paths))){}
ManagedStateJournal::ManagedStateJournal(std::shared_ptr<SqliteStateStore> database):database_(std::move(database)){
    require(database_&&database_->role()==StateRole::Pi);
}
StateRecord ManagedStateJournal::record_kind(const std::string &kind){
    // Existing owner-local names only; configuration bytes are local-settings/1
    // JSON in database mode, never a generated INI file or compatibility view.
    if(kind=="ini")return StateRecord::Configuration;
    if(kind=="settings")return StateRecord::ManagedSettings;
    if(kind=="operations")return StateRecord::Operations;
    if(kind=="authority")return StateRecord::Catalog;
    if(kind=="recurrence")return StateRecord::RecurrenceAuthority;
    if(kind=="progress")return StateRecord::RecurrenceInitialization;
    if(kind=="initialization")return StateRecord::Initialization;
    if(kind=="legacy")return StateRecord::RecurrenceRecovery;
    throw std::runtime_error("Unsupported managed database record");
}
std::optional<ManagedStateJournal::Observation> ManagedStateJournal::inspect(const std::string &kind) const{
    if(files_)return files_->inspect(kind);
    const auto saved=database_->record(record_kind(kind));
    if(!saved.bytes)return {};
    require(saved.record_revision.has_value());
    return Observation{*saved.bytes,signature(kind,*saved.bytes,*saved.record_revision)};
}
void ManagedStateJournal::prepare(const std::vector<Image> &images,const std::optional<nlohmann::json> &rollback){
    if(files_){files_->prepare(images,rollback);return;}
    require(!images.empty()&&images.size()<=7);
    if(rollback){require(decision_==rollback&&decision_->at("state")=="prepared");verify_prepared(*rollback);}
    else require(!decision_);
    const auto saved=database_->snapshot();
    std::set<StateRecord> unique;
    std::vector<StateChange> changes;
    auto rows=nlohmann::json::array();
    for(const auto &image:images){
        const auto kind=record_kind(image.kind);require(unique.insert(kind).second);
        require(image.bytes.size()<=(kind==StateRecord::Catalog?262144:SqliteStateStore::maximum_record_bytes));
        const auto found=saved.records.find(kind);
        const auto old=found==saved.records.end()?std::nullopt:std::optional<std::string>(PiManagementJournal::digest(found->second));
        require(image.expected_old_digest==old);
        if(image.expected_old_signature)require(found!=saved.records.end()&&*image.expected_old_signature==signature(image.kind,found->second,saved.record_revisions.at(kind)));
        changes.push_back({kind,image.bytes});
        rows.push_back({{"kind",image.kind},{"old_digest",old?nlohmann::json(*old):nlohmann::json(nullptr)},
            {"new_digest",PiManagementJournal::digest(image.bytes)},{"staged_name",fresh()}});
    }
    auto decision=nlohmann::json{{"schema","pi-management-transaction/1"},{"device_id",database_->identity()},
        {"transaction_id",fresh()},{"state","prepared"},{"records",rows}};
    before_=saved;changes_=std::move(changes);decision_=std::move(decision);
}
void ManagedStateJournal::commit(){
    if(files_){files_->commit();return;}
    require(decision_&&decision_->at("state")=="prepared"&&before_);
    database_->commit(before_->revision,changes_);
    (*decision_)["state"]="committed";
}
bool ManagedStateJournal::recover(bool retain_prepared){
    if(files_)return files_->recover(retain_prepared);
    if(!decision_)return false;
    if(decision_->at("state")=="prepared"){
        verify_prepared(*decision_);
        if(retain_prepared)return true;
    }else{
        require(decision_->at("state")=="committed");
        const auto saved=database_->snapshot();
        for(const auto &change:changes_)require(change.bytes&&saved.records.contains(change.kind)&&saved.records.at(change.kind)==*change.bytes);
    }
    before_.reset();changes_.clear();decision_.reset();return true;
}
std::optional<nlohmann::json> ManagedStateJournal::decision(){
    if(files_)return files_->decision();
    (void)database_->record(StateRecord::Configuration); // fence held/external state
    return decision_;
}
void ManagedStateJournal::verify_prepared(const nlohmann::json &expected){
    if(files_){files_->verify_prepared(expected);return;}
    require(decision_&&decision_==expected&&expected.at("state")=="prepared"&&before_&&database_->snapshot()==*before_);
}
void ManagedStateJournal::write_committed_ini(const std::string &bytes){
    if(files_){files_->write_committed_ini(bytes);return;}
    require(decision_&&decision_->at("state")=="committed");
    bool found=false;
    for(const auto &change:changes_)if(change.kind==StateRecord::Configuration){require(change.bytes==bytes);found=true;}
    require(found&&database_->record(StateRecord::Configuration).bytes==bytes);
}
} // namespace wsprrypi
