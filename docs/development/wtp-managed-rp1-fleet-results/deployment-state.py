"""Read only the public deployed state and selected file identities."""
import hashlib
import configparser
import json
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

stage = Path(sys.argv[1])
source = stage / 'source'
base = 'http://127.0.0.1:31415/api/v1/host/'

def get(resource):
    with urllib.request.urlopen(base + resource, timeout=10) as response:
        return json.load(response)

manifest = json.loads((source / 'docs/development/wtp-managed-rp1-fleet-results/source-manifest.json').read_text())
assert all(hashlib.sha256((source / p).read_bytes()).hexdigest() == h
           for p, h in manifest['sha256'].items())
pid = subprocess.check_output(['systemctl', 'show', '-p', 'MainPID', '--value', 'wsprrypi.service'], text=True).strip()
arguments = Path('/proc/' + pid + '/cmdline').read_bytes().decode().split('\0')[:-1]
fleet = get('fleet')
endpoint = get('wtp-endpoint')
config = get('config')['config']
discovery = get('discovery')
ini = configparser.ConfigParser()
ini.read('/usr/local/etc/wsprrypi.ini')
assert not endpoint['local_requested'] and not endpoint['local_work_active'] and not endpoint['output_unknown']
assert all(not a['enabled'] and not a['in_flight'] for a in fleet['assignments'])
assert endpoint['dns_sd_published'] and discovery['available']
assert arguments == ['/usr/local/bin/wsprrypi', '-J', '-i', '/usr/local/etc/wsprrypi.ini']
result = {'utc_ns': str(time.time_ns()), 'service_active': subprocess.check_output(
    ['systemctl', 'is-active', 'wsprrypi.service'], text=True).strip(),
    'arguments': arguments, 'temporary_confirmation_removed': True,
    'source_hashes_match_native_stage': True, 'binary_sha256': hashlib.sha256(
        Path('/usr/local/bin/wsprrypi').read_bytes()).hexdigest(),
    'companion_sha256': hashlib.sha256(Path('/usr/local/lib/wsprrypi/route_application.py').read_bytes()).hexdigest(),
    'config': {k: config[k] for k in ('Operation', 'GPIO', 'WSPR', 'WTP Server')},
    'fleet_test_frequency_override': ini.getboolean('Experimental', 'Allow Unqualified Frequency'),
    'endpoint': endpoint, 'fleet': fleet, 'discovery': discovery,
    'rf_acceptance': 'Final native-frame and second receiver-group pass pending stopped combiner rotation'}
(stage / 'final-state.json').write_text(json.dumps(result, indent=2) + '\n')
print('Canonical service active, five assignments paused, local off, DNS-SD available')
