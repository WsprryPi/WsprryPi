// SPDX-License-Identifier: MIT
#pragma once
#include "wtp_server/job_service.hpp"
#include <array>
namespace wsprrypico::wtp {
inline std::string state_name(State s) {
    constexpr std::array names{"empty", "loaded", "armed", "running",
                               "complete", "aborted", "missed", "failed"};
    return names.at(static_cast<unsigned>(s));
}
}
