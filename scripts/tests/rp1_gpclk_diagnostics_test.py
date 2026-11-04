#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
collector = (root / "scripts/collect-support-bundle.sh").read_text()
runtime = (root / "src/scheduling.cpp").read_text()
websocket = (root / "src/web_socket_commands.cpp").read_text()
admin_probe = (root / "src/WSPR-Transmitter/src/rp1_gpclk_admin_probe.cpp").read_text()

# Execute the collector's actual RP1 dispatch block with a recording command boundary.
import os
import shlex
import subprocess
import tempfile

def shell_function(name):
    start = collector.index(name + '() {')
    return collector[start:collector.index('\n}\n', start) + 3]

with tempfile.TemporaryDirectory(prefix='rp1-diagnostic-dispatch-') as raw:
    work = Path(raw)
    provider = work / 'provider.py'; provider.write_text('# fixture\n'); provider.chmod(0o644)
    # A real root-owned regular file exercises ownership checks without chown/sudo.
    # run_cmd only records arguments; neither this file nor a provider is executed.
    safe_provider = Path('/usr/bin/true').resolve()
    assert safe_provider.stat().st_uid == 0 and safe_provider.is_file()
    qualification = safe_provider
    record = work / 'ownership.json'; record.write_text('{}')
    block_start = collector.index('RP1_KERNEL="$(uname -r')
    block = collector[block_start:collector.index('\nif command -v dpkg', block_start)]
    functions = '\n'.join(shell_function(name) for name in
                          ('run_optional_absolute_cmd', 'run_bounded_absolute_cmd', 'safe_root_owned_regular_file'))
    prelude = r'''
set -eu
run_cmd() {
  local name="$1"; shift
  printf '%s\0' "$@" > "${OUT_DIR}/commands/${name}.argv"
  : > "${OUT_DIR}/commands/${name}.txt"
}
collect_rp1_record_summary() { run_cmd rp1_gpclk_ownership_identity "$RP1_PYTHON" -I - "$RP1_INSTALLATION_RECORD"; }
ini_value() { printf '4'; }
'''
    def run_case(name, unsafe=False, unavailable=False, untrusted=False):
        out = work / name; (out/'commands').mkdir(parents=True); (out/'hardware/rp1-gpclk').mkdir(parents=True)
        provider.chmod(0o666 if unsafe else 0o644)
        env = dict(os.environ, OUT_DIR=str(out), RP1_DKMS='/usr/bin/true', RP1_MODINFO='/usr/bin/true',
                   RP1_PYTHON='/usr/bin/python3', RP1_TIMEOUT=str(work/'absent') if unavailable else '/usr/bin/timeout',
                   RP1_RUNTIME_PROVIDER=str(provider if unsafe or untrusted else safe_provider), RP1_QUALIFICATION=str(qualification),
                   RP1_INSTALLATION_RECORD=str(record), INSTALLED_INI=str(work/'config.ini'))
        subprocess.run(['bash', '-c', prelude + functions + block], check=True, env=env, timeout=10)
        calls = {p.stem: p.read_bytes().rstrip(b'\0').decode().split('\0') for p in (out/'commands').glob('*.argv')}
        assert calls['rp1_gpclk_dkms'] == ['/usr/bin/true', 'status', '-m', 'rp1-gpclk-dkms']
        kernel = os.uname().release
        for module in ('rp1_gpclk_dkms', 'rp1_route_controller'):
            for field in ('filename', 'version', 'vermagic'):
                assert calls[f'rp1_gpclk_modinfo_{module}_{field}'] == ['/usr/bin/true', '-k', kernel, '-F', field, module]
        if not unsafe and not untrusted and not unavailable:
            assert calls['rp1_gpclk_runtime_provider_inspect'] == ['/usr/bin/timeout', '--signal=TERM', '--kill-after=5s', '30s', '/usr/bin/python3', str(safe_provider), 'inspect']
        else:
            assert 'rp1_gpclk_runtime_provider_inspect' not in calls
            assert (out/'commands/rp1_gpclk_runtime_provider_inspect.txt').stat().st_size > 0
        expected = {'rp1_gpclk_dkms', 'rp1_gpclk_loaded_modules', 'rp1_gpclk_ownership_identity',
                    'rp1_gpclk_device_state', 'rp1_gpclk_route_socket', 'rp1_gpclk_route_journals'}
        expected |= {f'rp1_gpclk_modinfo_{module}_{field}' for module in ('rp1_gpclk_dkms', 'rp1_route_controller') for field in ('filename', 'version', 'vermagic')}
        if not unavailable: expected.add('rp1_gpclk_qualification_evidence')
        if not unsafe and not untrusted and not unavailable: expected.add('rp1_gpclk_runtime_provider_inspect')
        assert set(calls) == expected, set(calls) ^ expected
        assert all(not any(word in ('sudo', 'modprobe', 'insmod', 'rmmod', 'ensure', 'plan') for word in args) for args in calls.values())
        assert 'Qualification: not established' in (out/'hardware/rp1-gpclk/evidence-summary.txt').read_text()
    run_case('safe')
    run_case('unsafe', unsafe=True)
    run_case('unbounded', unavailable=True)
    if os.geteuid() != 0: run_case('untrusted-owner', untrusted=True)

# Retained static policy for hardware-facing production sources: passive diagnostics only.
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
