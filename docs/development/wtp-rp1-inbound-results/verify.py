"""Independent assertions for the retained GPIO20 acceptance artifacts."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parent
read = lambda name: json.loads((root / name).read_text())
acceptance = read('acceptance.json')
plan = read('plan.json')
assert acceptance['all_five_modes_passed']
expected = {'tone', 'qrss', 'fskcw', 'dfcw', 'wspr'}
assert set(acceptance['modes']) == expected
assert {j['mode'] for j in plan['jobs']} == expected and len(plan['jobs']) == 5
assert len({j['job_id'] for j in plan['jobs']}) == 5
boots = set()
for job in plan['jobs']:
    mode = job['mode']
    launch = read(mode + '-launch.json')
    summary = read(mode + '-summary.json')
    capture = read(mode + '-capture.json')
    result = acceptance['modes'][mode]
    assert job['job_id'] == launch['confirmation']['operation_id'] == summary['job_id'] == result['job_id']
    assert launch['confirmation']['route'] == 'GPIO20' and job['job_id'] in launch['actual_arguments']
    assert summary['hello']['body']['device_id'] == 'e045017b11e54c2fb7029f0154e6deb9'
    assert summary['caps']['body']['engine'] == 'rp1-gpclk'
    assert set(summary['caps']['body']['modes']) == expected
    clock = summary['clock']['body']
    assert clock['state'] == 'synchronized'
    assert int(clock['uncertainty_ns']) <= int(summary['caps']['body']['maximum_arm_uncertainty_ns'])
    boots.add(summary['hello']['body']['boot_id'])
    assert summary['client_host'] == 'wspr4' and summary['local_address'][0] == '192.168.1.120'
    assert summary['peer_address'] == ['192.168.1.54', 31417]
    assert summary['running_observations'] > 0 and summary['completed']['job_id'] == job['job_id']
    assert summary['load']['ok'] and summary['arm']['ok'] and summary['release']['ok']
    assert summary['after']['state'] == 'empty' and not summary['after']['output_active'] and not summary['after']['owner_id']
    assert job['capture_exit'] == capture['process_exit_code'] == 0
    assert capture['cleanup']['outcome'] == 'verified' and capture['output']['complete']
    assert result['passed'] and result['corresponding_signal_observed']
assert len(boots) == 5
assert len(read('wspr-summary.json')['events']) == 162
assert read('wspr-summary.json')['duration_ns'] == '110592000000'
final = read('final-six.json')
assert len(final) == 6 and len({d['hello']['device_id'] for d in final}) == 6
assert all(d['status']['state'] == 'empty' and not d['status']['owner_id'] and not d['status']['job_id'] and not d['status']['output_active'] for d in final)
controller = read('final-controller.json')
assert len(controller['fleet']['assignments']) == 5
assert all(not a['enabled'] and not a['in_flight'] for a in controller['fleet']['assignments'])
endpoint = controller['wtp-endpoint']
assert not any(endpoint[k] for k in ('local_requested', 'local_work_active', 'remote_owner', 'remote_output_active', 'output_unknown'))
restoration = (root / 'restoration.txt').read_text()
assert restoration.startswith('/usr/local/bin/wsprrypi\n-J\n-i\n/usr/local/etc/wsprrypi.ini\n')
assert '--rp1-development-confirmation-json' not in restoration
assert (root / 'before-hashes.txt').read_text().strip() in restoration
manifest = [{'path': p.name, 'bytes': p.stat().st_size, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
            for p in sorted(root.iterdir()) if p.is_file() and p.name != 'evidence-manifest.json']
(root / 'evidence-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print('Five exact-job remote GPIO20 modes, corresponding RF, unchanged installation/configuration and six inactive endpoints verified')
