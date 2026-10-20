#!/usr/bin/env python3

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require(text: str, needle: str, context: str) -> None:
    if needle not in text:
        raise AssertionError(f"missing {needle!r} in {context}")


linux = (ROOT / "machine_power_control_linux.cpp").read_text(encoding="utf-8")
unavailable = (ROOT / "machine_power_control_unavailable.cpp").read_text(encoding="utf-8")
scheduling = "".join(
    (ROOT / path).read_text(encoding="utf-8")
    for path in ("scheduling.cpp", "scheduling_runtime.cpp")
)
main = (ROOT / "main.cpp").read_text(encoding="utf-8")
makefile = (ROOT / "Makefile").read_text(encoding="utf-8")

for needle in (
    "#include <sys/reboot.h>",
    "#include <linux/reboot.h>",
    "LINUX_REBOOT_CMD_RESTART",
    "LINUX_REBOOT_CMD_POWER_OFF",
    "::reboot(LINUX_REBOOT_CMD_RESTART)",
    "::reboot(LINUX_REBOOT_CMD_POWER_OFF)",
    "sync();",
):
    require(linux, needle, "Linux machine-power implementation")

for forbidden in (
    "sys/reboot.h",
    "linux/reboot.h",
    "LINUX_REBOOT_CMD_",
    "::reboot",
    "system(",
    "popen(",
    "fork(",
    "exec",
    "sync(",
):
    if forbidden in unavailable:
        raise AssertionError(
            f"non-Linux implementation must not contain {forbidden!r}"
        )

require(unavailable, "MachinePowerStatus::Unsupported", "non-Linux implementation")
require(unavailable, "return false;", "non-Linux capability query")
require(linux, "return true;", "Linux capability query")
require(scheduling, "Machine reboot is unavailable on this platform", "scheduler diagnostics")
require(scheduling, "Machine power-off is unavailable on this platform", "scheduler diagnostics")
require(makefile, "$(filter Linux,$(HOST_OS))", "platform source selection")
require(main, "if (machine_power_control_supported())", "caller capability guard")

cleanup = main.index("signalHandler.stop();")
reboot_request = main.index("reboot_machine();")
power_off_request = main.index("shutdown_machine();")
if not cleanup < reboot_request < power_off_request:
    raise AssertionError("coordinated application cleanup must precede machine power requests")

for forbidden in ("--machine-power", "WSPRRYPI_MACHINE_POWER", "machine_power_override"):
    for path in ROOT.rglob("*"):
        if not path.is_file() or "build" in path.parts or path == Path(__file__):
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        if forbidden in text:
            raise AssertionError(f"unexpected machine-power bypass {forbidden!r} in {path}")

print("machine power control regression tests passed")
