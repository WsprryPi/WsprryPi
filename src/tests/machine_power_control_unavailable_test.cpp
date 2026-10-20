#include "../machine_power_control.hpp"

#include <cassert>

int main()
{
    assert(!machine_power_control_supported());

    const MachinePowerResult reboot_result =
        request_machine_power(MachinePowerOperation::Reboot);
    assert(reboot_result.status == MachinePowerStatus::Unsupported);
    assert(reboot_result.error_number == 0);

    const MachinePowerResult power_off_result =
        request_machine_power(MachinePowerOperation::PowerOff);
    assert(power_off_result.status == MachinePowerStatus::Unsupported);
    assert(power_off_result.error_number == 0);
}
