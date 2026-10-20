#!/usr/bin/env python3

from pathlib import Path


source = Path(__file__).with_name("thread_affinity.cpp").read_text(encoding="utf-8")

linux_start = source.index("#if defined(__linux__)")
unavailable_start = source.index("#elif defined(__APPLE__)")
unsupported_start = source.index("#else", unavailable_start)
linux_branch = source[linux_start:unavailable_start]
unavailable_branch = source[unavailable_start:unsupported_start]

for required in (
    "cpu_set_t cpus",
    "CPU_ZERO(&cpus)",
    "CPU_SET(cpu, &cpus)",
    "pthread_setaffinity_np(",
    "pthread_self()",
    "ThreadAffinityStatus::Failed",
):
    assert required in linux_branch

for forbidden in (
    "cpu_set_t",
    "CPU_ZERO",
    "CPU_SET",
    "pthread_setaffinity_np",
    "thread_policy_set",
):
    assert forbidden not in unavailable_branch

assert "ThreadAffinityStatus::Unsupported" in unavailable_branch
assert '#error "Transmit-thread CPU affinity is supported only on Linux' in source
assert "getenv(" not in source

print("Transmit thread affinity platform selection: PASS")
