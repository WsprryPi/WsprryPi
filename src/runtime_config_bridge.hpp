/**
 * @file runtime_config_bridge.hpp
 * @brief JSON-free access to transactional runtime configuration services.
 */

#ifndef RUNTIME_CONFIG_BRIDGE_HPP
#define RUNTIME_CONFIG_BRIDGE_HPP

#include "config_types.hpp"

#include <memory>
#include <string>
#include <vector>

class RuntimeConfigCandidateStorage;

struct RuntimeConfigCandidate
{
    std::shared_ptr<const RuntimeConfigCandidateStorage> storage;
    ArgParserConfig normalized_config{};
    bool valid = false;
    bool transmit_enabled = false;
    std::string error_reason{};
    std::vector<std::string> warnings{};
    bool migration_required = false;
    bool preserve_interactive_takeover = false;
};

void prepare_runtime_config_candidate(
    const std::string &filename,
    RuntimeConfigCandidate &candidate_out);
void commit_runtime_config_candidate(const RuntimeConfigCandidate &candidate);

void config_to_json();
void persist_runtime_transmit_disabled();
TestTonePlanningConfigSnapshot current_test_tone_planning_config_snapshot();

#endif // RUNTIME_CONFIG_BRIDGE_HPP
