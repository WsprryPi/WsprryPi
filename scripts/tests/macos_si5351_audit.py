#!/usr/bin/env python3
"""Read-only native profile audit: no device enumeration or probe is run."""
import subprocess
import sys
from pathlib import Path
binary = Path(sys.argv[1]).resolve()
transport = subprocess.check_output([str(binary), "--si5351-transport"], text=True).strip()
assert transport == "mcp2221a-hid", transport
backends = subprocess.check_output([str(binary), "--list-backends"], text=True).strip()
assert set(backends.split(",")) == {"si5351", "wtp"}, backends
libraries = subprocess.check_output(["otool", "-L", str(binary)], text=True)
assert "IOKit.framework" in libraries and "CoreFoundation.framework" in libraries
assert "hidapi" not in libraries
symbols = subprocess.check_output(["nm", "-u", str(binary)], text=True)
# Generic POSIX ioctl can be used by unrelated host code. The selected device
# objects must contain no ioctl/open/read/write device access symbols or paths.
objects = list(binary.parents[1].glob("obj/release/*transport-mcp2221a-hid/**/si5351*.o"))
assert objects, "native profile device objects missing"
for obj in objects:
    undefined = subprocess.check_output(["nm", "-u", str(obj)], text=True)
    assert "_ioctl" not in undefined and " _open\n" not in undefined, str(obj)
    strings = subprocess.check_output(["strings", str(obj)], text=True)
    assert "/dev/i2c-" not in strings and "I2C_SLAVE" not in strings, str(obj)
assert "IOHIDDeviceSetReportWithCallback" in symbols
print("Native Si5351 build/source/symbol audit passed")
