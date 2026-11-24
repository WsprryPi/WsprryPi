// SPDX-License-Identifier: MIT
#include "persistent_configuration.hpp"
#include "wtp_integration/control_contract.hpp"
#include "wtp_integration/controller_contract.hpp"
#include <set>
#include <limits>
#include <string_view>
#include <vector>

namespace wsprrypi {
namespace {
using Json = nlohmann::json;
using Code = StateStorageError::Code;
void require(bool value, Code code = Code::InvalidInput) { if (!value) throw StateStorageError(code); }
bool fields(const Json &value, std::initializer_list<const char *> names) {
    if (!value.is_object() || value.size() != names.size()) return false;
    for (const auto *name : names) if (!value.contains(name)) return false;
    return true;
}
const Json &definitions() {
    static const auto value = Json::parse(controller_document_definitions());
    return value;
}
std::string_view role_name(StateRole role) {
    require(role == StateRole::Pi || role == StateRole::Controller);
    return role == StateRole::Pi ? "pi" : "controller";
}
bool port(const Json &value) {
    return value.is_number_integer() && value >= 1024 && value <= 49151;
}
void validate_configuration(const Json &configuration, StateRole role) {
    if (role == StateRole::Pi) {
        const auto &rules = definitions();
        require(valid_closed_contract_document(configuration, rules.at("pi-config"), rules));
        // The normalized storage image uses the runtime serializer's canonical
        // output names, rather than preserving parser aliases or unknown text.
        const auto &output = configuration.at("Si5351").at("TX Output");
        require(output == "CLK0" || output == "CLK1" || output == "CLK2");
    } else {
        require(fields(configuration, {"Operation"}));
        const auto &operation = configuration.at("Operation");
        require(fields(operation, {"Web Port", "Socket Port"}) &&
            port(operation.at("Web Port")) && port(operation.at("Socket Port")) &&
            operation.at("Web Port") != operation.at("Socket Port"));
    }
}
PersistentConfiguration unpack(const Json &document, StateRole expected) {
    require(fields(document, {"schema", "role", "configuration", "private"}));
    require(document.at("schema") == "local-settings/1", Code::Unsupported);
    require(document.at("role") == std::string(role_name(expected)));
    validate_configuration(document.at("configuration"), expected);
    const auto &settings = document.at("private");
    require(expected == StateRole::Pi
        ? fields(settings, {"debug_logging", "network_safety", "allow_unqualified_frequency", "allow_non_amateur_frequency", "startup_loop_tx", "startup_tx_iterations"})
        : fields(settings, {"debug_logging", "network_safety"}));
    require(settings.at("debug_logging").is_boolean() &&
        (settings.at("network_safety") == "enforced" || settings.at("network_safety") == "insecure-disabled"));
    PrivateConfiguration private_settings;
    private_settings.debug_logging = settings.at("debug_logging").get<bool>();
    private_settings.network_safety = settings.at("network_safety") == "enforced"
        ? SavedNetworkSafety::Enforced : SavedNetworkSafety::InsecureDisabled;
    if (expected == StateRole::Pi) {
        require(settings.at("allow_unqualified_frequency").is_boolean() && settings.at("allow_non_amateur_frequency").is_boolean());
        private_settings.allow_unqualified_frequency = settings.at("allow_unqualified_frequency").get<bool>();
        private_settings.allow_non_amateur_frequency = settings.at("allow_non_amateur_frequency").get<bool>();
        require(settings.at("startup_loop_tx").is_boolean()&&settings.at("startup_tx_iterations").is_number_integer()&&
            settings.at("startup_tx_iterations")>=std::numeric_limits<int>::min()&&
            settings.at("startup_tx_iterations")<=std::numeric_limits<int>::max());
        private_settings.startup_loop_tx=settings.at("startup_loop_tx").get<bool>();
        private_settings.startup_tx_iterations=settings.at("startup_tx_iterations").get<int>();
    }
    return {expected, document.at("configuration"), private_settings};
}
}

std::string encode_persistent_configuration(const Json &source, const PrivateConfiguration &settings, StateRole role) {
    try {
        require(source.is_object());
        Json configuration = Json::object();
        if (role == StateRole::Pi) {
            // The authoritative current field closure excludes observations,
            // process options and unknown sections without a second key list.
            for (const auto &[section, rule] : definitions().at("pi-config").at("properties").items()) {
                require(source.contains(section) && source.at(section).is_object());
                configuration[section] = Json::object();
                for (const auto &[key, unused] : rule.at("properties").items()) {
                    (void)unused;
                    require(source.at(section).contains(key));
                    configuration[section][key] = source.at(section).at(key);
                }
            }
        } else {
            require(role == StateRole::Controller && source.contains("Operation") && source.at("Operation").is_object());
            const auto &operation = source.at("Operation");
            configuration["Operation"] = {{"Web Port", operation.at("Web Port")}, {"Socket Port", operation.at("Socket Port")}};
            require(!settings.allow_unqualified_frequency && !settings.allow_non_amateur_frequency&&
                !settings.startup_loop_tx&&settings.startup_tx_iterations==0);
        }
        require(settings.network_safety == SavedNetworkSafety::Enforced || settings.network_safety == SavedNetworkSafety::InsecureDisabled);
        Json private_settings{{"debug_logging", settings.debug_logging}, {"network_safety",
            settings.network_safety == SavedNetworkSafety::Enforced ? "enforced" : "insecure-disabled"}};
        if (role == StateRole::Pi) {
            private_settings["allow_unqualified_frequency"] = settings.allow_unqualified_frequency;
            private_settings["allow_non_amateur_frequency"] = settings.allow_non_amateur_frequency;
            private_settings["startup_loop_tx"]=settings.startup_loop_tx;
            private_settings["startup_tx_iterations"]=settings.startup_tx_iterations;
        }
        const Json document{{"schema", "local-settings/1"}, {"role", role_name(role)},
            {"configuration", configuration}, {"private", private_settings}};
        (void)unpack(document, role);
        const auto encoded = document.dump();
        require(encoded.size() <= SqliteStateStore::maximum_record_bytes);
        return encoded;
    } catch (const StateStorageError &) { throw; }
    catch (...) { throw StateStorageError(Code::InvalidInput); }
}

PersistentConfiguration decode_persistent_configuration(const std::string &bytes, StateRole expected) {
    try {
        require(bytes.size() <= SqliteStateStore::maximum_record_bytes);
        (void)role_name(expected);
        std::vector<std::set<std::string>> keys;
        auto parsed = Json::parse(bytes, [&](int depth, Json::parse_event_t event, Json &item) {
            require(depth <= 16);
            if (event == Json::parse_event_t::object_start) keys.emplace_back();
            if (event == Json::parse_event_t::key) require(!keys.empty() && keys.back().insert(item.get<std::string>()).second);
            if (event == Json::parse_event_t::object_end) { require(!keys.empty()); keys.pop_back(); }
            return true;
        });
        return unpack(parsed, expected);
    } catch (const StateStorageError &) { throw; }
    catch (...) { throw StateStorageError(Code::InvalidInput); }
}

PersistentConfiguration configuration_from_snapshot(const StateSnapshot &snapshot, StateRole role) {
    const auto configuration = snapshot.records.find(StateRecord::Configuration);
    require(configuration != snapshot.records.end(), Code::Unavailable);
    return decode_persistent_configuration(configuration->second, role);
}
} // namespace wsprrypi
