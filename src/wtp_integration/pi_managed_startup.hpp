// SPDX-License-Identifier: MIT
#pragma once
#include "catalog.hpp"
#include "pi_managed_store.hpp"
#include "config_ini_draft.hpp"
#include <memory>
namespace wsprrypi {
// Singleton application lifetime, not a configuration or network selector.
// The caller supplies its verified OS identity, prepares before INI parsing,
// and activates only after native startup quiescence/authority recovery. All
// output/listener workers must stop before destruction.
class PiManagedStartup {
public:
    using ConfigurationGate=std::function<void(const std::function<void()> &)>;
    PiManagedStartup(std::string ini,std::string self,std::function<void(PiPairInitializationProof)> pairing = {});
    // Admitted migration or explicit first-install seed. Preparation never
    // imports files or reclassifies missing established history as fresh.
    explicit PiManagedStartup(std::shared_ptr<SqliteStateStore>,std::function<void(PiPairInitializationProof)> pairing = {});
    ~PiManagedStartup();
    PiManagedStartup(const PiManagedStartup &)=delete;
    PiManagedStartup &operator=(const PiManagedStartup &)=delete;
    void prepare();
    void activate(ConfigurationGate);
    bool admission_ready() noexcept;
    const std::string &device_id() const noexcept;
    bool configuration_validated() const;
    std::optional<ConfigIniDraftSnapshot> draft() const;
    // Runs one explicitly authorized recovery save/action through the installed
    // owner while native startup remains closed. Caller performs API binding,
    // CSRF, capability and If-Match checks; the store owns source/replay guards.
    nlohmann::json execute_recovery(const PiManagedStore::Operation &,
        const std::function<void()> &validated_mutation);
    PiManagedStore &store();
    WtpCatalog &catalog();
private:
    struct State;
    std::shared_ptr<State> state_;
};
} // namespace wsprrypi
