#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
collector = (root / "scripts/collect-support-bundle.sh").read_text()
runtime = (root / "src/scheduling.cpp").read_text()
websocket = (root / "src/web_socket_commands.cpp").read_text()
admin_probe = (root / "src/WSPR-Transmitter/src/rp1_gpclk_admin_probe.cpp").read_text()

for command in ("status -m rp1-gpclk-dkms", "-k \"$RP1_KERNEL\" -F",
                "rp1_gpclk_runtime_provider_inspect",
                "rp1_gpclk_loaded_modules"):
    assert command in collector, command
for command in ("overlay-sha256.txt", "rp1_gpclk_module_state",
                "rp1_gpclk_boot_route"):
    assert command not in collector, command
for evidence in ("/dev/rp1-gpclk",
                 "/var/lib/rp1-gpclk-dkms/route-transactions",
                 "/var/lib/wsprrypi/rp1-gpclk-dkms-installation.json",
                 "/usr/src/linux-headers-$RP1_KERNEL"):
    assert evidence in collector, evidence
for evidence in ("Provider provisioning and package identity: external to WsprryPi",
                 "DKMS registration and kernel-specific module identity",
                 "WsprryPi ownership record: bounded identity whitelist",
                 "read-only inspect operation",
                 "Persisted route", "Active route and operation readiness",
                 "Qualification: not established"):
    assert evidence in collector, evidence
for guard in ("safe_root_owned_regular_file", '"$owner" == "0"',
              "! -L", "run_bounded_absolute_cmd", "--kill-after=5s 30s",
              "runtime provider absent, inaccessible, or unsafe",
              "Neither RP1 GPCLK module is loaded",
              "/proc/modules absent or inaccessible"):
    assert guard in collector, guard
for module in ("rp1_gpclk_dkms", "rp1_route_controller"):
    for field in ("filename", "version", "vermagic"):
        assert f"rp1_gpclk_modinfo_{module}_{field}" in collector
for prohibited in ("sudo ", " modprobe ", " insmod ", " rmmod ",
                   '"$RP1_RUNTIME_PROVIDER" ensure',
                   '"$RP1_RUNTIME_PROVIDER" plan'):
    assert prohibited not in collector, prohibited
for identity in ("source_commit=", "module=rp1-gpclk-dkms/",
                 "package=unreleased", "development-candidate"):
    assert identity not in collector and identity not in runtime, identity
for field in ("rp1_route_requested", "rp1_route_persisted",
              "rp1_route_configured", "rp1_route_active", "rp1_eligibility",
              "rp1_cleanup_state", "rp1_journal_state"):
    assert field in runtime and field in websocket, field
assert 'snapshot.rp1_route_active = "unavailable"' in runtime
assert 'snapshot.rp1_eligibility = "unknown"' in runtime
assert "provider.passiveSnapshot" in admin_probe
for prohibited in ("provider.acquire", "provider.release", "provider.submit", "O_RDWR"):
    assert prohibited not in admin_probe, prohibited
for evidence in ("read_only=true", "lease_token_exposed=false",
                 "indeterminate safety observation", "provider route mismatch"):
    assert evidence in admin_probe, evidence
print("rp1_gpclk_diagnostics_test passed")
