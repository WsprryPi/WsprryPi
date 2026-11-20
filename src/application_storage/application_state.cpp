// SPDX-License-Identifier: MIT
#include "application_state.hpp"
#include "application_recovery/restore_hold.hpp"
#include "config_handler.hpp"
#include "recurrence_authority.hpp"
#include "wtp_integration/catalog.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include "wtp_integration/browser_api.hpp"
#include "controller_runtime.hpp"
#include "pi_managed_host_runtime.hpp"
#include <mutex>
#include <filesystem>

namespace wsprrypi {
namespace {
struct Selection {
    std::mutex mutex;
    bool selected{};
    std::weak_ptr<SqliteStateStore> database;
};
Selection &selection(){static Selection value;return value;}
}
ApplicationStateSelection::ApplicationStateSelection(std::shared_ptr<SqliteStateStore> database)
    :database_(std::move(database)){
    if(!database_)throw StateStorageError(StateStorageError::Code::InvalidInput);
    auto &slot=selection();std::lock_guard lock(slot.mutex);
    if(slot.selected)throw std::logic_error("Application state already selected");
    select_application_restore_hold(database_);
    slot.database=database_;slot.selected=true;
}
ApplicationStateSelection::~ApplicationStateSelection(){
    auto &slot=selection();std::lock_guard lock(slot.mutex);
    retire_application_restore_hold();
    slot.database.reset(); // selected deliberately remains true
}
bool application_database_selected(){auto &slot=selection();std::lock_guard lock(slot.mutex);return slot.selected;}
std::shared_ptr<SqliteStateStore> application_database(){
    auto &slot=selection();std::lock_guard lock(slot.mutex);
    if(!slot.selected)return {};
    const auto database=slot.database.lock();
    if(!database)throw StateStorageError(StateStorageError::Code::Unavailable);
    return database;
}
std::string application_configuration_namespace(){
    const auto database=application_database();
    if(!database)throw StateStorageError(StateStorageError::Code::Unavailable);
    return database->directory()+"/configuration";
}
std::string application_origin_namespace(){
    const auto database=application_database();
    if(!database)throw StateStorageError(StateStorageError::Code::Unavailable);
    const auto saved=database->record(StateRecord::Initialization);
    if(!saved.bytes)throw StateStorageError(StateStorageError::Code::Unavailable);
    const auto source=strict_browser_json(*saved.bytes);
    if(!source.is_object()||source.size()!=8||source.value("schema",nlohmann::json(nullptr))!="state-initialization/1"||
        source.value("identity",nlohmann::json(nullptr))!=database->identity()||
        source.value("role",nlohmann::json(nullptr))!=(database->role()==StateRole::Pi?"pi":"controller"))
        throw StateStorageError(StateStorageError::Code::Unavailable);
    if(source.value("origin",nlohmann::json(nullptr))=="fresh"&&source.value("source_namespace",nlohmann::json(nullptr))=="fresh"){
        const auto directory=std::filesystem::path(database->directory());
        if(directory.filename().string().starts_with("generation-")){
            if(source.value("generation",nlohmann::json(nullptr))!="1")throw StateStorageError(StateStorageError::Code::Unavailable);
            return (directory.parent_path()/"generation-1/configuration").string();
        }
        return application_configuration_namespace();
    }
    if(source.value("origin",nlohmann::json(nullptr))!="migration"||!source.contains("source_namespace")||!source["source_namespace"].is_string())
        throw StateStorageError(StateStorageError::Code::Unavailable);
    const auto result=source["source_namespace"].get<std::string>();
    if(result.empty()||result.front()!='/'||result.find('\0')!=std::string::npos)
        throw StateStorageError(StateStorageError::Code::Unavailable);
    return result;
}
DatabaseConfigurationInput database_configuration_input(){
    const auto database=application_database();
    if(!database)throw StateStorageError(StateStorageError::Code::Unavailable);
    const auto saved=database->record(StateRecord::Configuration);
    if(!saved.bytes||!saved.record_revision)throw StateStorageError(StateStorageError::Code::Unavailable);
    return {decode_persistent_configuration(*saved.bytes,database->role()),*saved.record_revision};
}
WtpCatalog &application_catalog(){
    (void)application_database();
    if(!application_database_selected())throw StateStorageError(StateStorageError::Code::Unavailable);
    if(controller_only_mode())return controller_storage_catalog();
    if(const auto host=pi_managed_host())return host->catalog();
    throw StateStorageError(StateStorageError::Code::Unavailable);
}
std::string database_configuration_after_image(const nlohmann::json &internal){
    const auto saved=database_configuration_input();
    return encode_persistent_configuration(internal,saved.settings.private_settings,saved.settings.role);
}
std::vector<StateChange> initial_application_records(const std::string &configuration_namespace,
    const std::string &identity,StateRole role,const std::string &installation_id){
    std::vector<StateChange> result{{StateRecord::Configuration,
        encode_persistent_configuration(installed_configuration_draft(),{},role)},
        {StateRecord::Catalog,WtpCatalog::initial_storage_image()}};
    if(role==StateRole::Pi)result.push_back({StateRecord::RecurrenceAuthority,
        recurrence_bootstrap_record(identity,PiManagementJournal::digest(configuration_namespace),installation_id).dump()});
    return result;
}
} // namespace wsprrypi
