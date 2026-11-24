// SPDX-License-Identifier: MIT
#pragma once
#include "owned_state_directory.hpp"
#include <functional>
#include <cstdint>
#include <optional>
#include <string>

namespace wsprrypi {
// Independent from application generations and recovery archives. Production
// supplies the OS account home; only typed storage tests supply another root.
class ControllerIdentityStore {
public:
    explicit ControllerIdentityStore(std::string owner_home,
#ifdef _WIN32
        std::optional<std::wstring> owner_sid = {});
#else
        std::optional<std::uint64_t> owner_uid = {});
#endif
    std::optional<std::string> read() const;
    std::string initialize(const std::function<std::string()> &random_identity,
        const std::function<void(OwnedStatePublicationPoint)> &checkpoint = {}) const;
    std::string directory() const;
private:
    void validate_home() const;
    std::string home_;
#ifdef _WIN32
    std::wstring owner_sid_;
#else
    std::uint64_t owner_uid_;
#endif
};

// No-op on Linux, preserving its accepted machine-ID and owner namespaces.
// Native controller entry points call this before any identity consumption.
void configure_native_controller_identity();
// Explicit initialize-state only, before creating a new application root.
// Existing state retains its established identity; startup never provisions.
void initialize_native_controller_identity(const std::string &application_root);
bool controller_identity_matches(const std::string &) noexcept;
}
