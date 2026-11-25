#!/usr/bin/env python3
"""Read-only production Make admission and controller profile isolation checks."""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def plan(*arguments, goal='debug'):
    environment = dict(os.environ)
    for key in ('MAKEFLAGS', 'MFLAGS', 'MAKELEVEL', 'BUILD_PROFILE', 'BACKENDS', 'ANCILLARY_GPIO', 'SI5351_TRANSPORT'):
        environment.pop(key, None)
    return subprocess.run(['make', '--no-print-directory', '-B', '-n', goal, 'SUDO=', *arguments],
                          cwd=ROOT / 'src', env=environment, capture_output=True, text=True, timeout=30)


def main():
    admitted = plan('BUILD_PROFILE=controller')
    assert admitted.returncode == 0, admitted.stderr
    assert 'build/obj/debug/controller/' in admitted.stdout
    assert str(ROOT / 'src/build/generated/controller') in admitted.stdout
    assert 'build/bin/profiles/controller/wsprrypi_debug' in admitted.stdout
    for forbidden in ('/wspr_transmit.cpp', '/rp1_pps_calibration.cpp', '/rp1_gpclk_route_service.cpp',
                      '/job_service.cpp', '/tone_engine.cpp', '/thread_affinity.cpp', '/mailbox.cpp', 'MonitorFile', '/ppm_manager.cpp', '/support_bundle_'):
        assert forbidden not in admitted.stdout, forbidden
    assert 'transmitter_runtime_bridge_controller.cpp' in admitted.stdout
    assert 'cp build/bin/profiles/controller/wsprrypi_debug build/bin/wsprrypi_debug' not in admitted.stdout
    assert 'build/bin/profiles/controller/BOTAN-LICENSE.txt' in admitted.stdout
    assert '-lgpiod' not in admitted.stdout
    for arguments, error in (
        (('BUILD_PROFILE=unknown',), 'Unknown BUILD_PROFILE'),
        (('BUILD_PROFILE=%',), 'Unknown BUILD_PROFILE'),
        (('BUILD_PROFILE=controller transmitter',), 'Select exactly one BUILD_PROFILE'),
        (('BUILD_PROFILE=controller', 'BACKENDS=simulated'), 'cannot select local BACKENDS'),
        (('BUILD_PROFILE=controller', 'ANCILLARY_GPIO=1'), 'requires ANCILLARY_GPIO=0'),
        (('BUILD_PROFILE=controller', 'SI5351_TRANSPORT=linux-i2c'), 'cannot select SI5351_TRANSPORT'),
    ):
        refused = plan(*arguments)
        assert refused.returncode != 0 and error in refused.stderr, (arguments, refused.stdout, refused.stderr)
        assert 'Compiling' not in refused.stdout
    for goal in ('test', 'test-tone', 'test-oneshot', 'gdb', 'install', 'install-binary',
                 'debuginstall', 'uninstall', 'install-pairing-notices'):
        refused = plan('BUILD_PROFILE=controller', goal=goal)
        assert refused.returncode != 0 and 'does not support legacy transmitter run/install targets' in refused.stderr
        assert not refused.stdout, (goal, refused.stdout)
    ordinary = plan('BACKENDS=simulated', 'ANCILLARY_GPIO=0')
    assert ordinary.returncode == 0, ordinary.stderr
    assert 'MonitorFile' not in ordinary.stdout and '/monitorfile.cpp' not in ordinary.stdout
    for arguments, goal, profile in (
        (('BUILD_PROFILE=controller',), 'controller-only-test', 'controller'),
        (('BACKENDS=simulated', 'ANCILLARY_GPIO=0'), 'sqlite-state-entry-test', 'simulated+ancillary-gpio0'),
    ):
        fixture = plan(*arguments, goal=goal)
        assert fixture.returncode == 0, fixture.stderr
        assert f'build/bin/profiles/{profile}/controller_only_production_fixture' in fixture.stdout
        assert 'build/bin/controller_only_production_fixture' not in fixture.stdout
    print('PASS controller build admission, private objects/headers/artifacts/notices, and no physical GPIO link dependency')


if __name__ == '__main__':
    main()
