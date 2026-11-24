// SPDX-License-Identifier: MIT
#pragma once
#include "paths.hpp"
#include "json.hpp"
#include <cstdint>
#include <string>
#ifdef _WIN32
#include <memory>
#endif
namespace wsprrypi {
// The browser and local CLI pin the same admitted encrypted artifact. No caller
// can select a source filename or read a decrypted snapshot through this API.
class RecoveryBackupArtifact {
public:
    RecoveryBackupArtifact(const RecoveryPaths &,const std::string &job_id);
    ~RecoveryBackupArtifact();
    RecoveryBackupArtifact(const RecoveryBackupArtifact &)=delete;
    RecoveryBackupArtifact &operator=(const RecoveryBackupArtifact &)=delete;
    std::uint64_t size()const{return size_;}
    const std::string &sha256()const{return hash_;}
    std::string read(std::uint64_t offset,std::size_t maximum)const;
private:
#ifdef _WIN32
    struct Native;std::unique_ptr<Native> native_;
#else
    int descriptor_{-1};
#endif
    std::uint64_t size_{};std::string hash_;nlohmann::json stamp_;
};
}
