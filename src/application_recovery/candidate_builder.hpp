// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
namespace wsprrypi {
// Narrow offline application entry, called only by the independent maintenance
// owner after a confirmed hold/drain and exclusive stopped-backend admission.
class RecoveryCandidateBuilder {
public:
    static nlohmann::json build(const std::string &job_id);
    static nlohmann::json seal(const std::string &job_id);
};
}
