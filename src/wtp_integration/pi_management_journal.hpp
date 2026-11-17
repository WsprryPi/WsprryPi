// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace wsprrypi {
// Parent-owned multi-record transaction. The caller holds its writer/admission
// lock throughout preparation, commit, recovery, and snapshot publication.
// This boundary grants no authority, validates no configuration policy, and
// never publishes runtime state or performs remote mutation retries.
class PiManagementJournal {
public:
    struct Image {
        std::string kind;
        std::optional<std::string> expected_old_digest;
        std::string bytes;
    };
    // Paths are fixed by the owner, never supplied by a browser. This private
    // directory is exclusively owned by this coordinator. At most seven fixed
    // destinations may be registered; a decision retains its six-image bound.
    PiManagementJournal(std::string directory, std::string self,
        std::map<std::string,std::string> destinations);
    // No final path changes during preparation. Failure leaves admission held;
    // recover a prepared decision to old images before proceeding.
    void prepare(const std::vector<Image> &,
        const std::optional<nlohmann::json> &replace_rollback = {});
    void commit();
    // Live preparer only, after its committed decision. Publish exactly the
    // staged INI bytes once through the same no-follow/exclusive atomic writer;
    // leave the complete WAL intact until normal recovery finalizes all records.
    void write_committed_ini(const std::string &);
    // Prepared -> all old images; committed -> all new images. Validates every
    // image and every final path before applying any. Returns false if no WAL.
    // Retention is only for a proven uncommitted first initialization. Its
    // owner may atomically replace that exact prepared proof in prepare().
    bool recover(bool retain_prepared = false);
    std::optional<nlohmann::json> decision();
    void verify_prepared(const nlohmann::json &expected);
    struct Observation {
        std::string bytes;
        // Includes inode and nanosecond metadata as well as content. An owner
        // can detect replacement even when an external editor restores bytes.
        std::string signature;
    };
    // Bounded, no-follow inspection of one fixed destination. Does not create
    // directories, initialize authority, or recover a transaction.
    std::optional<Observation> inspect(const std::string &kind) const;
    // Application-owned INI reader, also for privately prepared validation
    // files. Returns only a stable, bounded, owned regular no-follow input.
    // Absence is an error; callers select paths internally, never from HTTP.
    static Observation read_ini_input(const std::string &path);
    static std::string digest(const std::string &);
private:
    void directory(bool create);
    void write_decision(const nlohmann::json &);
    void validate(const nlohmann::json &) const;
    void sync_directory(const std::string &) const;
    void clean_images(const std::optional<nlohmann::json> &retained = {});
    void validate_prepared_inputs(const nlohmann::json &) const;
    std::string directory_,self_,wal_;
    std::map<std::string,std::string> destinations_;
    std::string prepared_transaction_;
    bool ini_published_{};
    std::map<std::string,std::optional<Observation>> prepared_observations_;
};
} // namespace wsprrypi
