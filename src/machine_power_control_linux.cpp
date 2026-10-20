#include "machine_power_control.hpp"

#include <cerrno>
#include <sys/reboot.h>
#include <linux/reboot.h>
#include <unistd.h>

bool machine_power_control_supported() noexcept
{
    return true;
}

MachinePowerResult request_machine_power(MachinePowerOperation operation) noexcept
{
    // Preserve the existing Linux behavior: flush filesystem buffers before
    // directly requesting the selected reboot(2) operation.
    sync();

    const int result = operation == MachinePowerOperation::Reboot
                           ? ::reboot(LINUX_REBOOT_CMD_RESTART)
                           : ::reboot(LINUX_REBOOT_CMD_POWER_OFF);
    if (result < 0)
    {
        return {MachinePowerStatus::Failed, errno};
    }

    return {MachinePowerStatus::Requested, 0};
}
