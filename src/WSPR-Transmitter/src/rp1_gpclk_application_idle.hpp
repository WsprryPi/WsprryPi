#pragma once

namespace wsprrypi {

struct Rp1GpclkApplicationIdleState {
    bool controller_prepared{false};
    bool execution_active{false};
    bool schedule_committed{false};
    bool stop_or_drain_active{false};
    bool cancellation_or_cleanup_active{false};
    bool provider_lease_active{false};
    bool backend_transaction_active{false};
    bool shutdown_or_restart_active{false};

    bool complete() const noexcept {
        return !controller_prepared && !execution_active && !schedule_committed &&
               !stop_or_drain_active && !cancellation_or_cleanup_active &&
               !provider_lease_active && !backend_transaction_active &&
               !shutdown_or_restart_active;
  }
};

} // namespace wsprrypi
