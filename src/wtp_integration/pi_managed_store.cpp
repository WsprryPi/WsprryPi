// SPDX-License-Identifier: MIT
#include "pi_managed_store.hpp"
#include "solar_configuration.hpp"
#include "pi_managed_records.hpp"
#include "controller_contract.hpp"
#include "config_handler_serialization.hpp"
#include "config_handler.hpp"
#include "wtp_endpoint/identity.hpp"
#include <charconv>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <set>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool condition){if(!condition)throw std::runtime_error("pi_management_storage_hold");}
std::string recovery_source_revision(const std::string &self,std::uint64_t epoch,const Json &managed,
    const std::optional<PiManagementJournal::Observation> &ini){
    return PiManagementJournal::digest(canonical_pi_management_json(Json{
        {"device_id",self},{"context_epoch",std::to_string(epoch)},{"managed_revision",managed},
        {"ini",ini?Json{{"signature",ini->signature},{"digest",PiManagementJournal::digest(ini->bytes)}}:Json(nullptr)}}));
}
std::string next(const Json &value){
    const auto text=value.get<std::string>();std::uint64_t number{};
    const auto result=std::from_chars(text.data(),text.data()+text.size(),number);
    require(result.ec==std::errc{}&&result.ptr==text.data()+text.size()&&number<std::numeric_limits<std::uint64_t>::max());
    return std::to_string(number+1);
}
Json migrated_scheduling(const Json &configuration){
    Json locator=nullptr;
    const auto saved=configuration.at("WSPR").at("Grid Square");
    // Shape validation below is the authority for a migrated locator; invalid
    // legacy text does not become a transmitter-site assertion.
    const Json ordinary{{"allow",true},{"wspr_override",nullptr},{"cw_base_hz",nullptr}};
    const Json gray{{"enabled",false},{"lower_mdeg",-6000},{"upper_mdeg",6000},{"wspr_override",nullptr},{"cw_base_hz",nullptr}};
    Json scheduling{{"enabled",true},{"solar",{{"enabled",false},{"location",{{"source","locator"},{"locator",locator}}},
        {"day",ordinary},{"night",ordinary},{"morning",gray},{"evening",gray}}}};
    auto candidate=Json{{"configuration",configuration},{"scheduling",scheduling}};
    candidate["scheduling"]["solar"]["location"]["locator"]=saved;
    if(valid_pi_candidate_shape(candidate))scheduling=candidate["scheduling"];
    return scheduling;
}
}
PiManagedStore::PiManagedStore(std::string ini,std::string self,PiManagementAnchor anchor)
    :ini_(std::move(ini)),self_(std::move(self)),directory_(ini_+".pi-management"),anchor_(std::move(anchor)),
     journal_(directory_,self_,{{"ini",ini_},{"settings",ini_+".managed-settings.json"},
        {"operations",ini_+".management-operations.json"},{"authority",ini_+".wtp-devices.json"}}){
    require(anchor_.complete());
}
void PiManagedStore::load(){
    initial_rollback_.reset();initial_observations_.clear();
    observations_.clear();
    for(const auto *kind:{"ini","settings","operations"}){
        const auto value=journal_.inspect(kind);require(value.has_value());observations_.emplace(kind,*value);
    }
    settings_=parse_pi_managed_settings_record(observations_.at("settings").bytes,self_,
        PiManagementJournal::digest(observations_.at("ini").bytes));
    validate_stored_solar(settings_["scheduling"]);
    operations_=parse_pi_operations_record(observations_.at("operations").bytes,self_);
    initialized_=true;
}
void PiManagedStore::recover(){
    std::lock_guard lock(mutex_);held_=true;initialized_=false;
    initial_rollback_.reset();initial_observations_.clear();
    // Validate/recover before reading any configuration or initializing history.
    const bool existed=std::filesystem::exists(std::filesystem::symlink_status(directory_));
    const auto decision=journal_.decision();
    bool initial_prepared=false;
    if(decision&&decision->at("state")=="prepared"){
        std::set<std::string> absent,kinds;
        for(const auto &row:decision->at("records")){
            kinds.insert(row.at("kind").get<std::string>());
            if(row.at("old_digest").is_null())absent.insert(row.at("kind").get<std::string>());
        }
        initial_prepared=kinds==std::set<std::string>{"ini","settings","operations","authority"}&&absent.contains("settings")&&absent.contains("operations");
    }
    journal_.recover(initial_prepared);
    // Before any owning parser follows the fixed path, reject existing FIFO,
    // symlink, wrong-owner, oversized, or unsafe-mode inputs. Absence is kept
    // distinct from previously initialized missing history below.
    (void)journal_.inspect("ini");
    const auto history=anchor_.read();
    if(!history){
        require(!existed||initial_prepared);
        require(!journal_.inspect("settings")&&!journal_.inspect("operations"));
        if(initial_prepared){
            initial_rollback_=journal_.decision();require(initial_rollback_==decision);
            for(const auto *kind:{"ini","settings","operations","authority"})initial_observations_[kind]=journal_.inspect(kind);
        }
        history_.clear();observations_.clear();held_=false;return;
    }
    require(std::filesystem::is_directory(std::filesystem::symlink_status(directory_)));
    history_=*history;load();held_=false;
}
void PiManagedStore::check_initial_rollback(){
    require(!held_&&!initialized_);
    try{
        require(journal_.decision()==initial_rollback_);
        if(initial_rollback_)journal_.verify_prepared(*initial_rollback_);
        for(const auto &[kind,expected]:initial_observations_){
            const auto current=journal_.inspect(kind);
            require(current.has_value()==expected.has_value()&&(!current||(current->signature==expected->signature&&current->bytes==expected->bytes)));
        }
    }catch(...){held_=true;throw;}
}
void PiManagedStore::check(){
    require(!held_&&initialized_);
    try{
        require(anchor_.read()==std::optional<std::string>(history_));
        require(std::filesystem::is_directory(std::filesystem::symlink_status(directory_))&&!journal_.decision());
        for(const auto &[kind,expected]:observations_){
            const auto observed=journal_.inspect(kind);
            require(observed&&observed->signature==expected.signature&&observed->bytes==expected.bytes);
        }
    }catch(...){held_=true;throw;}
}
void PiManagedStore::initialize(const Json &internal){
    std::lock_guard lock(mutex_);require(!held_);
    const auto configuration=config_handler_serialization::saved_configuration_from_internal_json(internal);
    if(initialized_){check();require(configuration==settings_["configuration"]);return;}
    const auto ini=journal_.inspect("ini");require(ini.has_value());
    initialize_transaction(internal,ini->bytes,[]{});
}
void PiManagedStore::initialize_persisted(const Json &internal,const std::function<void()> &writer){
    std::lock_guard lock(mutex_);require(bool(writer));
    initialize_transaction(internal,serialize_config_ini_projection_for_management(internal),writer);
}
void PiManagedStore::initialize_stock(const Json &internal,const std::string &ini){
    std::lock_guard lock(mutex_);initialize_transaction(internal,ini,[]{});
}
void PiManagedStore::initialize_transaction(const Json &internal,const std::string &bytes,const std::function<void()> &writer,
    const std::optional<Operation> &operation,const std::optional<std::string> &source_revision){
    require(!held_&&!initialized_);
    check_initial_rollback();
    const auto configuration=config_handler_serialization::saved_configuration_from_internal_json(internal);
    require(!anchor_.read()&&!journal_.inspect("settings")&&!journal_.inspect("operations"));
    const auto ini=journal_.inspect("ini");
    if(source_revision&&*source_revision!=recovery_source_revision(self_,context_epoch_,nullptr,ini))
        throw std::runtime_error("revision_conflict");
    observations_.clear();if(ini)observations_["ini"]=*ini;
    WtpPiBootIdentity random;history_=random.new_boot_id();
    publish(Json{{"configuration",configuration},{"scheduling",migrated_scheduling(configuration)}},bytes,writer,operation);
}
bool PiManagedStore::initialized() const {std::lock_guard lock(mutex_);return initialized_;}
void PiManagedStore::write_committed_ini(const std::string &bytes){
    std::lock_guard lock(mutex_);require(!held_);journal_.write_committed_ini(bytes);
}
const std::string &PiManagedStore::device_id() const noexcept {return self_;}
std::uint64_t PiManagedStore::context_epoch() const {std::lock_guard lock(mutex_);return context_epoch_;}
Json PiManagedStore::settings(){std::lock_guard lock(mutex_);check();return settings_;}
void PiManagedStore::validate_snapshot(const Json &internal){
    std::lock_guard lock(mutex_);check();
    require(config_handler_serialization::saved_configuration_from_internal_json(internal)==settings_["configuration"]);
}
std::optional<Json> PiManagedStore::receipt(const std::string &principal,const std::string &authorization,const std::string &operation){
    std::lock_guard lock(mutex_);check();
    for(const auto &entry:operations_["receipts"])
        if(entry["receipt"]["operation_id"]==operation&&entry["principal_id"]==principal&&entry["authorization_generation"]==authorization)return entry["receipt"];
    return {};
}
std::optional<Json> PiManagedStore::replay(const Operation &operation){
    std::lock_guard lock(mutex_);check();
    const auto digest=pi_managed_request_digest(self_,operation.principal,operation.authorization_generation,operation.route,operation.request);
    for(const auto &entry:operations_["receipts"])if(entry["receipt"]["operation_id"]==operation.request["operation_id"]){
        if(entry["principal_id"]!=operation.principal||entry["authorization_generation"]!=operation.authorization_generation||entry["route"]!=operation.route||entry["request_digest"]!=digest)
            throw std::runtime_error("request_id_reuse");
        return entry["receipt"];
    }
    return {};
}
void PiManagedStore::persist(const Json &internal,const std::function<void()> &writer,const std::optional<Operation> &operation){
    std::lock_guard lock(mutex_);check();require(bool(writer));
    if(operation)require(valid_controller_document(operation->request,"target-save"));
    const Json candidate{{"configuration",config_handler_serialization::saved_configuration_from_internal_json(internal)},
        {"scheduling",operation?operation->request.at("candidate").at("scheduling"):settings_["scheduling"]}};
    if(operation){
        require(!replay(*operation));
        if(operation->request["expected_revision"]!=settings_["revision"])throw std::runtime_error("revision_conflict");
        require(canonical_pi_management_json(candidate)==canonical_pi_management_json(operation->request.at("candidate")));
    }
    publish(candidate,serialize_config_ini_projection_for_management(internal),writer,operation);
}
void PiManagedStore::persist_stock(const Json &internal,const std::string &ini){
    std::lock_guard lock(mutex_);check();
    const Json candidate{{"configuration",config_handler_serialization::saved_configuration_from_internal_json(internal)},
        {"scheduling",settings_["scheduling"]}};
    publish(candidate,ini,[]{},{});
}
void PiManagedStore::persist_stop(const Json &internal,const std::function<void()> &writer,const Operation &operation){
    std::lock_guard lock(mutex_);check();require(bool(writer));
    require(valid_controller_document(operation.request,"target-stop")&&
        operation.route=="/api/control/v1/targets/"+self_+"/stop"&&!replay(operation));
    if(operation.request["expected_revision"]!=settings_["revision"])throw std::runtime_error("revision_conflict");
    auto expected=settings_["configuration"];expected["Operation"]["Transmit"]=false;
    const auto configuration=config_handler_serialization::saved_configuration_from_internal_json(internal);
    require(configuration==expected);
    publish(Json{{"configuration",configuration},{"scheduling",settings_["scheduling"]}},
        serialize_config_ini_projection_for_management(internal),writer,operation);
}
void PiManagedStore::persist_private(const Json &internal,const std::string &before,const std::string &after){
    std::lock_guard lock(mutex_);check();
    require(before==observations_.at("ini").bytes&&after.size()<=1048576&&
        config_handler_serialization::saved_configuration_from_internal_json(internal)==settings_["configuration"]);
    if(before==after)return;
    publish(Json{{"configuration",settings_["configuration"]},{"scheduling",settings_["scheduling"]}},after,[]{},{});
}
std::string PiManagedStore::recovery_revision(){
    std::lock_guard lock(mutex_);require(!held_);
    if(initialized_)check();
    else {check_initial_rollback();require(!anchor_.read()&&!journal_.inspect("settings")&&!journal_.inspect("operations"));}
    const auto ini=journal_.inspect("ini");
    return recovery_source_revision(self_,context_epoch_,initialized_?settings_["revision"]:Json(nullptr),ini);
}
void PiManagedStore::persist_recovery(const Json &internal,const std::function<void()> &writer,
    const Operation &operation,const std::optional<std::string> &stock_bytes){
    std::lock_guard lock(mutex_);require(bool(writer));
    const auto &request=operation.request;
    const bool save=valid_controller_document(request,"configuration-recovery-save");
    const bool action=valid_controller_document(request,"configuration-recovery-action");
    require((save||action)&&operation.route=="/api/control/v1/host/configuration-recovery"&&(!action||stock_bytes.has_value()));
    // Validate the complete principal, route, request, and device binding before
    // looking up a receipt or reserving any storage authority.
    (void)pi_managed_request_digest(self_,operation.principal,operation.authorization_generation,operation.route,request);
    if(initialized_)require(!replay(operation));
    if(request["expected_revision"]!=recovery_revision())throw std::runtime_error("revision_conflict");
    const auto configuration=config_handler_serialization::saved_configuration_from_internal_json(internal);
    if(save){
        auto public_candidate=configuration;
        for(const auto *key:{"TLS CA File","TLS Client Certificate","TLS Client Key"})public_candidate["WTP"][key]="";
        require(canonical_pi_management_json(public_candidate)==canonical_pi_management_json(request["configuration"]));
    }
    const auto ini=stock_bytes?*stock_bytes:serialize_config_ini_projection_for_management(internal);
    if(!initialized_){initialize_transaction(internal,ini,writer,operation,request["expected_revision"].get<std::string>());return;}
    publish(Json{{"configuration",configuration},{"scheduling",settings_["scheduling"]}},ini,writer,operation);
}
void PiManagedStore::publish(const Json &candidate,const std::string &ini,const std::function<void()> &writer,const std::optional<Operation> &operation){
    require(valid_pi_candidate_shape(candidate));
    validate_stored_solar(candidate.at("scheduling"));
    if(initialized_)require(candidate.at("scheduling").at("enabled")==settings_.at("scheduling").at("enabled"));
    const auto saved=pi_managed_settings_record(self_,initialized_?next(settings_["configuration_generation"]):"1",candidate,PiManagementJournal::digest(ini));
    auto receipts=initialized_?operations_["receipts"]:Json::array();
    if(operation){
        const auto digest=pi_managed_request_digest(self_,operation->principal,operation->authorization_generation,operation->route,operation->request);
        const Json receipt{{"contract","Wsprry-Control/1"},{"schema","target-operation"},{"binding",operation->request.at("binding")},
            {"operation_id",operation->request.at("operation_id")},{"request_digest",digest},{"state","committed"},{"revision",saved["revision"]},{"error",nullptr}};
        receipts.push_back({{"principal_id",operation->principal},{"route",operation->route},{"authorization_generation",operation->authorization_generation},
            {"request_digest",digest},{"receipt",receipt}});
    }
    const auto retained=pi_operations_record(self_,receipts);
    require(!retained.evicted||context_epoch_<std::numeric_limits<std::uint64_t>::max());
    const auto operations=retained.document;
    // All pure validation precedes the prepared decision and any final writes.
    std::map<std::string,std::optional<PiManagementJournal::Observation>> before_files;
    for(const auto *kind:{"ini","settings","operations"}){
        const auto observed=journal_.inspect(kind);before_files.emplace(kind,observed);
        const auto known=observations_.find(kind);
        require(known==observations_.end()?!observed:(observed&&observed->signature==known->second.signature));
    }
    const auto expected=[&](const std::string &kind)->std::optional<std::string>{
        const auto &observed=before_files.at(kind);return observed?std::optional<std::string>(PiManagementJournal::digest(observed->bytes)):std::nullopt;
    };
    try{
        anchor_.coordinate(history_,[&](const auto &before,const auto &after){
            for(const auto &[kind,expected]:before_files){
                const auto current=journal_.inspect(kind);
                require(current.has_value()==expected.has_value()&&(!current||current->signature==expected->signature));
            }
            journal_.prepare({{"ini",expected("ini"),ini},{"settings",expected("settings"),saved.dump()},
                {"operations",expected("operations"),operations.dump()},{"authority",PiManagementJournal::digest(before),after}},initial_rollback_);
            journal_.commit();writer();journal_.recover();
        });
        load();require(settings_==saved&&operations_==operations);
        if(retained.evicted)++context_epoch_;
    }catch(...){held_=true;throw;}
}
} // namespace wsprrypi
