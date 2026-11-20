// SPDX-License-Identifier: MIT
#include "route_maintenance.hpp"
#include "persistent_configuration.hpp"
#include "sqlite_catalog.hpp"
#include "wtp_integration/catalog.hpp"
#include "wtp_integration/pi_managed_store.hpp"
#include "config_handler_serialization.hpp"
namespace wsprrypi {
nlohmann::json route_maintenance_state(std::shared_ptr<SqliteStateStore> database,
    const std::optional<std::string> &route){
    using Json=nlohmann::json;
    if(!database||database->role()!=StateRole::Pi||(route&&*route!="gpio4"&&*route!="gpio20"))
        throw StateStorageError(StateStorageError::Code::InvalidInput);
    const auto record=database->record(StateRecord::Configuration);
    if(!record.bytes)throw StateStorageError(StateStorageError::Code::Unavailable);
    const auto saved=decode_persistent_configuration(*record.bytes,StateRole::Pi);
    auto candidate=saved.configuration;
    auto backend=candidate.at("Operation").at("Transmit Backend").get<std::string>();
    if(backend!="gpio"&&backend!="rp1-gpclk"&&backend!="si5351")
        throw StateStorageError(StateStorageError::Code::InvalidInput);
    if(route){
        const auto mode=candidate.at("Operation").at("Mode").get<std::string>();
        if(mode!="WSPR"&&mode!="QRSS"&&mode!="FSKCW"&&mode!="DFCW")
            throw StateStorageError(StateStorageError::Code::InvalidInput);
        candidate["Operation"]["Transmit Backend"]="rp1-gpclk";
        candidate["Operation"]["Transmit"]=false;
        candidate["GPIO"]["Transmit Pin"]=*route=="gpio4"?4:20;
        if(candidate!=saved.configuration){
            // The actual native owner commits settings, recurrence, catalog
            // authority and receipts together; no independent SQL settings edit.
            WtpCatalog catalog(sqlite_catalog_storage(database));
            PiManagedStore owner(database,catalog.pi_management_anchor(database->identity(),{}));
            owner.bind_recurrence_storage({},true);owner.recover();
            owner.persist(candidate,[]{});
        }
        backend="rp1-gpclk";
    }
    const auto pin=candidate.at("GPIO").at("Transmit Pin").get<int>();
    return Json{{"contract","wsprrypi-route-application-v1"},{"backend",backend},
        {"route",pin==4?Json("gpio4"):pin==20?Json("gpio20"):Json(nullptr)},
        {"transmit",candidate.at("Operation").at("Transmit")},{"config",database->directory()+"/configuration"}};
}
}
