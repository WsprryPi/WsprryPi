// SPDX-License-Identifier: MIT
#include "migration_source.hpp"
#include "ini_migration.hpp"
#include "companion_capture.hpp"
#include "controller_settings.hpp"
#include "sqlite_catalog.hpp"
#include "config_handler.hpp"
#include "wtp_integration/catalog.hpp"
#include "wtp_integration/pi_managed_store.hpp"
#include "wtp_integration/controller_retirement.hpp"
#include "wtp_integration/fleet_store.hpp"
#include "wtp_integration/pi_pair_owner.hpp"
#include "wtp_endpoint/runtime.hpp"
#include "recurrence_authority.hpp"
#include "wtp_integration/browser_api.hpp"
#include <cerrno>
#include <filesystem>
#include <sys/stat.h>
namespace wsprrypi {
namespace {
void require(bool value){if(!value)throw StateStorageError(StateStorageError::Code::Unavailable);}
bool present(const std::string &path){struct stat info{};if(!::lstat(path.c_str(),&info))return true;require(errno==ENOENT);return false;}
}
struct StateMigrationSource::Impl {
    std::string source,self;StateRole role;
    std::vector<std::string> warnings;
    std::unique_ptr<WtpCatalog> catalog;
    std::unique_ptr<PiManagedStore> native;
    std::unique_ptr<ControllerRetirementBook> retirement;
    Impl(std::string input,std::string identity,StateRole selected):self(std::move(identity)),role(selected){
        require(role==StateRole::Pi||role==StateRole::Controller);
        require(self.size()==32&&self.find_first_not_of("0123456789abcdef")==std::string::npos&&
            !input.empty()&&input.find('\0')==std::string::npos);
        const auto path=std::filesystem::path(input);
        source=(std::filesystem::canonical(path.has_parent_path()?path.parent_path():std::filesystem::path("."))/path.filename()).string();
        (void)PiManagementJournal::read_ini_input(source);
        const auto retirement_root=private_device_state_directory(PrivateDeviceDomain::ControllerRetirements,self);
        if(present(source+".wtp-devices.json"))catalog=std::make_unique<WtpCatalog>(source+".wtp-devices.json");
        else{
            require(!present(source+".management")&&!present(source+".pi-management")&&!present(source+".managed-settings.json")&&
                !present(source+".management-operations.json")&&!present(source+".pi-management.json")&&!present(retirement_root)&&!present(retirement_root+".established"));
            if(role==StateRole::Pi)catalog=std::make_unique<WtpCatalog>(source+".wtp-devices.json");
        }
        if(role==StateRole::Pi){
            native=std::make_unique<PiManagedStore>(source,self,catalog->pi_management_anchor(self,{}));
            const auto recurrence=private_device_state_directory(PrivateDeviceDomain::Recurrence,self);
            if(present(recurrence)){require(present(recurrence+"/authority.json"));native->bind_recurrence_storage(recurrence+"/authority.json",true);}
            native->recover();
            if(!native->initialized()&&!native->installation_pending()){
                const auto decoded=decode_ini_migration_configuration(PiManagementJournal::read_ini_input(source).bytes,StateRole::Pi);
                // Existing plain INI input is not proof of unused hardware.
                // Reuse the current local authority's legacy initialization;
                // normal startup still performs its established native handover.
                native->initialize(decoded.internal);
            }
        }
        if(present(retirement_root)||present(retirement_root+".established")){
            require(catalog!=nullptr);
            WtpFleetStore assignments(source+".wtp-assignments.json",self);
            std::function<void(PiPairInitializationProof)> pairing;
            const auto owner=prepare_pi_pair_owner(self);
            pairing=[owner](PiPairInitializationProof proof){owner->provision(std::move(proof));};
            retirement=std::make_unique<ControllerRetirementBook>(retirement_root,source+".wtp-assignments.json",self,
                assignments.snapshot().first.at("assignments"),std::function<void(RetirementWritePoint)>{},std::move(pairing));
        }
    }
};
StateMigrationSource::StateMigrationSource(std::string source,std::string identity,StateRole role)
    :impl_(std::make_unique<Impl>(std::move(source),std::move(identity),role)){}
StateMigrationSource::~StateMigrationSource()=default;
const std::vector<std::string> &StateMigrationSource::warnings() const{return impl_->warnings;}
StateMigrationCapture StateMigrationSource::capture(){
    auto &source=*impl_;
    const auto input=PiManagementJournal::read_ini_input(source.source);
    const auto decoded=decode_ini_migration_configuration(input.bytes,source.role);
    source.warnings=decoded.warnings;
    StateMigrationCapture result;
    if(source.role==StateRole::Pi)result=source.native->database_import_image(decoded.internal,decoded.private_settings);
    else{
        result.source_namespace=source.source;result.original_records[StateRecord::Configuration]=input.bytes;
        auto catalog=WtpCatalog::initial_storage_image();std::string original;
        if(source.catalog){original=source.catalog->current_storage_image();catalog=WtpCatalog::database_import_image(original);result.original_records[StateRecord::Catalog]=original;}
        result.records={{StateRecord::Configuration,encode_persistent_configuration(decoded.internal,decoded.private_settings,StateRole::Controller)},
            {StateRecord::Catalog,catalog}};
        result.source_revision=PiManagementJournal::digest(input.signature+":"+original);
    }
    result.companions=capture_application_companions(source.source,source.self,source.catalog.get());
    if(source.retirement){
        WtpFleetStore assignments(source.source+".wtp-assignments.json",source.self);
        (void)source.retirement->read("sqlite-migration",assignments.snapshot().first.at("assignments"));
    }
    require(PiManagementJournal::read_ini_input(source.source).signature==input.signature);
    return result;
}
void validate_application_storage(std::shared_ptr<SqliteStateStore> database){
    require(database!=nullptr);
    WtpCatalog catalog(sqlite_catalog_storage(database));(void)catalog.snapshot({});
    if(database->role()==StateRole::Pi){
        PiManagedStore owner(database,catalog.pi_management_anchor(database->identity(),{}));
        owner.bind_recurrence_storage({},true);owner.recover();
        if(owner.initialized())(void)owner.settings();
        else if(!owner.installation_pending()){
            const auto record=database->record(StateRecord::RecurrenceAuthority),provenance=database->record(StateRecord::Initialization);
            require(record.bytes&&provenance.bytes);
            const auto bootstrap=parse_recurrence_storage_record(*record.bytes,database->identity(),PiManagementJournal::digest(database->directory()+"/configuration"));
            const auto origin=strict_browser_json(*provenance.bytes);
            require(bootstrap["schema"]=="pi-recurrence-bootstrap/1"&&origin.value("origin",nlohmann::json(nullptr))=="fresh"&&
                origin.contains("source_revision")&&origin["source_revision"].is_string()&&
                bootstrap["installation_id"]==origin["source_revision"].get<std::string>().substr(0,32));
        }
    }else{ControllerSettings owner(database);(void)owner.snapshot();}
    (void)capture_application_companions(database->directory()+"/configuration",database->identity(),catalog);
}
} // namespace wsprrypi
