#pragma once

enum class MachinePowerOperation
{
    Reboot,
    PowerOff,
};

enum class MachinePowerStatus
{
    Requested,
    Unsupported,
    Failed,
};

struct MachinePowerResult
{
    MachinePowerStatus status = MachinePowerStatus::Unsupported;
    int error_number = 0;
};

/** @brief Return whether this build supports host-machine power control. */
bool machine_power_control_supported() noexcept;

/**
 * @brief Request a host-machine reboot or power-off.
 *
 * Linux performs the request directly through reboot(2). Other platforms fail
 * closed and return Unsupported without invoking a command or system call.
 */
MachinePowerResult request_machine_power(MachinePowerOperation operation) noexcept;
