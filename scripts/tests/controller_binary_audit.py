#!/usr/bin/env python3
"""Audit the linked production controller, including safe early CLI exits."""
from pathlib import Path
import subprocess
import sys


def main():
    binary = Path(sys.argv[1]).resolve(strict=True)
    symbols = subprocess.check_output(['nm', '-C', str(binary)], text=True)
    assert 'controller_profile_entry' in symbols, 'Missing production profile admission symbol'
    for forbidden in ('wsprTransmitter', 'WsprTransmitter::WsprTransmitter',
                      'wsprrypi::(anonymous namespace)::Runtime::Runtime',
                      'Rp1PpsCalibration::Impl::Capture', 'JobService::JobService',
                      'WsprRpiBackend::WsprRpiBackend', 'SimulatedTransmitBackend::SimulatedTransmitBackend',
                      'pthread_setaffinity_np', 'MonitorFile::', 'iniMonitor', 'ppmManager', 'PPMManager::',
                      'SupportBundleCollectorExecutor::', 'SupportBundleJobManager::create(', 'SupportBundleRuntime::create_production(', 'getopt_long'):
        assert forbidden not in symbols, forbidden
    link_map = Path(str(binary) + '.map').read_text(errors='surrogateescape')
    for required in ('/transmitter_runtime_bridge_controller.o', '/arg_parser_runtime_controller.o',
                     '/controller_local_hardware_unavailable.o'):
        assert required in link_map, required
    for forbidden in ('/wspr_transmit.o', '/mailbox.o', '/mailbox_revision.o',
                      '/rp1_pps_calibration.o', '/rp1_gpclk_route_service.o',
                      '/job_service.o', '/tone_engine.o', '/thread_affinity.o', '/monitorfile.o', '/ppm_manager.o', '/support_bundle_'):
        assert forbidden not in link_map, forbidden
    for options, expected in ((['--help'], 0), (['--version'], 0), (['--list-backends'], 0), ([], 2)):
        result = subprocess.run([str(binary), *options], capture_output=True, text=True, timeout=5)
        assert result.returncode == expected, (options, result)
        if options == ['--version']:
            assert 'Build profile: controller' in result.stdout
            assert 'Compiled backends: none' in result.stdout
            assert 'Ancillary GPIO: disabled' in result.stdout
        if options == ['--list-backends']:
            assert result.stdout == '\n'
    print('PASS production controller link map, forbidden runtime symbols, zero-output metadata, and safe early CLI')


if __name__ == '__main__':
    main()
