// SPDX-License-Identifier: MIT
#pragma once
#include "persistent_configuration.hpp"

namespace wsprrypi {
// One backend-selected namespace for the process lifetime. Web/CLI clients do
// not select or open it. Teardown never falls back to an INI/file owner.
class ApplicationStateSelection {
public:
    explicit ApplicationStateSelection(std::shared_ptr<SqliteStateStore>);
    ~ApplicationStateSelection();
    ApplicationStateSelection(const ApplicationStateSelection &)=delete;
    ApplicationStateSelection &operator=(const ApplicationStateSelection &)=delete;
private:
    std::shared_ptr<SqliteStateStore> database_;
};
bool application_database_selected();
std::shared_ptr<SqliteStateStore> application_database();
std::string application_configuration_namespace();
// Stable original namespace identity for independently owned authority whose
// binding predates migration. It is metadata, never a path to reopen an INI.
std::string application_origin_namespace();
class WtpCatalog;
WtpCatalog &application_catalog();
struct DatabaseConfigurationInput {
    PersistentConfiguration settings;
    std::string record_revision;
};
DatabaseConfigurationInput database_configuration_input();
std::string database_configuration_after_image(const nlohmann::json &);
std::vector<StateChange> initial_application_records(const std::string &configuration_namespace,
    const std::string &identity,StateRole,const std::string &installation_id);
} // namespace wsprrypi
