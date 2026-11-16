// SPDX-License-Identifier: MIT
#pragma once
#include "pico_management_context.hpp"
#include "catalog.hpp"
#include <map>
#include <shared_mutex>
namespace wsprrypi {
// Serializes profile replacement with each complete bound management operation.
// Catalog HTTP mutations take the exclusive side; management takes the shared side.
std::shared_mutex &pico_management_catalog_gate();
struct PicoTargetReply {
    PicoHttpResponse response;
    std::string context,operation,location,readback_revision;
    std::string recovery_resource{};
};
class PicoTargetController {
public:
    using Active=std::function<std::optional<WtpSettings>()>;
    using Fixtures=std::function<PicoManagementTarget::Fixtures(const WtpCatalogProfile &)>;
    PicoTargetController(WtpCatalog &,Active,std::string directory,std::string identity,Fixtures={});
    PicoTargetReply handle(const BrowserRequest &,const std::string &profile,
        const std::string &context,const std::string &operation);
    // Caller holds the catalog's exclusive management gate after an explicit
    // catalog mutation. Accepted operations have already left their durable record.
    void catalog_changed();
private:
    struct Entry {
        WtpCatalogProfile profile;
        std::string fingerprint,profile_generation,connection_generation;
        std::shared_ptr<PicoManagementContext> context;
        std::atomic_bool enrolled{};
        std::mutex operation;
    };
    std::shared_ptr<Entry> selected(const std::string &device,const std::string &profile,bool create);
    bool current(const Entry &);
    WtpCatalog &catalog_;
    Active active_;
    Fixtures fixtures_;
    std::string directory_,identity_;
    std::mutex mutex_;
    std::map<std::string,std::shared_ptr<Entry>> entries_;
    std::atomic<unsigned> connections_{};
};
namespace testing {
// Install before listeners start; clear after they stop. There is no runtime
// selector for this host-only source-pair fixture dependency.
void set_pico_target_controller(std::shared_ptr<PicoTargetController>);
}
} // namespace wsprrypi
