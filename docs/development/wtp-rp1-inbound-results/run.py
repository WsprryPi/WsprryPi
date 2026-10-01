"""Authorized finite GPIO20 campaign. Run on the Mac with SSH to both Pis.

Each WTP job receives its own exact-job RP1 launch confirmation. This harness
does not install binaries, change INI values, or start the central scheduler.
"""
import json
import subprocess
import sys
import time
import uuid
from pathlib import Path

stage = sys.argv[1]
out = Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
dropin = '/etc/systemd/system/wsprrypi.service.d/99-rp1-inbound-acceptance.conf'
capture_helper = '/home/pi/.cache/wsprrypi-qualification/native-v2/82562c1b937ba98816eb3ae1d27aa270729b35be862b1cf8d87d00b497b95438/wspq-capture-soapy'
def ssh(host, command, timeout=60):
    result = subprocess.run(['ssh', '-o', 'BatchMode=yes', host, command], capture_output=True, text=True, timeout=timeout)
    if result.returncode:
        raise RuntimeError(result.stderr + result.stdout)
    return result.stdout
plan = {'stage': stage, 'route': 'GPIO20', 'target': 'wspr5', 'client': 'wspr4',
        'frequency_hz': 14097100, 'jobs': [], 'started_utc_ns': str(time.time_ns())}
(out / 'plan.json').write_text(json.dumps(plan, indent=2) + '\n')
before_hashes = ssh('wspr5', f'test ! -e {dropin} && sha256sum /usr/local/bin/wsprrypi /usr/local/lib/wsprrypi/route_application.py && sudo sha256sum /usr/local/etc/wsprrypi.ini /usr/local/etc/wsprrypi.ini.wtp-assignments.json')
(out / 'before-hashes.txt').write_text(before_hashes)
try:
    for mode in ('tone', 'qrss', 'fskcw', 'dfcw', 'wspr'):
        job = uuid.uuid4().hex
        confirmation = dict(enabled=True, route='GPIO20', operation_id=job,
            physical_connection_confirmed=True, attenuation_and_load_confirmed=True,
            bounded_operation_confirmed=True, non_radiating_topology_confirmed=True,
            experimental_status_acknowledged=True)
        args_json = json.dumps(confirmation, separators=(',', ':'))
        unit = '[Service]\nExecStart=\nExecStart=/usr/local/bin/wsprrypi -J -i /usr/local/etc/wsprrypi.ini --rp1-development-confirmation-json ' + "'" + args_json + "'\n"
        # All interpolated values are fixed paths, fixed modes, UUID hex or JSON
        # generated here. The remote Python literal avoids shell JSON expansion.
        command = f"python3 - <<'PY'\nfrom pathlib import Path\nPath('{stage}/{mode}-dropin.conf').write_text({unit!r})\nPY\nsudo install -m 0644 {stage}/{mode}-dropin.conf {dropin}\nsudo systemctl daemon-reload\nsudo systemctl restart wsprrypi\npid=$(systemctl show wsprrypi -p MainPID --value)\nsudo cat /proc/$pid/cmdline | tr '\\0' '\\n'"
        actual = ssh('wspr5', command, timeout=80)
        assert job in actual and 'GPIO20' in actual, actual
        (out / (mode + '-launch.json')).write_text(json.dumps({'confirmation': confirmation, 'actual_arguments': actual}, indent=2) + '\n')
        duration = 110.592 if mode == 'wspr' else (10 if mode == 'tone' else 5)
        count = int((duration + 20) * 250000)
        capture = [capture_helper, '--enable-physical-sdr', 'sdrplay', '2404058C60', '14075100', str(count), '20', '250000', '200000', '0', 'false', 'false', '500000', str(int(duration + 35)), f'{stage}/{mode}.cf32', f'{stage}/{mode}-capture.json', f'rp1-inbound-{mode}']
        remote = ' '.join(capture)
        log = (out / (mode + '-capture.log')).open('w')
        receiver = subprocess.Popen(['ssh', '-o', 'BatchMode=yes', 'wspr5', remote], stdout=log, stderr=subprocess.STDOUT)
        try:
            time.sleep(3)
            assert receiver.poll() is None, 'SDR exited before job submission'
            reply = ssh('wspr4', f'python3 -u {stage}/client.py {stage} {mode} {job}', timeout=duration + 30)
            print(reply.strip(), flush=True)
            rc = receiver.wait(timeout=duration + 40)
            assert rc == 0, f'SDR exit {rc}'
        finally:
            if receiver.poll() is None:
                receiver.terminate()
                receiver.wait(timeout=10)
            log.close()
        plan['jobs'].append({'mode': mode, 'job_id': job, 'capture_exit': rc, 'capture_command': capture})
        (out / 'plan.json').write_text(json.dumps(plan, indent=2) + '\n')
finally:
    restored = ssh('wspr5', f'set -eu\nsudo rm -f {dropin}\nsudo systemctl daemon-reload\nsudo systemctl restart wsprrypi\npid=$(systemctl show wsprrypi -p MainPID --value)\nsudo cat /proc/$pid/cmdline | tr "\\0" "\\n"\nsha256sum /usr/local/bin/wsprrypi /usr/local/lib/wsprrypi/route_application.py\nsudo sha256sum /usr/local/etc/wsprrypi.ini /usr/local/etc/wsprrypi.ini.wtp-assignments.json', timeout=80)
    (out / 'restoration.txt').write_text(restored)
    assert '--rp1-development-confirmation-json' not in restored
    assert before_hashes.strip() in restored
    print('Canonical managed service restored', flush=True)
