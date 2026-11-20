#include "wtp_retirement.hpp"
// SPDX-License-Identifier: MIT
#include "pi_managed_store.hpp"
#include "application_storage/persistent_configuration.hpp"
#include "application_storage/application_state.hpp"
#include "application_recovery/restore_hold.hpp"
#include "application_control/advanced_settings.hpp"
#include "application_storage/ini_migration.hpp"
#include "catalog.hpp"
#include "privileged_network_admin.hpp"
#include "pi_pair_contract.hpp"
#include "pi_pair_history.hpp"
#include "pi_pair_policy_admission.hpp"
#include "pi_pair_records.hpp"
#include "pi_pair_peers.hpp"
#include "pi_pair_operations.hpp"
#include "pi_pair_tls.hpp"
#include "pi_pair_retirement.hpp"
#include "pi_pair_mutation_guard.hpp"
#include "solar_configuration.hpp"
#include "pi_managed_records.hpp"
#include "recurrence_authority.hpp"
#include "controller_contract.hpp"
#include "browser_api.hpp"
#include "config_handler_serialization.hpp"
#include "config_handler.hpp"
#include "wtp_endpoint/identity.hpp"
#include <charconv>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <set>
#include <algorithm>
#include <sys/stat.h>
#include <cerrno>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool condition){if(!condition)throw std::runtime_error("pi_management_storage_hold");}
void validate_resume_integrity(const Json &authority){
    if(!authority.contains("management"))return;
    const auto validate=[](const Json &value){
        if(value.is_null())return;
        require(valid_pi_pair_document(value["grant"],"resume-grant")&&
            valid_controller_document(value["disabled_snapshot"]["receipt"],"target-operation")&&
            value["grant"]["disable_receipt_digest"]==PiManagementJournal::digest(canonical_pi_management_json(value["disabled_snapshot"]["receipt"])));
    };
    const auto &managed=authority["management"];
    if(managed.contains("resume"))validate(managed["resume"]);
    if(managed.contains("handover"))validate(managed["handover"]["resume_basis"]);
}
bool pending_remote_resume(const Json &authority){
    return authority.contains("management")&&authority["management"].contains("resume")&&authority["management"]["resume"]["state"]=="pending";
}
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
std::string private_digest(const std::string &ini,const Json &recurrence,const Json &legacy,const Json &progress){
    // Bind authority to the existing settings revision without adding a second
    // checksum field or confusing the catalog history image with recurrence.
    auto authority=recurrence;
    // This redundant readback field equals the resulting settings revision;
    // normalize it to avoid a circular hash and verify equality on every load.
    if(authority["state"]=="staged")authority["staged_revision"]=nullptr;
    Json binding{
        {"ini_digest",PiManagementJournal::digest(ini)},
        {"recurrence_digest",PiManagementJournal::digest(authority.dump())},
        {"legacy_digest",legacy.is_null()?Json(nullptr):Json(PiManagementJournal::digest(legacy.dump()))}};
    if(!progress.is_null())binding["progress_initialization_digest"]=PiManagementJournal::digest(progress.dump());
    return PiManagementJournal::digest(canonical_pi_management_json(binding));
}
Json parse_progress_initialization(const std::string &bytes,const std::string &self){
    require(bytes.size()<=512);
    const auto value=strict_browser_json(bytes,4);
    require(value.is_object()&&value.size()==3&&value.value("schema",Json(nullptr))=="pi-recurrence-progress-initialization/1"&&
        value.value("device_id",Json(nullptr))==self&&value.contains("initialization_id")&&value["initialization_id"].is_string());
    const auto &id=value["initialization_id"].get_ref<const std::string &>();require(id.size()==32&&id.find_first_not_of("0123456789abcdef")==std::string::npos);return value;
}
std::string encode_bytes(const std::string &bytes){
    std::string encoded;encoded.reserve(bytes.size()*2);
    for(const unsigned char byte:bytes){encoded+="0123456789abcdef"[byte>>4];encoded+="0123456789abcdef"[byte&15];}
    return encoded;
}
std::string decode_bytes(const std::string &bytes){
    require(bytes.size()%2==0&&bytes.find_first_not_of("0123456789abcdef")==std::string::npos);
    const auto digit=[](char value){return value<='9'?value-'0':value-'a'+10;};
    std::string decoded;decoded.reserve(bytes.size()/2);
    for(std::size_t i=0;i<bytes.size();i+=2)decoded+=static_cast<char>((digit(bytes[i])<<4)|digit(bytes[i+1]));
    return decoded;
}
Json parse_legacy(const std::string &bytes,const std::string &self){
    std::vector<std::set<std::string>> objects;
    auto value=Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json &item){
        require(depth<=16);
        if(event==Json::parse_event_t::object_start)objects.emplace_back();
        if(event==Json::parse_event_t::key)require(objects.back().insert(item.get<std::string>()).second);
        if(event==Json::parse_event_t::object_end)objects.pop_back();
        return true;
    });
    require(value.is_object()&&value.size()==5&&value.value("schema",Json(nullptr))=="pi-legacy-recurrence/1"&&
        value.value("device_id",Json(nullptr))==self&&value.contains("assignment_id")&&value["assignment_id"].is_string()&&
        value["assignment_id"].get_ref<const std::string&>().size()==32&&value["assignment_id"].get_ref<const std::string&>().find_first_not_of("0123456789abcdef")==std::string::npos&&
        value.contains("candidate")&&valid_pi_stored_candidate_shape(value["candidate"])&&value.contains("ini_hex")&&value["ini_hex"].is_string());
    (void)decode_bytes(value["ini_hex"].get<std::string>());validate_stored_solar(value["candidate"]["scheduling"]);return value;
}
std::string installation_binding(const std::string &self,const std::string &installation,
    const std::optional<PiManagementJournal::Observation> &ini){
    return PiManagementJournal::digest(canonical_pi_management_json(Json{
        {"domain","installation-initial-input/1"},{"device_id",self},{"installation_id",installation},
        {"ini",ini?Json(ini->bytes):Json(nullptr)}})).substr(0,32);
}
}
PiManagedStore::PiManagedStore(std::string ini,std::string self,PiManagementAnchor anchor,std::function<void(PiManagedWritePoint)> checkpoint)
    :ini_(std::move(ini)),self_(std::move(self)),directory_(ini_+".pi-management"),anchor_(std::move(anchor)),
     journal_(directory_,self_,{{"ini",ini_},{"settings",ini_+".managed-settings.json"},
        {"operations",ini_+".management-operations.json"},{"authority",ini_+".wtp-devices.json"},
        {"recurrence",ini_+".recurrence-authority.json"},{"legacy",ini_+".legacy-recurrence.json"},{"progress",ini_+".recurrence-progress-initialization.json"}}),checkpoint_(std::move(checkpoint)){
    require(anchor_.complete());
    const auto parent=std::filesystem::canonical(std::filesystem::path(ini_).has_parent_path()?std::filesystem::path(ini_).parent_path():".");
    source_binding_=PiManagementJournal::digest((parent/std::filesystem::path(ini_).filename()).string());
}
PiManagedStore::PiManagedStore(std::shared_ptr<SqliteStateStore> database,PiManagementAnchor anchor,
    std::function<void(PiManagedWritePoint)> checkpoint)
    :ini_(database?database->directory()+"/configuration":std::string{}),self_(database?database->identity():std::string{}),
     directory_(ini_+".pi-management"),anchor_(std::move(anchor)),journal_(std::move(database)),checkpoint_(std::move(checkpoint)){
    require(anchor_.complete());
    source_binding_=PiManagementJournal::digest(ini_);
}
std::string PiManagedStore::configuration_bytes(const Json &internal) const{
    if(!journal_.database_backed())return serialize_config_ini_projection_for_management(internal);
    const auto saved=journal_.inspect("ini");require(saved.has_value());
    // Normal public saves do not persist transient debug/frequency CLI flags.
    // Advanced/private changes enter through their explicit owning mutation.
    const auto settings=decode_persistent_configuration(saved->bytes,StateRole::Pi);
    return encode_persistent_configuration(internal,settings.private_settings,StateRole::Pi);
}
void PiManagedStore::bind_recurrence_storage(const std::string &path,bool previous){
    std::lock_guard lock(mutex_);require(held_&&!initialized_&&!recurrence_bound_);
    if(!journal_.database_backed())journal_=ManagedStateJournal(directory_,self_,{{"ini",ini_},{"settings",ini_+".managed-settings.json"},
        {"operations",ini_+".management-operations.json"},{"authority",ini_+".wtp-devices.json"},
        {"recurrence",path},{"legacy",ini_+".legacy-recurrence.json"},{"progress",ini_+".recurrence-progress-initialization.json"}});
    recurrence_bound_=true;require_recurrence_=previous;
}
void PiManagedStore::validate_initial_source(){std::lock_guard lock(mutex_);require(held_&&!initialized_);(void)journal_.inspect("ini");}
PiManagedStore::DatabaseImportImage PiManagedStore::database_import_image(const Json &internal,const PrivateConfiguration &private_settings){
    std::lock_guard lock(mutex_);require(!journal_.database_backed());
    const bool pending=!initialized_;
    if(pending){check_initial_rollback();require(installation_pending()&&legacy_.is_null());}
    else check();
    const auto configuration=config_handler_serialization::saved_configuration_from_internal_json(internal);
    if(!pending)require(configuration==current_pi_configuration(settings_["configuration"]));
    require(internal.at("Meta").at("debug_logging")==private_settings.debug_logging&&
        internal.at("Experimental").at("Allow Unqualified Frequency")==private_settings.allow_unqualified_frequency&&
        internal.at("Experimental").at("Allow Non-Amateur Frequency")==private_settings.allow_non_amateur_frequency);
    const auto policy=parse_privileged_network_mode(read_privileged_network_ini_value(observations_.at("ini").bytes));
    require((policy.valid||policy.missing)&&
        (private_settings.network_safety==SavedNetworkSafety::Enforced?policy.mode==PrivilegedNetworkMode::enforced:
            private_settings.network_safety==SavedNetworkSafety::InsecureDisabled&&policy.mode==PrivilegedNetworkMode::insecure_disabled));
    const auto persisted_private=saved_ini_private_configuration(observations_.at("ini").bytes);
    require(persisted_private.debug_logging==private_settings.debug_logging&&
        persisted_private.allow_unqualified_frequency==private_settings.allow_unqualified_frequency&&
        persisted_private.allow_non_amateur_frequency==private_settings.allow_non_amateur_frequency&&
        persisted_private.network_safety==private_settings.network_safety);
    const auto bytes=encode_persistent_configuration(internal,persisted_private,StateRole::Pi);
    auto authority=recurrence_;
    if(pending){
        require(authority["state"]=="unclassified"&&!authority["clean_proof"].is_null());
        const auto observation=std::optional<PiManagementJournal::Observation>{{bytes,"database-migration"}};
        authority["clean_proof"]["attestation_id"]=installation_binding(self_,authority["clean_proof"]["installation_id"],observation);
        DatabaseImportImage image;
        image.source_namespace=(std::filesystem::canonical(std::filesystem::path(ini_).has_parent_path()?
            std::filesystem::path(ini_).parent_path():std::filesystem::path("."))/std::filesystem::path(ini_).filename()).string();
        image.records={{StateRecord::Configuration,bytes},{StateRecord::RecurrenceAuthority,authority.dump()}};
        image.original_records={{StateRecord::Configuration,observations_.at("ini").bytes},{StateRecord::RecurrenceAuthority,observations_.at("recurrence").bytes}};
        auto observations=Json::object();
        for(const auto &[kind,value]:observations_)observations[kind]=value.signature;
        anchor_.coordinate(history_,[&](const auto &before,const auto &after){
            require(before==after);const auto current=journal_.inspect("authority");require(current&&current->bytes==before);
            image.records.push_back({StateRecord::Catalog,WtpCatalog::database_import_image(before)});
            image.original_records[StateRecord::Catalog]=before;observations["catalog"]=current->signature;
        });
        check_initial_rollback();
        image.source_revision=PiManagementJournal::digest(canonical_pi_management_json(Json{{"source",observations},{"configuration",PiManagementJournal::digest(bytes)}}));
        return image;
    }
    auto recovery=legacy_;
    if(!recovery.is_null()){
        const auto old=decode_ini_migration_configuration(decode_bytes(recovery.at("ini_hex").get<std::string>()),StateRole::Pi);
        const auto encoded=encode_persistent_configuration(old.internal,old.private_settings,StateRole::Pi);
        require(decode_persistent_configuration(encoded,StateRole::Pi).configuration==recovery.at("candidate").at("configuration"));
        recovery["ini_hex"]=encode_bytes(encoded);
    }
    const auto saved=pi_managed_settings_record(self_,settings_["configuration_generation"],
        Json{{"configuration",configuration},{"scheduling",settings_["scheduling"]}},
        private_digest(bytes,authority,recovery,progress_initialization_));
    if(authority["state"]=="staged")authority["staged_revision"]=saved["revision"];
    DatabaseImportImage image;
    image.source_namespace=(std::filesystem::canonical(std::filesystem::path(ini_).has_parent_path()?
        std::filesystem::path(ini_).parent_path():std::filesystem::path("."))/std::filesystem::path(ini_).filename()).string();
    image.original_records={{StateRecord::Configuration,observations_.at("ini").bytes},
        {StateRecord::ManagedSettings,observations_.at("settings").bytes},
        {StateRecord::Operations,observations_.at("operations").bytes},
        {StateRecord::RecurrenceAuthority,observations_.at("recurrence").bytes}};
    image.records={{StateRecord::Configuration,bytes},{StateRecord::ManagedSettings,saved.dump()},
        {StateRecord::Operations,operations_.dump()},{StateRecord::RecurrenceAuthority,authority.dump()}};
    if(!progress_initialization_.is_null()) {
        image.records.push_back({StateRecord::RecurrenceInitialization,progress_initialization_.dump()});
        image.original_records[StateRecord::RecurrenceInitialization]=observations_.at("progress").bytes;
    }
    if(!recovery.is_null()){
        image.records.push_back({StateRecord::RecurrenceRecovery,recovery.dump()});
        image.original_records[StateRecord::RecurrenceRecovery]=observations_.at("legacy").bytes;
    }
    Json observations=Json::object();
    for(const auto &[kind,value]:observations_)observations[kind]=value.signature;
    if(progress_){
        const auto before=progress_->snapshot();
        const auto progress=PiManagementJournal::read_private_record(ini_+".recurrence-progress.json");
        require(progress.has_value()&&progress_->snapshot()==before);
        image.progress_bytes=progress->bytes;observations["execution-progress"]=progress->signature;
    }
    anchor_.coordinate(history_,[&](const auto &before,const auto &after){
        require(before==after);
        const auto observed=journal_.inspect("authority");require(observed&&observed->bytes==before);
        image.records.push_back({StateRecord::Catalog,WtpCatalog::database_import_image(before)});
        image.original_records[StateRecord::Catalog]=before;
        observations["catalog"]=observed->signature;
    });
    check();
    image.source_revision=PiManagementJournal::digest(canonical_pi_management_json(Json{
        {"source",observations},{"configuration",PiManagementJournal::digest(bytes)},
        {"settings",saved["revision"]}}));
    return image;
}
void PiManagedStore::load(){
    initial_rollback_.reset();initial_observations_.clear();
    observations_.clear();
    for(const auto *kind:{"ini","settings","operations"}){
        const auto value=journal_.inspect(kind);require(value.has_value());observations_.emplace(kind,*value);
    }
    const auto stored=journal_.inspect("recurrence");bootstrap_=nullptr;recurrence_=nullptr;
    if(stored){
        const auto record=parse_recurrence_storage_record(stored->bytes,self_,source_binding_);
        if(record["schema"]=="pi-recurrence-bootstrap/1")bootstrap_=record;else recurrence_=record;
        observations_.emplace("recurrence",*stored);
    }
    const bool authority=!recurrence_.is_null();
    if(authority)validate_resume_integrity(recurrence_);
    const auto legacy=journal_.inspect("legacy");
    legacy_=legacy?parse_legacy(legacy->bytes,self_):Json(nullptr);
    if(journal_.database_backed()&&!legacy_.is_null())
        require(decode_persistent_configuration(decode_bytes(legacy_.at("ini_hex").get<std::string>()),StateRole::Pi).configuration==legacy_.at("candidate").at("configuration"));
    if(legacy)observations_.emplace("legacy",*legacy);
    const auto progress=journal_.inspect("progress");
    progress_initialization_=progress?parse_progress_initialization(progress->bytes,self_):Json(nullptr);
    if(progress)observations_["progress"]=*progress;
    // An existing pre-authority record is accepted only with its exact original
    // checksum. Removing a new authority image cannot satisfy that checksum.
    settings_=parse_pi_managed_settings_record(observations_.at("settings").bytes,self_,
        authority?private_digest(observations_.at("ini").bytes,recurrence_,legacy_,progress_initialization_):PiManagementJournal::digest(observations_.at("ini").bytes));
    if(!authority)require(!legacy);
    if(authority&&recurrence_["state"]=="staged")require(recurrence_["staged_revision"]==settings_["revision"]);
    validate_stored_solar(settings_["scheduling"]);
    if(settings_["scheduling"]["solar"]["enabled"]==true)require(recurrence_["state"]=="staged"||recurrence_["state"]=="native"||
        (settings_["scheduling"]["enabled"]==false&&pending_remote_resume(recurrence_)));
    if(recurrence_["state"]=="native"){
        require(recurrence_["jobs"].empty()&&(!recurrence_["controllers"].empty()||!recurrence_["clean_proof"].is_null()));
        for(const auto &controller:recurrence_["controllers"]){
            bool retired=false;for(const auto &row:recurrence_["retirements"])
                if(row["controller_id"]==controller["controller_id"]&&row["assignment_id"]==controller["assignment_id"])retired=true;
            require(retired);
        }
        require(!progress_initialization_.is_null());
    }
    if(!progress_initialization_.is_null()&&!progress_){
        progress_=std::make_shared<RecurrenceProgress>(ini_+".pi-recurrence-progress",ini_+".recurrence-progress.json",self_);
        RecurrenceListDigests empty;empty.fill(PiManagementJournal::digest(""));progress_->recover(false,empty);
    }
    operations_=parse_pi_operations_record(observations_.at("operations").bytes,self_);
    for(const auto &row:operations_["receipts"])if(row.contains("disabled_snapshot"))
        require(std::stoull(row["disabled_snapshot"]["configuration_generation"].get<std::string>())<=std::stoull(settings_["configuration_generation"].get<std::string>()));
    initialized_=true;
}
void PiManagedStore::recover(){
    std::lock_guard lock(mutex_);held_=true;initialized_=false;
    progress_.reset();progress_initialization_=nullptr;
    initial_rollback_.reset();initial_observations_.clear();
    // Validate/recover before reading any configuration or initializing history.
    const bool existed=std::filesystem::exists(std::filesystem::symlink_status(directory_));
    const auto decision=journal_.decision();
    bool initial_prepared=false,first_recurrence=false;
    if(decision&&decision->at("state")=="prepared"){
        std::set<std::string> absent,kinds;
        for(const auto &row:decision->at("records")){
            kinds.insert(row.at("kind").get<std::string>());
            if(row.at("old_digest").is_null())absent.insert(row.at("kind").get<std::string>());
        }
        initial_prepared=(kinds==std::set<std::string>{"authority","recurrence"}&&absent.contains("recurrence"))||((kinds==std::set<std::string>{"ini","settings","operations","authority"}||
            kinds==std::set<std::string>{"ini","settings","operations","authority","recurrence"})&&
            absent.contains("settings")&&absent.contains("operations")&&
            (!kinds.contains("recurrence")||absent.contains("recurrence")));
        first_recurrence=kinds==std::set<std::string>{"ini","settings","operations","authority","recurrence"}&&absent.contains("recurrence");
    }
    journal_.recover(initial_prepared||first_recurrence);
    if(require_recurrence_&&!journal_.inspect("recurrence"))require((initial_prepared||first_recurrence)&&decision.has_value()&&
        std::any_of(decision->at("records").begin(),decision->at("records").end(),[](const auto &row){return row.at("kind")=="recurrence"&&row.at("old_digest").is_null();}));
    // Before any owning parser follows the fixed path, reject existing FIFO,
    // symlink, wrong-owner, oversized, or unsafe-mode inputs. Absence is kept
    // distinct from previously initialized missing history below.
    (void)journal_.inspect("ini");
    const auto storage=journal_.inspect("recurrence");bootstrap_=nullptr;
    if(storage){
        const auto record=parse_recurrence_storage_record(storage->bytes,self_,source_binding_);
        if(record["schema"]=="pi-recurrence-bootstrap/1")bootstrap_=record;
    }
    const auto history=anchor_.read();
    if(!history){
        require(!existed||initial_prepared||!bootstrap_.is_null());
        require(!journal_.inspect("settings")&&!journal_.inspect("operations")&&(!storage||!bootstrap_.is_null())&&!journal_.inspect("legacy")&&!journal_.inspect("progress"));
        if(initial_prepared){
            initial_rollback_=journal_.decision();require(initial_rollback_==decision);
            for(const auto *kind:{"ini","settings","operations","authority","recurrence","legacy"})initial_observations_[kind]=journal_.inspect(kind);
        }
        history_.clear();observations_.clear();if(storage)observations_["recurrence"]=*storage;
        held_=false;return;
    }
    require(journal_.database_backed()||std::filesystem::is_directory(std::filesystem::symlink_status(directory_)));
    history_=*history;
    if(!journal_.inspect("settings")&&!journal_.inspect("operations")){
        const auto authority=journal_.inspect("recurrence");require(authority.has_value()&&!journal_.inspect("legacy"));
        const auto ini=journal_.inspect("ini");
        recurrence_=parse_recurrence_authority_record(authority->bytes,self_);
        require(recurrence_["state"]=="unclassified"&&
            recurrence_["controllers"].empty()&&recurrence_["retirements"].empty()&&recurrence_["jobs"].empty()&&
            recurrence_["generation"]=="1"&&recurrence_["transition_id"].is_null()&&recurrence_["staged_revision"].is_null());
        if(!recurrence_["clean_proof"].is_null())require(recurrence_["clean_proof"]["attestation_id"]==
            installation_binding(self_,recurrence_["clean_proof"]["installation_id"],ini));
        observations_.clear();observations_["recurrence"]=*authority;if(ini)observations_["ini"]=*ini;
        held_=false;return;
    }
    load();held_=false;
    if(recurrence_.is_null()){
        // Upgrade one verified pre-authority image under the existing owner.
        // Unknown prior authority remains unknown; absence never grants proof.
        recurrence_=unclassified_recurrence_authority(self_);
        if(first_recurrence)initial_rollback_=decision;
        publish(Json{{"configuration",settings_["configuration"]},{"scheduling",settings_["scheduling"]}},
            observations_.at("ini").bytes,[]{},{});
    }
}
void PiManagedStore::prepare_installation(bool prior_output_history,
    const std::function<void(PiPairInitializationProof)> &pairing){
    std::lock_guard lock(mutex_);require(!held_);
    if(initialized_||!history_.empty())return;
    const auto ini=journal_.inspect("ini");
    bool clean=!prior_output_history;
    if(journal_.database_backed()) {
        // Only the explicit first-install seed can issue this event. Missing
        // established rows and migrated installations cannot become fresh.
        const auto provenance=journal_.inspect("initialization");
        require(provenance&&ini&&!bootstrap_.is_null());
        const auto source=strict_browser_json(provenance->bytes);
        require(source.is_object()&&source.size()==8&&source.value("schema",Json(nullptr))=="state-initialization/1"&&
            source.value("identity",Json(nullptr))==self_&&source.value("role",Json(nullptr))=="pi"&&
            source.value("origin",Json(nullptr))=="fresh"&&source.value("generation",Json(nullptr))=="1"&&
            source.value("source_namespace",Json(nullptr))=="fresh"&&
            source.contains("source_revision")&&source["source_revision"].is_string()&&
            source.contains("images_digest")&&source["images_digest"].is_string());
        const auto revision=source["source_revision"].get<std::string>(),digest=source["images_digest"].get<std::string>();
        require(revision.size()==64&&revision.find_first_not_of("0123456789abcdef")==std::string::npos&&
            digest.size()==64&&digest.find_first_not_of("0123456789abcdef")==std::string::npos&&
            bootstrap_["installation_id"]==revision.substr(0,32));
        const auto defaults=decode_persistent_configuration(
            encode_persistent_configuration(installed_configuration_draft(),{},StateRole::Pi),StateRole::Pi);
        require(decode_persistent_configuration(ini->bytes,StateRole::Pi)==defaults);
    } else if(ini){
        // An untouched installed template is an input to a new installation,
        // not evidence that this hardware has never been used. All prior
        // durable authority evidence below still prevents this path.
        try{if(PiManagementJournal::read_ini_input(ini_+".stock").bytes!=ini->bytes)clean=false;}
        catch(...){clean=false;}
    }
    check_initial_rollback();
    // Retained first-write rollback and any sibling legacy recovery assets
    // carry prior evidence. They cannot be reclassified as a new installation.
    if(initial_rollback_){
        std::set<std::string> kinds;
        for(const auto &row:initial_rollback_->at("records"))kinds.insert(row.at("kind").get<std::string>());
        if(kinds!=std::set<std::string>{"authority","recurrence"})return;
    }else if(const auto catalog=journal_.inspect("authority")){
        // An unconsumed atomic bootstrap may encounter the empty catalog made
        // before its first WAL preparation. Preserve automatic retry without
        // mistaking an edited/enrolled catalog for a pristine installation.
        bool fresh=false;
        if(journal_.database_backed())fresh=!bootstrap_.is_null()&&catalog->bytes==WtpCatalog::initial_storage_image();
        else if(!bootstrap_.is_null())try{
            const auto value=strict_browser_json(catalog->bytes);
            fresh=value.is_object()&&value.size()==7&&value.value("version",Json(nullptr))==2&&
                value.value("generation",Json(nullptr))=="1"&&value.value("profiles",Json(nullptr))==Json::array()&&
                value.value("management_history",Json(nullptr))==Json::object()&&value.value("management_retired",Json(nullptr))==Json::object()&&
                value.contains("pi_management_history")&&value["pi_management_history"].is_null();
        }catch(...){}
        if(!fresh)clean=false;
    }
    for(const auto *suffix:{".wtp-assignments.json",".wtp-fleet-contexts.json",".management",".pi-management.json",".legacy-recurrence.json"}){
        const auto path=ini_+suffix;struct stat info{};
        if(!::lstat(path.c_str(),&info)){clean=false;continue;}
        require(errno==ENOENT);
    }
    WtpPiBootIdentity random;
    auto authority=unclassified_recurrence_authority(self_);
    const auto installation=bootstrap_.is_null()?random.new_boot_id():bootstrap_["installation_id"].get<std::string>();
    if(clean)authority["clean_proof"]={{"installation_id",installation},{"kind","never_initialized"},
        {"attestation_id",installation_binding(self_,installation,ini)}};
    history_=random.new_boot_id();
    try{
        if(clean&&pairing){
            try{pairing(PiPairInitializationProof(self_,installation,PiPairInitializationProof::Source::TransmitterInstallation));}
            catch(...){/* Pairing owner retains unavailable/recovery state independently. */}
        }
        anchor_.coordinate(history_,[&](const auto &before,const auto &after){
            const auto current=journal_.inspect("ini");
            const auto stored=journal_.inspect("recurrence");
            require(current.has_value()==ini.has_value()&&(!current||current->signature==ini->signature)&&
                !journal_.inspect("settings")&&!journal_.inspect("operations"));
            if(stored)require(!bootstrap_.is_null()&&parse_recurrence_storage_record(stored->bytes,self_,source_binding_)==bootstrap_);
            else require(bootstrap_.is_null());
            journal_.prepare({{"authority",PiManagementJournal::digest(before),after},
                {"recurrence",stored?std::optional<std::string>(PiManagementJournal::digest(stored->bytes)):std::nullopt,authority.dump()}},initial_rollback_);
            journal_.commit();journal_.recover();
        });
        recurrence_=authority;observations_["recurrence"]=*journal_.inspect("recurrence");
        if(ini)observations_["ini"]=*ini;
        initial_rollback_.reset();initial_observations_.clear();bootstrap_=nullptr;
    }catch(...){held_=true;throw;}
}
bool PiManagedStore::installation_pending(){
    std::lock_guard lock(mutex_);require(!held_);
    return !initialized_&&!history_.empty()&&recurrence_.is_object()&&!recurrence_["clean_proof"].is_null();
}
bool PiManagedStore::migration_pending() const {
    std::lock_guard lock(mutex_);
    return initialized_&&!held_&&recurrence_.is_object()&&recurrence_.value("state",Json(nullptr))=="retiring";
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
        if(!history_.empty()){
            require(anchor_.read()==std::optional<std::string>(history_));
            const auto current=journal_.inspect("recurrence");const auto known=observations_.find("recurrence");
            require(current&&known!=observations_.end()&&current->signature==known->second.signature&&current->bytes==known->second.bytes);
        }
    }catch(...){held_=true;throw;}
}
void PiManagedStore::check(){
    require(!held_&&initialized_);
    try{
        require(anchor_.read()==std::optional<std::string>(history_));
        require((journal_.database_backed()||std::filesystem::is_directory(std::filesystem::symlink_status(directory_)))&&!journal_.decision());
        for(const auto &[kind,expected]:observations_){
            const auto observed=journal_.inspect(kind);
            require(observed&&observed->signature==expected.signature&&observed->bytes==expected.bytes);
        }
        if(!observations_.contains("legacy"))require(!journal_.inspect("legacy"));
    }catch(...){held_=true;throw;}
}
void PiManagedStore::initialize(const Json &internal){
    std::lock_guard lock(mutex_);require(!held_);
    const auto configuration=config_handler_serialization::saved_configuration_from_internal_json(internal);
    if(initialized_){
        check();require(configuration==current_pi_configuration(settings_["configuration"]));
        if(recurrence_["state"]=="unclassified"&&recurrence_["clean_proof"].is_null()&&recurrence_["controllers"].empty()&&
            recurrence_["retirements"].empty()&&recurrence_["jobs"].empty()){
            const auto before=recurrence_;WtpPiBootIdentity random;
            recurrence_["state"]="legacy";recurrence_["generation"]=next(recurrence_["generation"]);
            recurrence_["controllers"].push_back({{"controller_id",self_},{"assignment_id",random.new_boot_id()},{"assignment_revision",settings_["candidate_digest"]}});
            try{publish(Json{{"configuration",configuration},{"scheduling",settings_["scheduling"]}},observations_.at("ini").bytes,[]{},{},true,false,false);}
            catch(...){if(!held_)recurrence_=before;throw;}
        }
        return;
    }
    const auto ini=journal_.inspect("ini");require(ini.has_value());
    initialize_transaction(internal,ini->bytes,[]{});
}
void PiManagedStore::initialize_persisted(const Json &internal,const std::function<void()> &writer){
    std::lock_guard lock(mutex_);require(bool(writer));
    initialize_transaction(internal,configuration_bytes(internal),writer);
}
void PiManagedStore::initialize_stock(const Json &internal,const std::string &ini){
    std::lock_guard lock(mutex_);initialize_transaction(internal,ini,[]{});
}
void PiManagedStore::initialize_transaction(const Json &internal,const std::string &bytes,const std::function<void()> &writer,
    const std::optional<Operation> &operation,const std::optional<std::string> &source_revision,const std::optional<std::string> &held_restore_job){
    require(!held_&&!initialized_);
    check_initial_rollback();
    const auto configuration=config_handler_serialization::saved_configuration_from_internal_json(internal);
    const bool installing=installation_pending(),prepared_owner=!history_.empty();
    require(!journal_.inspect("settings")&&!journal_.inspect("operations"));
    if(installing&&!held_restore_job){
        // Only an explicit protected setup save/action may complete this
        // installation. No parser reload, startup default, or GET can do so.
        require(operation.has_value()&&operation->route=="/api/control/v1/host/configuration-recovery"&&
            configuration.at("Operation").at("Transmit")==false&&configuration.at("Operation").at("Enable on Boot")=="Never");
    }else if(!prepared_owner)require(!anchor_.read()&&(!journal_.inspect("recurrence")||!bootstrap_.is_null()));
    const auto ini=journal_.inspect("ini");
    if(source_revision&&*source_revision!=recovery_source_revision(self_,context_epoch_,nullptr,ini))
        throw std::runtime_error("revision_conflict");
    if(!installing){
        WtpPiBootIdentity random;
        if(!prepared_owner){
            const auto stored=journal_.inspect("recurrence");observations_.clear();if(stored)observations_["recurrence"]=*stored;
            history_=random.new_boot_id();recurrence_=unclassified_recurrence_authority(self_);
        }
        else recurrence_["generation"]=next(recurrence_["generation"]);
        recurrence_["state"]="legacy";
        recurrence_["controllers"].push_back({{"controller_id",self_},{"assignment_id",random.new_boot_id()},
            {"assignment_revision",PiManagementJournal::digest(canonical_pi_management_json(Json{
                {"configuration",configuration},{"scheduling",migrated_scheduling(configuration)}}))}});
    }
    if(ini)observations_["ini"]=*ini;
    auto scheduling=migrated_scheduling(configuration);
    const auto before=recurrence_;
    if(installing){
        scheduling["enabled"]=false;recurrence_["state"]="staged";
        recurrence_["generation"]=next(recurrence_["generation"]);recurrence_["transition_id"]=held_restore_job?Json(*held_restore_job):operation->request.at("operation_id");
    }
    try{publish(Json{{"configuration",configuration},{"scheduling",scheduling}},bytes,writer,operation);}
    catch(...){if(!held_)recurrence_=before;throw;}
}
bool PiManagedStore::initialized() const {std::lock_guard lock(mutex_);return initialized_;}
void PiManagedStore::write_committed_ini(const std::string &bytes){
    std::lock_guard lock(mutex_);require(!held_);journal_.write_committed_ini(bytes);
}
const std::string &PiManagedStore::device_id() const noexcept {return self_;}
std::uint64_t PiManagedStore::context_epoch() const {std::lock_guard lock(mutex_);return context_epoch_;}
Json PiManagedStore::settings(){std::lock_guard lock(mutex_);check();return current_pi_settings_record(settings_);}
Json PiManagedStore::recurrence_authority(){std::lock_guard lock(mutex_);check();return recurrence_;}
std::shared_ptr<RecurrenceProgress> PiManagedStore::recurrence_progress(){std::lock_guard lock(mutex_);check();require(recurrence_["state"]=="native"&&bool(progress_));return progress_;}
std::optional<Json> PiManagedStore::authority_operation(const std::string &operation,const PiPairPrincipal &principal){
    std::lock_guard lock(mutex_);check();
    require(principal.self_id()==self_&&principal.identity().purpose==PiPairTlsPurpose::Management);
    if(!recurrence_.contains("management"))return {};
    if(recurrence_["management"].contains("resume")){
        const auto &saved=recurrence_["management"]["resume"];
        if(saved["grant"]["operation_id"]==operation&&saved["grant"]["controller_id"]==principal.peer_id()){
            const bool pending=saved["state"]=="pending";
            const auto digest=pending?saved["grant"]["grant_digest"].get<std::string>():PiManagementJournal::digest(canonical_pi_management_json(saved["acknowledgement"]));
            return Json{{"contract","Wsprry-Pair/1"},{"schema","operation"},{"operation_id",operation},
                {"pair_id",principal.identity().pair_id},{"authorization_generation",principal.identity().authorization_generation},
                {"request_digest",digest},{"state",pending?"pending":"committed"},{"result_digest",pending?Json(nullptr):Json(digest)}};
        }
    }
    for(const auto &row:recurrence_["management"]["operations"]){const auto &receipt=row["receipt"];
        if(receipt["operation_id"]==operation){
            if(row["controller_id"]!=principal.peer_id()||receipt["pair_id"]!=principal.identity().pair_id||
                receipt["authorization_generation"]!=principal.identity().authorization_generation)return {};
            return receipt;
        }
    }return {};
}
std::size_t PiPairHistory::reclaim_native_retired(PiManagedStore &native){
    PiPairPolicyAdmission policy;if(!policy.allowed())return 0;
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return 0;
    try{
        require(running_marked_&&service_permits()&&native.device_id()==self_);verify_selection();const auto before=store_->snapshot();
        auto catalog=parse_pi_pair_catalog(before.catalog.bytes,self_);const auto operations=parse_pi_pair_operations(before.operations.bytes,self_);
        // Root -> native owner; no peer wait. Revoked scopes cannot register a
        // new assignment or grant while their complete native duties are read.
        const auto authority=native.recurrence_authority();const auto count=catalog["retired"].size();
        Json retained=Json::array();
        for(const auto &pair:catalog["retired"]){
            bool unknown=false;for(const auto &entry:operations["entries"])if(entry["pair_id"]==pair["pair_id"]&&
                entry["authorization_generation"]==pair["authorization_generation"]&&(entry["unresolved"]==true||!entry["continuation"].is_null()))unknown=true;
            if(unknown||!recurrence_retirement_scope_closed(authority,pair["pair_id"].get<std::string>(),pair["authorization_generation"].get<std::string>()))retained.push_back(pair);
        }
        if(retained.size()==count)return 0;
        catalog["retired"]=std::move(retained);auto peers=parse_pi_pair_peers(before.peers->bytes,self_);
        require(publish_outgoing(before,catalog,operations,peers));return count-catalog["retired"].size();
    }catch(...){return 0;}
}
Json PiManagedStore::register_authority(const Json &request,const PiPairPrincipal &principal,const PiPairMutationGuard &){
    std::lock_guard lock(mutex_);check();
    require(valid_pi_pair_document(request,"authority-register")&&principal.identity().purpose==PiPairTlsPurpose::Management&&principal.self_id()==self_&&
        request["target_id"]==self_&&request["controller_id"]==principal.peer_id()&&request["pair_id"]==principal.identity().pair_id&&
        request["authorization_generation"]==principal.identity().authorization_generation);
    if(recurrence_.contains("management")&&recurrence_["management"].contains("resume")&&
        recurrence_["management"]["resume"]["grant"]["operation_id"]==request["operation_id"])throw std::runtime_error("request_id_reuse");
    const auto digest=PiManagementJournal::digest(canonical_pi_management_json(request));
    if(recurrence_.contains("management"))for(const auto &row:recurrence_["management"]["operations"])if(row["receipt"]["operation_id"]==request["operation_id"]){
        const auto retained=authority_operation(request["operation_id"].get<std::string>(),principal);
        if(!retained||(*retained)["request_digest"]!=digest)throw std::runtime_error("request_id_reuse");
        return *retained;
    }
    const Json receipt{{"contract","Wsprry-Pair/1"},{"schema","operation"},{"operation_id",request["operation_id"]},
        {"pair_id",request["pair_id"]},{"authorization_generation",request["authorization_generation"]},{"request_digest",digest},
        {"state","committed"},{"result_digest",digest}};
    const auto previous=recurrence_;
    const bool eviction=recurrence_.contains("management")&&recurrence_["management"]["operations"].size()==256;
    require(!eviction||context_epoch_<std::numeric_limits<std::uint64_t>::max());
    recurrence_=recurrence_authority_registration(recurrence_,request,receipt,history_);
    const Json candidate{{"configuration",settings_["configuration"]},{"scheduling",settings_["scheduling"]}};
    try{publish(candidate,observations_.at("ini").bytes,[]{},{},false,false,false,{},true);}
    catch(...){if(!held_)recurrence_=previous;throw;}
    if(eviction)++context_epoch_;
    return receipt;
}

void PiManagedStore::persist_scheduling(const Operation &operation,bool enabled,const std::string &accepted_occurrence){
    std::lock_guard lock(mutex_);check();
    require(valid_controller_document(operation.request,enabled?"target-enable":"target-action")&&
        operation.route=="/api/control/v1/targets/"+self_+(enabled?"/enable":"/disable")&&!replay(operation));
    if(operation.request["expected_revision"]!=settings_["revision"])throw std::runtime_error("revision_conflict");
    if(enabled){
        if(recurrence_.contains("management"))for(const auto &authority:recurrence_["management"]["authorities"])for(const auto &assignment:authority["assignments"]){
            const auto controller=authority["registration"]["controller_id"];bool known=false,retired=false;
            for(const auto &row:recurrence_["controllers"])if(row["controller_id"]==controller&&row["assignment_id"]==assignment["assignment_id"]&&row["assignment_revision"]==assignment["assignment_revision"])known=true;
            for(const auto &row:recurrence_["retirements"])if(row["controller_id"]==controller&&row["assignment_id"]==assignment["assignment_id"])retired=true;
            if(!known&&recurrence_["management"].contains("handover"))for(const auto &proof:recurrence_["management"]["handover"]["requests"])
                if(proof["state"]=="confirmed"&&proof["request"]["controller_id"]==controller&&proof["request"]["assignment_id"]==assignment["assignment_id"])
                    for(const auto &scope:proof["covered"])if(scope["pair_id"]==authority["registration"]["pair_id"]&&
                        scope["authorization_generation"]==authority["registration"]["authorization_generation"]&&scope["assignment_revision"]==assignment["assignment_revision"])known=true;
            require(known&&retired);
        }
        require(operation.request["candidate_digest"]==current_pi_settings_record(settings_)["candidate_digest"]&&
            (recurrence_["state"]=="staged"||recurrence_["state"]=="native")&&recurrence_["jobs"].empty());
        for(const auto &controller:recurrence_["controllers"]){
            bool retired=false;
            for(const auto &row:recurrence_["retirements"])if(row["controller_id"]==controller["controller_id"]&&row["assignment_id"]==controller["assignment_id"])retired=true;
            require(retired);
        }
        require(!recurrence_["controllers"].empty()||!recurrence_["clean_proof"].is_null());
        if(!progress_){
            progress_=std::make_shared<RecurrenceProgress>(ini_+".pi-recurrence-progress",ini_+".recurrence-progress.json",self_);
            RecurrenceListDigests empty;empty.fill(PiManagementJournal::digest(""));
            progress_->recover(recurrence_["state"]=="staged"&&progress_initialization_.is_null(),empty);
        }
        require(!progress_->held());
        const auto current_progress=progress_->snapshot();
        require(current_progress["in_flight"].is_null()||(!accepted_occurrence.empty()&&current_progress["in_flight"]["occurrence_id"]==accepted_occurrence));
        if(progress_initialization_.is_null()){
            const auto initial=progress_->snapshot();
            require(initial["consumed_through_utc_ns"]=="0"&&initial["in_flight"].is_null());
            for(const auto &[name,row]:initial["cursors"].items()){(void)name;require(row["index"]==0);}
        }
    }
    const auto before=recurrence_,previous_initialization=progress_initialization_;
    auto candidate=Json{{"configuration",settings_["configuration"]},{"scheduling",settings_["scheduling"]}};
    candidate["scheduling"]["enabled"]=enabled;
    if(enabled&&recurrence_["state"]!="native"){
        recurrence_["state"]="native";recurrence_["staged_revision"]=nullptr;recurrence_["generation"]=next(recurrence_["generation"]);
    }
    if(enabled&&progress_initialization_.is_null())progress_initialization_={{"schema","pi-recurrence-progress-initialization/1"},
        {"device_id",self_},{"initialization_id",WtpPiBootIdentity{}.new_boot_id()}};
    try{publish(candidate,observations_.at("ini").bytes,[]{},operation,true,false,false);}
    catch(...){if(!held_){recurrence_=before;progress_initialization_=previous_initialization;}throw;}
}
Json PiManagedStore::rollback_configuration(){
    std::lock_guard lock(mutex_);check();require(!legacy_.is_null());
    if(wtp_retirement_source_active(legacy_["candidate"]["configuration"]))throw std::runtime_error("migration_required");
    return current_pi_configuration(legacy_["candidate"]["configuration"]);
}
void PiManagedStore::persist_migration(const Json &internal,const std::function<void()> &writer,const Operation &operation,bool rollback,const std::optional<Json> &pending_job){
    std::lock_guard lock(mutex_);check();
    const auto &request=operation.request;
    require(valid_controller_document(request,rollback?"target-migration-rollback":"target-migration")&&
        operation.route=="/api/control/v1/targets/"+self_+(rollback?"/migration/rollback":"/migration")&&!replay(operation));
    if(request["expected_revision"]!=settings_["revision"]||request["expected_authority_generation"]!=recurrence_["generation"])
        throw std::runtime_error("revision_conflict");
    require((recurrence_["jobs"].empty()||(!rollback&&!pending_job&&recurrence_["state"]=="retiring"))&&recurrence_["controllers"].size()==1&&
        recurrence_["controllers"][0]["controller_id"]==self_);
    const auto row=recurrence_["controllers"][0];
    const auto configuration=config_handler_serialization::saved_configuration_from_internal_json(internal);
    Json candidate;std::string bytes;
    if(rollback){
        require((recurrence_["state"]=="staged"||recurrence_["state"]=="native")&&settings_["scheduling"]["enabled"]==false&&!legacy_.is_null()&&
            request["resume_controller_id"]==self_&&request["resume_assignment_id"]==row["assignment_id"]&&
            legacy_["assignment_id"]==row["assignment_id"]&&configuration==current_pi_configuration(legacy_["candidate"]["configuration"])&&
            row["assignment_revision"]==PiManagementJournal::digest(canonical_pi_management_json(legacy_["candidate"])));
        if(!progress_initialization_.is_null())require(progress_&&!progress_->held()&&progress_->snapshot()["in_flight"].is_null());
        candidate=legacy_["candidate"];bytes=configuration_bytes(internal);
    }else{
        require((recurrence_["state"]=="legacy"||recurrence_["state"]=="rollback"||recurrence_["state"]=="retiring")&&
            request["controllers"]==recurrence_["controllers"]&&configuration==current_pi_configuration(settings_["configuration"]));
        candidate={{"configuration",settings_["configuration"]},{"scheduling",settings_["scheduling"]}};
        require(row["assignment_revision"]==PiManagementJournal::digest(canonical_pi_management_json(
            recurrence_["state"]=="retiring"?legacy_.at("candidate"):candidate)));
        bytes=observations_.at("ini").bytes;
    }
    const auto before=recurrence_,previous=legacy_;
    try{
        recurrence_["generation"]=next(recurrence_["generation"]);
        if(before["state"]!="retiring")recurrence_["transition_id"]=request["operation_id"];
        if(rollback){recurrence_["state"]="rollback";recurrence_["staged_revision"]=nullptr;}
        else{
            if(before["state"]!="retiring")legacy_={{"schema","pi-legacy-recurrence/1"},{"device_id",self_},{"assignment_id",row["assignment_id"]},
                {"candidate",candidate},{"ini_hex",encode_bytes(bytes)}};
            require(legacy_.dump().size()<=1048576);(void)parse_legacy(legacy_.dump(),self_);
            if(pending_job){
                require(before["state"]!="retiring");recurrence_["jobs"]=Json::array({*pending_job});recurrence_["state"]="retiring";
                publish(candidate,bytes,[]{},operation,true,true,false);return;
            }
            auto &retirements=recurrence_["retirements"];
            for(auto iterator=retirements.begin();iterator!=retirements.end();){
                if((*iterator)["controller_id"]==self_&&(*iterator)["assignment_id"]==row["assignment_id"])iterator=retirements.erase(iterator);
                else ++iterator;
            }
            retirements.push_back({{"controller_id",self_},{"assignment_id",row["assignment_id"]},
                {"transition_id",recurrence_["transition_id"]},{"retirement_generation",recurrence_["generation"]},
                {"receipt_digest",PiManagementJournal::digest(canonical_pi_management_json(Json{{"schema","local-recurrence-retirement/1"},
                    {"device_id",self_},{"assignment_id",row["assignment_id"]},{"transition_id",recurrence_["transition_id"]},
                    {"generation",recurrence_["generation"]},{"disposition","retired"}}))}});
            recurrence_["state"]="staged";recurrence_["jobs"]=Json::array();candidate["scheduling"]["enabled"]=false;
        }
        publish(candidate,rollback?configuration_bytes(internal):bytes,writer,operation,true,false,rollback,
            before["state"]=="retiring"?before["transition_id"].get<std::string>():std::string{});
    }catch(...){if(!held_){recurrence_=before;legacy_=previous;}throw;}
}
void PiManagedStore::prepare_remote_migration(const Operation &operation,const Json &captured,const Json &signed_requests,const std::optional<Json> &pending_job){
    std::lock_guard lock(mutex_);check();const auto &request=operation.request;
    require(valid_controller_document(request,"target-migration")&&operation.route=="/api/control/v1/targets/"+self_+"/migration"&&
        !replay(operation)&&captured==recurrence_&&request["expected_revision"]==settings_["revision"]&&
        request["expected_authority_generation"]==recurrence_["generation"]&&recurrence_["jobs"].empty()&&
        (recurrence_["state"]=="legacy"||recurrence_["state"]=="rollback"||recurrence_["state"]=="staged"));
    const auto plan=recurrence_retirement_plan(captured,request["operation_id"].get<std::string>());
    require(!plan["requests"].empty()&&request["controllers"]==plan["controllers"]&&signed_requests.is_array()&&signed_requests.size()==plan["requests"].size());
    auto rows=plan["requests"];Json retained_fences=Json::array();
    for(std::size_t index=0;index<rows.size();++index){
        require(valid_pi_pair_document(signed_requests[index],"retirement"));auto body=signed_requests[index];body.erase("proof");
        const auto prior=recurrence_reusable_retirement(captured,rows[index]["request"]);
        if(!prior.is_null()&&prior["request"]==signed_requests[index]){
            rows[index]["request"]=prior["request"];rows[index]["state"]="confirmed";rows[index]["response"]=prior["response"];
            const auto &response=prior["response"];retained_fences.push_back({{"controller_id",response["controller_id"]},{"assignment_id",response["assignment_id"]},
                {"transition_id",response["transition_id"]},{"retirement_generation",response["retirement_generation"]},{"receipt_digest",response["receipt_digest"]}});
        }else{
            require(body==rows[index]["request"]);rows[index]["request"]=signed_requests[index];rows[index]["state"]="prepared";rows[index]["response"]=nullptr;
        }
    }
    const auto before=recurrence_,previous=legacy_;const Json candidate{{"configuration",settings_["configuration"]},{"scheduling",settings_["scheduling"]}};
    try{
        for(const auto &controller:plan["controllers"])if(controller["controller_id"]==self_){
            require(controller["assignment_revision"]==PiManagementJournal::digest(canonical_pi_management_json(candidate)));
            legacy_={{"schema","pi-legacy-recurrence/1"},{"device_id",self_},{"assignment_id",controller["assignment_id"]},
                {"candidate",candidate},{"ini_hex",encode_bytes(observations_.at("ini").bytes)}};
            (void)parse_legacy(legacy_.dump(),self_);
        }
        recurrence_["generation"]=plan["generation"];recurrence_["transition_id"]=request["operation_id"];recurrence_["state"]="retiring";
        recurrence_["controllers"]=plan["controllers"];recurrence_["retirements"]=retained_fences;recurrence_["jobs"]=Json::array();
        if(pending_job)recurrence_["jobs"].push_back(*pending_job);
        recurrence_["management"]["handover"]={{"schema","pi-remote-handover/1"},{"generation",plan["generation"]},
            {"transition_id",request["operation_id"]},{"candidate_digest",settings_["candidate_digest"]},
            {"inventory",captured["management"]["authorities"]},{"operations",captured["management"]["operations"]},{"resume_basis",recurrence_retirement_basis(captured)},
            {"controllers",plan["controllers"]},{"requests",rows}};
        publish(candidate,observations_.at("ini").bytes,[]{},operation,true,true,false);
    }catch(...){if(!held_){recurrence_=before;legacy_=previous;}throw;}
}
std::optional<Json> PiManagedStore::remote_handover(){
    std::lock_guard lock(mutex_);check();
    if(recurrence_["state"]!="retiring"||!recurrence_.contains("management")||!recurrence_["management"].contains("handover"))return {};
    return recurrence_["management"]["handover"];
}
std::optional<Json> PiManagedStore::disabled_snapshot(){
    std::lock_guard lock(mutex_);check();
    if(settings_["scheduling"]["enabled"]!=false||settings_["configuration"]["Operation"]["Transmit"]!=false)return {};
    for(auto it=operations_["receipts"].rbegin();it!=operations_["receipts"].rend();++it)if(it->contains("disabled_snapshot")){
        const auto &snapshot=(*it)["disabled_snapshot"];
        require(std::stoull(snapshot["configuration_generation"].get<std::string>())<=std::stoull(settings_["configuration_generation"].get<std::string>()));
        if(snapshot["candidate_digest"]==settings_["candidate_digest"])
            return Json{{"candidate_digest",snapshot["candidate_digest"]},{"configuration_generation",snapshot["configuration_generation"]},{"receipt",(*it)["receipt"]}};
    }
    return {};
}
void PiManagedStore::prepare_remote_resume(const Operation &operation,const Json &captured,
    const PiPairResumeTargetProof &proof,const Json &wtp_binding,const PiPairMutationGuard &){
    std::lock_guard lock(mutex_);check();const auto &request=operation.request;
    require(valid_controller_document(request,"target-migration-rollback")&&captured==recurrence_&&
        operation.route=="/api/control/v1/targets/"+self_+"/migration/rollback"&&!replay(operation)&&
        proof.body_digest()==PiManagementJournal::digest(canonical_pi_management_json(request))&&
        proof.scope()["controller_id"]==request["resume_controller_id"]&&
        request["expected_revision"]==settings_["revision"]&&request["expected_authority_generation"]==recurrence_["generation"]&&
        (recurrence_["state"]=="staged"||recurrence_["state"]=="native")&&recurrence_["jobs"].empty()&&
        recurrence_.contains("management")&&recurrence_["management"]["complete"]==true&&recurrence_["management"].contains("handover"));
    for(const auto &row:recurrence_["management"]["operations"])require(row["receipt"]["operation_id"]!=request["operation_id"]);
    if(recurrence_["management"].contains("resume")){
        const auto &previous=recurrence_["management"]["resume"];require(previous["state"]!="pending");
        const auto digest=PiManagementJournal::digest(canonical_pi_management_json(previous["acknowledgement"]));bool retained=false;
        for(const auto &row:recurrence_["management"]["operations"])if(row["controller_id"]==previous["grant"]["controller_id"]&&
            row["receipt"]["operation_id"]==previous["grant"]["operation_id"]&&row["receipt"]["request_digest"]==digest&&row["receipt"]["result_digest"]==digest)retained=true;
        require(retained);
        if(previous["state"]=="resumed"){
            // Current staged/native validity requires fresh permanent fencing;
            // keep the consumed lineage frozen in this handover as well.
            require(recurrence_["management"]["handover"]["resume_basis"]==previous);
            bool fresh=false;for(const auto &row:recurrence_["retirements"])if(row["controller_id"]==previous["grant"]["controller_id"]&&
                row["assignment_id"]==previous["grant"]["assignment_id"]&&std::stoull(row["retirement_generation"].get<std::string>())>
                std::stoull(previous["grant"]["authority_generation"].get<std::string>()))fresh=true;
            require(fresh);
        }
    }
    const auto disabled=disabled_snapshot();require(disabled.has_value());
    if(recurrence_["state"]=="native")require(progress_&&!progress_->held()&&progress_->snapshot()["in_flight"].is_null());
    Json retirement=nullptr;
    for(const auto &row:recurrence_["management"]["handover"]["requests"]) {
        require(row["state"]=="confirmed");
        if(row["request"]["controller_id"]==request["resume_controller_id"]&&row["request"]["assignment_id"]==request["resume_assignment_id"])
            retirement=Json{{"request",row["request"]},{"response",row["response"]}};
    }
    require(!retirement.is_null());Json incarnation=nullptr;
    for(const auto &row:recurrence_["management"]["authorities"]){const auto &registered=row["registration"];
        if(registered["controller_id"]==proof.scope()["controller_id"]&&registered["pair_id"]==proof.scope()["pair_id"]&&
            registered["authorization_generation"]==proof.scope()["authorization_generation"])
            for(const auto &assignment:row["assignments"])if(assignment["assignment_id"]==request["resume_assignment_id"])incarnation=registered["controller_incarnation"];
    }
    require(!incarnation.is_null());
    Json grant={{"contract","Wsprry-Pair/1"},{"schema","resume-grant"},{"operation_id",request["operation_id"]},
        {"pair_id",proof.scope()["pair_id"]},{"authorization_generation",proof.scope()["authorization_generation"]},
        {"target_id",self_},{"target_boot_id",request["binding"]["boot_id"]},{"controller_id",request["resume_controller_id"]},
        {"controller_incarnation",incarnation},{"assignment_id",request["resume_assignment_id"]},
        {"assignment_revision",retirement["request"]["assignment_revision"]},{"transition_id",retirement["request"]["transition_id"]},
        {"retirement_receipt_digest",retirement["response"]["receipt_digest"]},{"authority_generation",next(recurrence_["generation"])},
        {"disabled_revision",(*disabled)["receipt"]["revision"]},{"disable_receipt_digest",PiManagementJournal::digest(canonical_pi_management_json((*disabled)["receipt"]))},
        {"candidate_digest",settings_["candidate_digest"]},{"wtp_binding",wtp_binding}};
    grant["grant_digest"]=PiManagementJournal::digest(canonical_pi_management_json(grant));require(valid_pi_pair_document(grant,"resume-grant"));
    const auto before=recurrence_;const Json candidate{{"configuration",settings_["configuration"]},{"scheduling",settings_["scheduling"]}};
    try{
        recurrence_["generation"]=grant["authority_generation"];recurrence_["transition_id"]=request["operation_id"];
        recurrence_["state"]="reconciling";recurrence_["staged_revision"]=nullptr;
        recurrence_["management"]["resume"]={{"schema","pi-remote-resume/1"},{"grant",grant},{"disabled_snapshot",*disabled},
            {"retirement",retirement},{"state","pending"},{"acknowledgement",nullptr},{"transport",nullptr},{"registered",nullptr}};
        publish(candidate,observations_.at("ini").bytes,[]{},operation,true,true,false);
    }catch(...){if(!held_)recurrence_=before;throw;}
}
std::optional<Json> PiManagedStore::remote_resume(const PiPairPrincipal &principal){
    std::lock_guard lock(mutex_);check();require(principal.self_id()==self_&&principal.identity().purpose==PiPairTlsPurpose::Management);
    if(!recurrence_.contains("management")||!recurrence_["management"].contains("resume"))return {};
    const auto &resume=recurrence_["management"]["resume"];
    return resume["grant"]["controller_id"]==principal.peer_id()?std::optional<Json>(resume["grant"]):std::nullopt;
}
Json PiManagedStore::confirm_remote_resume(const PiPairResumeOutcomeProof &proof,const PiPairMutationGuard &){
    std::lock_guard lock(mutex_);check();const auto &response=proof.document();const auto &transport=proof.transport();
    require(valid_pi_pair_document(response,"resume-ack")&&transport.self_id()==self_&&transport.identity().purpose==PiPairTlsPurpose::Management&&
        response["target_id"]==self_&&response["controller_id"]==transport.peer_id()&&recurrence_.contains("management")&&recurrence_["management"].contains("resume"));
    const auto digest=PiManagementJournal::digest(canonical_pi_management_json(response));
    const Json receipt{{"contract","Wsprry-Pair/1"},{"schema","operation"},{"operation_id",response["operation_id"]},
        {"pair_id",transport.identity().pair_id},{"authorization_generation",transport.identity().authorization_generation},
        {"request_digest",digest},{"state","committed"},{"result_digest",digest}};
    const auto &saved=recurrence_["management"]["resume"],&grant=saved["grant"];
    if(response["operation_id"]!=grant["operation_id"]){
        for(const auto &row:recurrence_["management"]["operations"])if(row["controller_id"]==transport.peer_id()&&row["receipt"]["operation_id"]==response["operation_id"]){
            require(row["receipt"]["request_digest"]==digest&&row["receipt"]["result_digest"]==digest&&row["receipt"]["state"]=="committed");return receipt;
        }
        throw std::runtime_error("request_id_reuse");
    }
    for(const auto *field:{"operation_id","pair_id","authorization_generation","target_id","controller_id","assignment_id","grant_digest"})require(response[field]==grant[field]);
    if(saved["state"]!="pending"){
        require(saved["acknowledgement"]==response);
        const Json current_scope{{"pair_id",transport.identity().pair_id},{"authorization_generation",transport.identity().authorization_generation}};
        if(recurrence_remote_resume_active(recurrence_)&&saved["transport"]!=current_scope){
            const auto before=recurrence_;
            try{
                recurrence_["management"]["resume"]["transport"]=current_scope;recurrence_["management"]["resume"]["registered"]=nullptr;
                publish(Json{{"configuration",settings_["configuration"]},{"scheduling",settings_["scheduling"]}},observations_.at("ini").bytes,[]{},{},false,false,false,{},true);
            }catch(...){if(!held_)recurrence_=before;throw;}
        }
        return receipt;
    }
    require(recurrence_["state"]=="reconciling"&&recurrence_["transition_id"]==grant["operation_id"]);
    const auto before=recurrence_;const auto operation=grant["operation_id"].get<std::string>();
    const Json candidate{{"configuration",settings_["configuration"]},{"scheduling",settings_["scheduling"]}};
    require(candidate["scheduling"]["enabled"]==false&&candidate["configuration"]["Operation"]["Transmit"]==false);
    const bool eviction=recurrence_["management"]["operations"].size()==256;
    require(!eviction||context_epoch_<std::numeric_limits<std::uint64_t>::max());
    try{
        auto stored_receipt=receipt;stored_receipt["pair_id"]=grant["pair_id"];stored_receipt["authorization_generation"]=grant["authorization_generation"];
        auto &operations=recurrence_["management"]["operations"];
        for(const auto &row:operations)require(row["receipt"]["operation_id"]!=operation);
        operations.push_back({{"controller_id",response["controller_id"]},{"receipt",stored_receipt}});if(eviction)operations.erase(operations.begin());
        auto &current=recurrence_["management"]["resume"];current["state"]=response["outcome"];current["acknowledgement"]=response;
        current["transport"]={{"pair_id",transport.identity().pair_id},{"authorization_generation",transport.identity().authorization_generation}};
        recurrence_["state"]=response["outcome"]=="resumed"?"rollback":"staged";
        recurrence_["generation"]=next(recurrence_["generation"]);
        publish(candidate,observations_.at("ini").bytes,[]{},{},true,false,false,operation,false,response["outcome"]=="refused"?"authority_pending":nullptr);
    }catch(...){if(!held_)recurrence_=before;throw;}
    if(eviction)++context_epoch_;
    return receipt;
}
bool PiManagedStore::handover_handoff(const Json &request,const PiPairMutationGuard &){
    std::lock_guard lock(mutex_);check();const auto saved=remote_handover();if(!saved)return false;
    for(std::size_t index=0;index<(*saved)["requests"].size();++index){const auto &row=(*saved)["requests"][index];
        if(row["request"]!=request)continue;
        if(row["state"]!="prepared")return false;
        const auto before=recurrence_;
        try{recurrence_["management"]["handover"]["requests"][index]["state"]="awaiting_receipt";
            publish(Json{{"configuration",settings_["configuration"]},{"scheduling",settings_["scheduling"]}},observations_.at("ini").bytes,[]{},{},false,false,false,{},true);
            return true;
        }catch(...){if(!held_)recurrence_=before;throw;}
    }
    return false;
}
bool PiManagedStore::confirm_handover(const PiPairRetiredProof &proof){
    std::lock_guard lock(mutex_);check();const auto saved=remote_handover();if(!saved)return false;
    const auto &response=proof.document();require(valid_pi_pair_document(response,"retired"));
    for(std::size_t index=0;index<(*saved)["requests"].size();++index){const auto &row=(*saved)["requests"][index];
        auto expected=row["request"];expected.erase("proof");expected["schema"]="retired";auto received=response;received.erase("proof");received.erase("receipt_digest");
        if(expected!=received)continue;
        if(row["state"]=="confirmed")return row["response"]==response;
        if(row["state"]!="awaiting_receipt")return false;
        const auto before=recurrence_;
        try{auto &current=recurrence_["management"]["handover"]["requests"][index];current["state"]="confirmed";current["response"]=response;
            recurrence_["generation"]=next(recurrence_["generation"]);
            recurrence_["retirements"].push_back({{"controller_id",response["controller_id"]},{"assignment_id",response["assignment_id"]},
                {"transition_id",response["transition_id"]},{"retirement_generation",response["retirement_generation"]},{"receipt_digest",response["receipt_digest"]}});
            publish(Json{{"configuration",settings_["configuration"]},{"scheduling",settings_["scheduling"]}},observations_.at("ini").bytes,[]{},{},true,false,false);
            return true;
        }catch(...){if(!held_)recurrence_=before;throw;}
    }
    return false;
}
void PiManagedStore::complete_migration(){
    std::lock_guard lock(mutex_);check();
    require(recurrence_["state"]=="retiring");
    if(const auto handover=remote_handover()){
        for(const auto &row:(*handover)["requests"])require(row["state"]=="confirmed");
        // New history, even another pair for the same logical assignment, is
        // never silently omitted from the frozen retirement coverage.
        for(const auto &authority:recurrence_["management"]["authorities"])for(const auto &assignment:authority["assignments"]){
            bool covered=false;for(const auto &row:(*handover)["requests"])if(row["request"]["controller_id"]==authority["registration"]["controller_id"]&&
                row["request"]["assignment_id"]==assignment["assignment_id"])for(const auto &scope:row["covered"])
                    if(scope["pair_id"]==authority["registration"]["pair_id"]&&scope["authorization_generation"]==authority["registration"]["authorization_generation"]&&
                        scope["assignment_revision"]==assignment["assignment_revision"])covered=true;
            require(covered);
        }
    }else require(!legacy_.is_null()&&recurrence_["controllers"].size()==1&&recurrence_["controllers"][0]["controller_id"]==self_);
    const auto before=recurrence_;auto candidate=Json{{"configuration",settings_["configuration"]},{"scheduling",settings_["scheduling"]}};
    try{
        recurrence_["generation"]=next(recurrence_["generation"]);recurrence_["jobs"]=Json::array();recurrence_["state"]="staged";
        for(const auto &controller:recurrence_["controllers"])if(controller["controller_id"]==self_){
        require(!legacy_.is_null());const auto assignment=controller["assignment_id"];
        auto &retirements=recurrence_["retirements"];
        for(auto it=retirements.begin();it!=retirements.end();){
            if((*it)["controller_id"]==self_&&(*it)["assignment_id"]==assignment)it=retirements.erase(it);else ++it;
        }
        retirements.push_back({{"controller_id",self_},{"assignment_id",assignment},{"transition_id",recurrence_["transition_id"]},
            {"retirement_generation",recurrence_["generation"]},{"receipt_digest",PiManagementJournal::digest(canonical_pi_management_json(Json{
                {"schema","local-recurrence-retirement/1"},{"device_id",self_},{"assignment_id",assignment},
                {"transition_id",recurrence_["transition_id"]},{"generation",recurrence_["generation"]},{"disposition","retired"}}))}});
        }
        candidate["scheduling"]["enabled"]=false;
        publish(candidate,observations_.at("ini").bytes,[]{},{},true,false,false,before["transition_id"].get<std::string>());
    }catch(...){if(!held_)recurrence_=before;throw;}
}
void PiManagedStore::validate_snapshot(const Json &internal){
    std::lock_guard lock(mutex_);check();
    require(config_handler_serialization::saved_configuration_from_internal_json(internal)==current_pi_configuration(settings_["configuration"]));
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
    require(recurrence_["state"]!="retiring"&&recurrence_["state"]!="reconciling");
    if(operation)require(valid_controller_document(operation->request,"target-save"));
    const Json candidate{{"configuration",config_handler_serialization::saved_configuration_from_internal_json(internal)},
        {"scheduling",operation?operation->request.at("candidate").at("scheduling"):settings_["scheduling"]}};
    if(operation){
        require(!replay(*operation));
        if(operation->request["expected_revision"]!=settings_["revision"])throw std::runtime_error("revision_conflict");
        require(canonical_pi_management_json(candidate)==canonical_pi_management_json(operation->request.at("candidate")));
    }
    publish(candidate,configuration_bytes(internal),writer,operation);
}
void PiManagedStore::restore_configuration(const Json &internal,const std::string &saved,const Json &scheduling,const std::string &job){
    std::lock_guard lock(mutex_);
    const auto active=application_database();const auto hold=application_restore_hold_view();
    require(journal_.database_backed()&&active&&active->identity()==self_&&active->role()==StateRole::Pi&&
        ini_!=active->directory()+"/configuration"&&restore_maintenance_active()&&local_restore_output_held()&&restored_remote_authority_held()&&
        hold["job_id"]==job&&job.size()==32&&job.find_first_not_of("0123456789abcdef")==std::string::npos);
    require(decode_persistent_configuration(saved,StateRole::Pi).configuration==config_handler_serialization::saved_configuration_from_internal_json(internal));
    if(!initialized_){
        // This is the existing destination's positive installation proof, not
        // evidence imported from the other computer or inferred from absence.
        if(!installation_pending())prepare_installation();
        require(installation_pending());
        initialize_transaction(internal,saved,[]{},{},{},job);
    }
    check();require(recurrence_["state"]!="retiring"&&recurrence_["state"]!="reconciling");
    auto intent=scheduling.is_null()?migrated_scheduling(config_handler_serialization::saved_configuration_from_internal_json(internal)):scheduling;intent["enabled"]=false;
    // publish/load retain their existing solar/authority and resume validators.
    // A destination requiring native-authority recovery cannot be bypassed by
    // the maintenance hold or a source backup's historical permission.
    publish(Json{{"configuration",config_handler_serialization::saved_configuration_from_internal_json(internal)},{"scheduling",intent}},
        saved,[]{},{},true);
}
void PiManagedStore::persist_stock(const Json &internal,const std::string &ini){
    std::lock_guard lock(mutex_);check();
    require(recurrence_["state"]!="retiring"&&recurrence_["state"]!="reconciling");
    const Json candidate{{"configuration",config_handler_serialization::saved_configuration_from_internal_json(internal)},
        {"scheduling",settings_["scheduling"]}};
    publish(candidate,ini,[]{},{});
}
void PiManagedStore::persist_stop(const Json &internal,const std::function<void()> &writer,const Operation &operation){
    std::lock_guard lock(mutex_);check();require(bool(writer));
    require(valid_controller_document(operation.request,"target-stop")&&
        operation.route=="/api/control/v1/targets/"+self_+"/stop"&&!replay(operation));
    if(operation.request["expected_revision"]!=settings_["revision"])throw std::runtime_error("revision_conflict");
    auto expected=current_pi_configuration(settings_["configuration"]);expected["Operation"]["Transmit"]=false;
    const auto configuration=config_handler_serialization::saved_configuration_from_internal_json(internal);
    require(configuration==expected);
    publish(Json{{"configuration",configuration},{"scheduling",settings_["scheduling"]}},
        configuration_bytes(internal),writer,operation);
}
void PiManagedStore::persist_private(const Json &internal,const std::string &before,const std::string &after){
    std::lock_guard lock(mutex_);check();
    require(recurrence_["state"]!="retiring"&&recurrence_["state"]!="reconciling");
    require(before==observations_.at("ini").bytes&&after.size()<=1048576&&
        config_handler_serialization::saved_configuration_from_internal_json(internal)==current_pi_configuration(settings_["configuration"]));
    if(journal_.database_backed()){
        const auto previous=decode_persistent_configuration(before,StateRole::Pi);
        const auto proposed=decode_persistent_configuration(after,StateRole::Pi);
        auto allowed=previous.private_settings;allowed.network_safety=proposed.private_settings.network_safety;
        require(previous.configuration==proposed.configuration&&allowed==proposed.private_settings);
    }
    if(before==after)return;
    publish(Json{{"configuration",settings_["configuration"]},{"scheduling",settings_["scheduling"]}},after,[]{},{});
}
void PiManagedStore::persist_advanced(const Json &internal,const std::string &before,const std::string &after){
    std::lock_guard lock(mutex_);check();
    require(journal_.database_backed()&&recurrence_["state"]!="retiring"&&recurrence_["state"]!="reconciling"&&
        before==observations_.at("ini").bytes&&after.size()<=1048576);
    const auto previous=decode_persistent_configuration(before,StateRole::Pi);
    const auto proposed=decode_persistent_configuration(after,StateRole::Pi);
    require(advanced_only_change(previous,proposed)&&
        previous.configuration==current_pi_configuration(settings_["configuration"])&&
        proposed.configuration==config_handler_serialization::saved_configuration_from_internal_json(internal));
    if(before==after)return;
    publish(Json{{"configuration",proposed.configuration},{"scheduling",settings_["scheduling"]}},after,[]{},{});
}
std::string PiManagedStore::recovery_revision(){
    std::lock_guard lock(mutex_);require(!held_);
    if(initialized_)check();
    else {check_initial_rollback();require((!history_.empty()||!anchor_.read())&&!journal_.inspect("settings")&&!journal_.inspect("operations"));}
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
        require(canonical_pi_management_json(public_candidate)==canonical_pi_management_json(request["configuration"]));
    }
    const auto ini=stock_bytes?*stock_bytes:configuration_bytes(internal);
    if(!initialized_){initialize_transaction(internal,ini,writer,operation,request["expected_revision"].get<std::string>());return;}
    publish(Json{{"configuration",configuration},{"scheduling",settings_["scheduling"]}},ini,writer,operation);
}
void PiManagedStore::publish(const Json &proposed,const std::string &ini,const std::function<void()> &writer,const std::optional<Operation> &operation,bool authority_transition,bool pending,bool write_ini,const std::string &complete_operation,bool metadata_only,const char *completion_error){
    auto candidate=proposed;
    candidate["configuration"]=current_pi_configuration(candidate.at("configuration"));
    require(valid_pi_candidate_shape(candidate));
    if(journal_.database_backed()){
        require(decode_persistent_configuration(ini,StateRole::Pi).configuration==candidate.at("configuration"));
        if(!legacy_.is_null())require(decode_persistent_configuration(decode_bytes(legacy_.at("ini_hex").get<std::string>()),StateRole::Pi).configuration==legacy_.at("candidate").at("configuration"));
    }
    validate_stored_solar(candidate.at("scheduling"));
    if(candidate.at("scheduling").at("solar").at("enabled")==true)
        require(recurrence_["state"]=="staged"||recurrence_["state"]=="native"||
            (candidate["scheduling"]["enabled"]==false&&pending_remote_resume(recurrence_)));
    if(initialized_&&!authority_transition)require(candidate.at("scheduling").at("enabled")==settings_.at("scheduling").at("enabled"));
    require(valid_recurrence_authority_record(recurrence_,self_));
    validate_resume_integrity(recurrence_);
    auto next_authority=recurrence_;
    if(next_authority["state"]=="legacy"||(next_authority["state"]=="rollback"&&!recurrence_remote_resume_active(next_authority))){
        const auto digest=PiManagementJournal::digest(canonical_pi_management_json(candidate));
        for(auto &row:next_authority["controllers"])if(row["controller_id"]==self_&&row["assignment_revision"]!=digest){
            row["assignment_revision"]=digest;next_authority["generation"]=next(next_authority["generation"]);
        }
    }
    if(metadata_only){
        require(initialized_&&!operation&&!authority_transition&&!pending&&!write_ini&&complete_operation.empty()&&
            candidate==Json({{"configuration",current_pi_configuration(settings_["configuration"])},{"scheduling",settings_["scheduling"]}}));
        auto previous=parse_recurrence_authority_record(observations_.at("recurrence").bytes,self_),current=next_authority;
        previous.erase("management");current.erase("management");require(previous==current);
        require(ini==observations_.at("ini").bytes&&
            legacy_==(observations_.contains("legacy")?parse_legacy(observations_.at("legacy").bytes,self_):Json(nullptr))&&
            progress_initialization_==(observations_.contains("progress")?parse_progress_initialization(observations_.at("progress").bytes,self_):Json(nullptr)));
    }
    const auto generation=initialized_?(metadata_only?settings_["configuration_generation"].get<std::string>():next(settings_["configuration_generation"])):"1";
    const auto saved=pi_managed_settings_record(self_,generation,candidate,private_digest(ini,next_authority,legacy_,progress_initialization_));
    if(next_authority["state"]=="staged")next_authority["staged_revision"]=saved["revision"];
    auto receipts=initialized_?operations_["receipts"]:Json::array();
    const auto retain_disabled=[&](Json &entry){
        if(candidate["scheduling"]["enabled"]==false&&candidate["configuration"]["Operation"]["Transmit"]==false&&entry["receipt"]["state"]=="committed")
            entry["disabled_snapshot"]={{"candidate_digest",saved["candidate_digest"]},{"configuration_generation",generation}};
    };
    if(!complete_operation.empty()){
        bool found=false;
        for(auto &entry:receipts)if(entry["receipt"]["operation_id"]==complete_operation){
            require(entry["receipt"]["state"]=="pending");entry["receipt"]["state"]="committed";
            entry["receipt"]["revision"]=saved["revision"];
            if(completion_error){entry["receipt"]["state"]="rejected";entry["receipt"]["revision"]=nullptr;entry["receipt"]["error"]={{"code",completion_error},{"retryable",false}};}
            retain_disabled(entry);found=true;
        }
        require(found);
    }
    if(operation){
        const auto digest=pi_managed_request_digest(self_,operation->principal,operation->authorization_generation,operation->route,operation->request);
        const Json receipt{{"contract","Wsprry-Control/1"},{"schema","target-operation"},{"binding",operation->request.at("binding")},
            {"operation_id",operation->request.at("operation_id")},{"request_digest",digest},{"state",pending?"pending":"committed"},{"revision",pending?Json(nullptr):saved["revision"]},{"error",nullptr}};
        receipts.push_back({{"principal_id",operation->principal},{"route",operation->route},{"authorization_generation",operation->authorization_generation},
            {"request_digest",digest},{"receipt",receipt}});retain_disabled(receipts.back());
    }
    const auto retained=pi_operations_record(self_,receipts);
    require(!retained.evicted||context_epoch_<std::numeric_limits<std::uint64_t>::max());
    const auto operations=retained.document;
    // All pure validation precedes the prepared decision and any final writes.
    std::map<std::string,std::optional<PiManagementJournal::Observation>> before_files;
    for(const auto *kind:{"ini","settings","operations","recurrence","legacy","progress"}){
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
            std::vector<PiManagementJournal::Image> images{{"settings",expected("settings"),saved.dump()},
                {"operations",expected("operations"),operations.dump()},{"authority",PiManagementJournal::digest(before),after},
                {"recurrence",expected("recurrence"),next_authority.dump()}};
            if(write_ini)images.insert(images.begin(),{"ini",expected("ini"),ini});
            else require(before_files.at("ini")&&before_files.at("ini")->bytes==ini);
            if(!legacy_.is_null())images.push_back({"legacy",expected("legacy"),legacy_.dump()});
            if(!progress_initialization_.is_null()&&!before_files.at("progress"))images.push_back({"progress",std::nullopt,progress_initialization_.dump()});
            if(checkpoint_)checkpoint_(PiManagedWritePoint::BeforePrepare);
            journal_.prepare(images,initial_rollback_);
            if(checkpoint_)checkpoint_(PiManagedWritePoint::Prepared);
            journal_.commit();
            if(checkpoint_)checkpoint_(PiManagedWritePoint::Committed);
            if(write_ini)writer();
            journal_.recover();
            if(checkpoint_)checkpoint_(PiManagedWritePoint::Published);
        });
        load();require(settings_==saved&&operations_==operations);
        if(retained.evicted)++context_epoch_;
    }catch(...){held_=true;throw;}
}
} // namespace wsprrypi
