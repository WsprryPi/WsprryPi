#pragma once

#include <functional>

namespace wsprrypi {
// Optional native integration. Empty hooks preserve local execution. Admission
// must run at the last boundary before enabling physical RF, after preparation.
struct ScheduledExecutionHooks {
    std::function<bool()> admit_output_enable;
    std::function<void()> observe_output_enable;
    std::function<bool()> admit_silent_start;
    bool confirm_output_disable = false;
};
} // namespace wsprrypi
