// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <optional>
#include <string>
namespace wsprrypi {
// Private catalog authority for this Pi's settings transaction history. This
// neither enrolls a remote device nor grants settings or execution permission.
struct PiManagementAnchor {
    std::function<std::optional<std::string>()> read;
    using Commit = std::function<void(const std::string &before,const std::string &after)>;
    // Explicit write preparation only. Holds the catalog lock while the owner
    // stages/applies these exact bytes with its other records in one WAL.
    // The callback must not reenter the catalog. Failure never rolls back an
    // uncertain committed decision; the caller retains its admission hold.
    std::function<void(const std::string &history,const Commit &)> coordinate;
    bool complete() const {return bool(read)&&bool(coordinate);}
};
} // namespace wsprrypi
