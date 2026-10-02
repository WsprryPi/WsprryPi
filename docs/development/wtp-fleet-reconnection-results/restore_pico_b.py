"""Restore the exact retained Pico B image after paused, unowned Fleet cleanup.

Private console details and flash reads stay on wspr5 outside Git. No jobs,
settings, provisioning or reserved-sector writes are issued by this helper.
"""
import hashlib
import json
import os
import subprocess
import time
from pathlib import Path

os.umask(0o077)
root = Path('/home/pi/wtp-fleet-pico-b-20261001')
out = Path('/home/pi/wtp-fleet-reconnection-20261001/pico-b-restoration')
out.mkdir(mode=0o700, exist_ok=False)
serial, device = 'CDDBF8767C506C07', '29f20b7342051ef947aa56cb9d4fab42'
image = root / 'restore-615888e5364b.uf2'
digest = '81361b105def84231c23853507bad81f992426260b9c935061fab82081d5239f'
assert hashlib.sha256(image.read_bytes()).hexdigest() == digest
base = json.loads((root / 'base-info.json').read_text())
reserved = Path('/home/pi/wtp-fleet-reconnection-20261001/pico-b-switch/before-load-reserved.bin').read_bytes()
port = '/dev/serial/by-id/usb-WsprryPi_WsprryPico_' + serial + '-if00'
picotool = '/home/pi/phase11-4-e1/picotool-build/picotool'
client = '/home/pi/phase11-4-e1/scripts/standalone_console.py'
deadline = time.monotonic() + 180

def run(args, name, timeout=60):
    assert time.monotonic() < deadline
    result = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
    (out / name).write_text(result.stdout + result.stderr)
    assert result.returncode == 0, (name, result.returncode)
    return result.stdout

def console(action, revision, name):
    return json.loads(run(['python3', client, action, '--port', port,
        '--device-id', device, '--revision', revision, '--run'], name, 20))

def read_reserved(name):
    path = out / (name + '.bin')
    run(['sudo', '-n', picotool, 'save', '-r', '0x103f2000', '0x10400000',
         '-v', str(path), '-t', 'bin', '--ser', serial], name + '.log')
    run(['sudo', '-n', 'chown', f'{os.getuid()}:{os.getgid()}', str(path)], name + '-owner.log')
    data = path.read_bytes()
    assert len(data) == 57344 and data == reserved, 'Reserved provisioning bytes changed'
    return data

before = console('info', '3e1337074003', 'before-info.json')
assert before['device_id'] == device and before['status']['state'] == 'empty'
assert not before['status']['output_active'] and not before['status']['enabled']
console('bootsel', '3e1337074003', 'enter-rom.json')
time.sleep(2)
in_rom = True
try:
    read_reserved('before-restore-reserved')
    run(['sudo', '-n', picotool, 'load', '-v', str(image), '--ser', serial], 'load.log')
    read_reserved('after-restore-reserved')
    run(['sudo', '-n', picotool, 'reboot', '-a', '--ser', serial], 'reboot.log')
    in_rom = False
finally:
    if in_rom:
        run(['sudo', '-n', picotool, 'reboot', '-a', '--ser', serial], 'failure-reboot.log')
end = time.monotonic() + 25
while not Path(port).exists():
    assert time.monotonic() < end, 'Pico application did not re-enumerate'
    time.sleep(.25)
time.sleep(3)
after = console('info', '615888e5364b', 'after-info.json')
assert after['device_id'] == device and after['revision'] == base['revision'] == '615888e5364b'
assert after['system_clock_hz'] == 150000000
assert after['status']['engine'] == 'inhibited-standalone-simulator'
assert after['status']['state'] == 'empty' and not after['status']['output_active']
for key in ('provisioning_source', 'provisioning_generation', 'access_state', 'access_generation'):
    assert after[key] == base[key], key
for key in ('configured', 'enabled', 'expires_utc_s', 'station', 'schedules',
            'schedule_base_frequency_nhz', 'watermark_utc_ns'):
    assert after['status'][key] == base['status'][key], key
assert after.get('saved_consumer_profile') == base.get('saved_consumer_profile')
result = {'device_id': device, 'serial': serial, 'restored_revision': after['revision'],
    'restored_uf2_sha256': digest, 'reserved_bytes_identical': len(reserved),
    'boot_id': after['status']['boot_id'], 'engine': after['status']['engine'],
    'output_active': after['status']['output_active'], 'profile_preserved': True}
(out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
