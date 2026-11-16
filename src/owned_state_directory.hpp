// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <string>
namespace wsprrypi {
enum class OwnedStatePublicationPoint {PreparedDirectory,WrittenImage,StagedImage,PublishedDirectory,SyncedDirectory};
struct OwnedStateDirectory {std::string file;bool existed{};};
// Application-owned fixed namespace, never an HTTP path. Publish a complete
// private initial record and directory atomically, without replacing an existing
// path. One bounded preparation survives interruption. Existing final records
// are recovered/validated by their owner, never recreated by this helper.
OwnedStateDirectory prepare_owned_state_directory(const std::string &directory,
    const std::function<std::string()> &initial,
    const std::function<void(const std::string &)> &validate_initial,
    const std::function<void(OwnedStatePublicationPoint)> &checkpoint = {});
}
