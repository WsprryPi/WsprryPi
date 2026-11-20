// SPDX-License-Identifier: MIT
#pragma once
#include "companion_files.hpp"
namespace wsprrypi {
class WtpCatalog;
// Recover the owning journals before capture. This read-only operation invokes
// actual validators and preserves absence; it does not enroll or start workers.
std::vector<CompanionFile> capture_application_companions(const std::string &configuration_namespace,
    const std::string &identity,WtpCatalog *);
inline std::vector<CompanionFile> capture_application_companions(const std::string &name,const std::string &identity,WtpCatalog &catalog){
    return capture_application_companions(name,identity,&catalog);
}
} // namespace wsprrypi
