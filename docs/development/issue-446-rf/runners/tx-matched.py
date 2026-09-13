import configparser, hashlib, json, os, pathlib, signal, socket, subprocess, sys, time
sys.path.insert(0, '/home/pi/WsprryPi-Qualification-Harness/src')
from wsprrypi_qualification.bounded_tone_control import BoundedToneEndpoint, run_bounded_tone_transaction

freq = int(sys.argv[1])
assert freq in (7040100, 14097100)
root = pathlib.Path('/home/pi/issue446-rf') / 'matched'
root.mkdir(parents=True, exist_ok=True)
# A failed attempt consumes its slot; reruns require a new operator decision.
with (root / 'claimed').open('x') as f: f.write(str(time.time()))
binary = pathlib.Path('/usr/local/bin/wsprrypi')
assert hashlib.sha256(binary.read_bytes()).hexdigest() == 'b21fa9200b0671232c88b28595ef591f96825dd8a60f1e22e6a6d516faccc674'
installed = pathlib.Path('/usr/local/etc/wsprrypi.ini')
before = installed.read_bytes()
cfg = configparser.ConfigParser(strict=False, interpolation=None)
cfg.optionxform = str
cfg.read_string(before.decode())
assert cfg['Operation']['Transmit'].lower() == 'false'
assert cfg['Operation']['Enable on Boot'] == 'Never'
cfg['Operation'].update({'Transmit Backend': 'gpio', 'Transmit': 'false', 'Enable on Boot': 'Never', 'Use LED': 'false', 'Use Amp': 'false', 'Use Shutdown': 'false'})
cfg['GPIO'].update({'Transmit Pin': '4', 'Power Level': '0', 'Use System Clock Frequency Estimate': 'false', 'Frequency Residual PPM': '0.0', 'Manual PPM': '0.0'})
for key in cfg['Band GPIO']:
    cfg['Band GPIO'][key] = 'false' if key.endswith('Active High') else ''
ini = root / 'test.ini'
with ini.open('w') as f: cfg.write(f)
def command(args):
    return subprocess.run(args, check=True, text=True, capture_output=True, timeout=20).stdout.strip()
report = {'frequency_hz': freq, 'duration_ms_each': 5000, 'maximum_total_rf_ms': 20000, 'manual_ppm': 0.0, 'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(), 'source_commit': command(['git','-C','/home/pi/WsprryPi','rev-parse','HEAD']), 'config_before_sha256': hashlib.sha256(before).hexdigest(), 'events': []}
def event(name, value):
    report['events'].append({'event': name, 'time_unix': time.time(), 'value': value})
    (root/'result.json').write_text(json.dumps(report, indent=2))
    print(name, flush=True)
proc = None
restore = False
success = False
try:
    initial = command(['pinctrl','get','4']); event('gpio_before', initial)
    assert ' ip ' in initial
    assert command(['systemctl','is-active','wsprrypi.service']) == 'active'
    command(['systemctl','stop','wsprrypi.service']); restore = True
    args = [str(binary), '-i', str(ini), '--backend','gpio','--transmit-gpio','4','--gpio-power-level','0','--no-system-clock-frequency-estimate','--gpio-manual-ppm','0','--no-led','--no-shutdown','--no-amp-pin','--no-http','--socket-loopback-only','--socket-loopback-family','ipv4','--socket-port','41726','--debug-logging','--date-time-log']
    control = pathlib.Path('/home/pi/issue446-rf/control-source/src/build/bin/wsprrypi')
    control_hash = hashlib.sha256(control.read_bytes()).hexdigest()
    expected_control_hash = (root.parent/'control.sha256').read_text().split()[0]
    assert control_hash == expected_control_hash
    event('control_sha256', control_hash)
    for index, kind in enumerate(('old','new','new','old')):
        selected = str(control) if kind == 'old' else str(binary)
        cycle_args = [selected] + args[1:]
        event('arguments-'+str(index), cycle_args)
        with (root/('transmitter-'+str(index)+'-'+kind+'.log')).open('w') as log:
            proc = subprocess.Popen(cycle_args, cwd=root, stdout=log, stderr=subprocess.STDOUT)
            deadline=time.monotonic()+8
            while True:
                assert proc.poll() is None
                try:
                    with socket.create_connection(('127.0.0.1',41726),timeout=.2): break
                except OSError:
                    if time.monotonic()>deadline: raise RuntimeError('startup deadline')
                    time.sleep(.1)
            event('request_begin-'+str(index),kind)
            result=run_bounded_tone_transaction(BoundedToneEndpoint('127.0.0.1',41726),request_id='issue446-matched-'+str(index),frequency_hz=freq,duration_ms=5000,outer_timeout_s=15)
            event('bounded_tone-'+str(index),result)
            proc.send_signal(signal.SIGINT)
            code=proc.wait(timeout=10)
            event('process_exit-'+str(index),code)
            assert code==0
            proc=None
            idle=command(['pinctrl','get','4']);event('gpio_after-'+str(index),idle)
            assert ' ip ' in idle
            time.sleep(1)
    success=True
except BaseException as exc:
    event('failure', repr(exc))
finally:
    if proc is not None:
        if proc.poll() is None: proc.send_signal(signal.SIGINT)
        try: event('process_exit', proc.wait(timeout=10))
        except subprocess.TimeoutExpired:
            proc.kill(); proc.wait(); success=False; event('forced_kill', True)
    idle = command(['pinctrl','get','4']); event('gpio_after', idle)
    success = success and ' ip ' in idle
    if restore:
        command(['systemctl','start','wsprrypi.service'])
        event('service_restored',command(['systemctl','is-active','wsprrypi.service']))
    event('installed_config_unchanged', installed.read_bytes() == before)
    success = success and installed.read_bytes() == before
    report['passed_lifecycle'] = success
    (root/'result.json').write_text(json.dumps(report, indent=2))
raise SystemExit(0 if success else 1)
