#!/usr/bin/env python3

from pathlib import Path


source = Path(__file__).with_name("signal_wait.cpp").read_text(encoding="utf-8")

linux_start = source.index("#if defined(__linux__)")
mac_start = source.index("#elif defined(__APPLE__)")
unsupported_start = source.index("#else", mac_start)
linux_branch = source[linux_start:mac_start]
mac_branch = source[mac_start:unsupported_start]

assert "sigwaitinfo(&set, &signal_info)" in linux_branch
assert "normalize_sigwaitinfo_result(returned_signal, errno)" in linux_branch
assert "sigwait(" not in linux_branch.replace("sigwaitinfo", "")

assert "sigwait(&set, &received_signal)" in mac_branch
assert "normalize_sigwait_result(returned_error, received_signal)" in mac_branch
assert "sigwaitinfo" not in mac_branch
assert "errno" not in mac_branch

assert '#error "Signal-Handler synchronous waiting is supported only on Linux and macOS"' in source
assert "getenv(" not in source

print("Signal wait compile-time platform selection: PASS")
