/**
 * @file runtime_config_bridge.cpp
 * @brief Adapts JSON-backed configuration transactions to a narrow interface.
 */

#include "runtime_config_bridge.hpp"

#include "config_handler.hpp"
#include "wtp_runtime_bridge.hpp"

#include <stdexcept>
#include <utility>

class RuntimeConfigCandidateStorage
{
public:
    PreparedConfigCandidate prepared;
};

void prepare_runtime_config_candidate(
    const std::string &filename,
    RuntimeConfigCandidate &candidate_out)
{
    auto storage = std::make_shared<RuntimeConfigCandidateStorage>();
    prepare_ini_config_candidate(filename, storage->prepared);

    candidate_out.storage = storage;
    candidate_out.normalized_config = storage->prepared.normalized_config;
    candidate_out.valid = storage->prepared.valid;
    if (candidate_out.valid) {
        const auto &cfg = candidate_out.normalized_config;
        const auto error = wtp_runtime_selection_error(cfg.transmit_backend == TransmitBackendKind::WTP
            ? std::optional<WtpSettings>(cfg.wtp) : std::nullopt);
        if (!error.empty()) { storage->prepared.valid = candidate_out.valid = false; storage->prepared.error_reason = error; }
    }
    candidate_out.transmit_enabled = storage->prepared.transmit_enabled;
    candidate_out.error_reason = storage->prepared.error_reason;
    candidate_out.warnings = storage->prepared.warnings;
    candidate_out.migration_required = storage->prepared.migration_required;
    candidate_out.preserve_interactive_takeover = storage->prepared.preserve_interactive_takeover;
}

void commit_runtime_config_candidate(const RuntimeConfigCandidate &candidate)
{
    if (!candidate.storage)
        throw std::logic_error("Runtime configuration candidate has no prepared storage.");
    const auto &cfg = candidate.normalized_config;
    const auto error = wtp_runtime_selection_error(cfg.transmit_backend == TransmitBackendKind::WTP
        ? std::optional<WtpSettings>(cfg.wtp) : std::nullopt);
    if (!error.empty()) throw std::runtime_error(error);
    commit_config_candidate(candidate.storage->prepared);
}

void persist_runtime_transmit_disabled()
{
    iniFile.set_bool_value("Operation", "Transmit", false);
    iniFile.commit_changes();
}
