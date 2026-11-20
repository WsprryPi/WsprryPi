// SPDX-License-Identifier: MIT
#pragma once
#include "application_storage/persistent_configuration.hpp"
#include <array>
#include <string_view>
namespace wsprrypi {
enum class AdvancedValueKind {Boolean,Integer,Choice};
struct AdvancedField {
    const char *name;
    const char *section;
    const char *key;
    AdvancedValueKind kind;
    bool controller;
    int minimum,maximum;
};
const std::array<AdvancedField,10> &advanced_fields();
const AdvancedField *advanced_field(std::string_view);
bool valid_advanced_value(const AdvancedField &,const nlohmann::json &);
nlohmann::json parse_advanced_argument(const AdvancedField &,std::string_view);
nlohmann::json advanced_values(const PersistentConfiguration &);
PersistentConfiguration apply_advanced_changes(const PersistentConfiguration &,const nlohmann::json &);
// Owner-side defense: startup counters, safety policy, and other configuration
// fields cannot ride along with an advanced-settings decision.
bool advanced_only_change(const PersistentConfiguration &,const PersistentConfiguration &);
bool valid_advanced_view(const nlohmann::json &);
}
