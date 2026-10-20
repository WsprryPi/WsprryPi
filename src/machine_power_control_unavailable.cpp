#include "machine_power_control.hpp"

bool machine_power_control_supported() noexcept
{
    return false;
}

MachinePowerResult request_machine_power(MachinePowerOperation) noexcept
{
    return {MachinePowerStatus::Unsupported, 0};
}
