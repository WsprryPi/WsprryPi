"""Bounded acceptance observer/cleanup guard; UI enables remote assignments."""
import json, os, signal, subprocess, sys, time, urllib.request
from pathlib import Path

stage = Path(sys.argv[1])
slot = int(sys.argv[2])
base = 'http://127.0.0.1:31415'
stopping = False

def http(path, method='GET', body=None, etag=None):
    headers = {'Origin': base, 'X-WsprryPico-Request': '1'}
    if body is not None:
        headers['Content-Type'] = 'application/json'
    if etag:
        headers['If-Match'] = etag
    req = urllib.request.Request(base + path, data=None if body is None else json.dumps(body).encode(), headers=headers, method=method)
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.load(r), r.headers.get('ETag')

def local_enable(value):
    _, etag = http('/api/v1/host/config')
    return http('/api/v1/host/config', 'PUT', {'Operation': {'Transmit': value}}, etag)[0]

def pause_all():
    errors = []
    for attempt in range(3):
        try:
            local_enable(False)
            fleet, _ = http('/api/v1/host/fleet')
            for a in fleet['assignments']:
                if not a['enabled'] and not a['in_flight']:
                    continue
                _, etag = http('/api/v1/host/fleet')
                http('/api/v1/host/fleet', 'POST', {'operation':'pause','device_id':a['device_id']}, etag)
            return errors
        except Exception as e:
            errors.append(repr(e))
            time.sleep(1)
    return errors

def interrupted(signum, frame):
    global stopping
    stopping = True

for s in (signal.SIGINT, signal.SIGTERM):
    signal.signal(s, interrupted)

started = time.time()
manifest = {'slot_utc_seconds':slot,'slots':[slot,slot+120,slot+240], 'local_stop_utc_seconds':slot+114,'remote_stop_utc_seconds':slot+255,'capture_center_hz':14075100,'sample_rate':250000,'stage':str(stage)}
(stage/'run-plan.json').write_text(json.dumps(manifest,indent=2)+'\n')
capture = None
local_started = False
local_stopped = False
try:
    seconds = int(slot + 269 - time.time())
    assert 270 <= seconds <= 500, seconds
    helper = '/home/pi/.cache/wsprrypi-qualification/native-v2/82562c1b937ba98816eb3ae1d27aa270729b35be862b1cf8d87d00b497b95438/wspq-capture-soapy'
    args = [helper,'--enable-physical-sdr','sdrplay','2404058C60','14075100',str(seconds*250000),'20','250000','200000','0','false','false','500000',str(seconds+15),str(stage/'capture.cf32'),str(stage/'capture.json'),'concurrent-fleet-first-group']
    (stage/'capture-command.json').write_text(json.dumps(args,indent=2)+'\n')
    log = (stage/'capture.log').open('w')
    capture = subprocess.Popen(args,stdout=log,stderr=subprocess.STDOUT)
    (stage/'observer-ready').write_text(str(os.getpid()))
    with (stage/'observations.jsonl').open('w',buffering=1) as out:
        while time.time() < slot+255 and not stopping:
            now = time.time()
            if capture.poll() is not None:
                raise RuntimeError('Receiver stopped before test completion: '+str(capture.returncode))
            if (stage/'go-local').exists() and not local_started:
                if now >= slot-10:
                    raise RuntimeError('Local enable missed preparation deadline')
                local_enable(True)
                local_started = True
                (stage/'local-enabled').write_text(str(time.time_ns()))
            if now >= slot+114 and not local_stopped:
                local_enable(False)
                local_stopped = True
                (stage/'local-disabled').write_text(str(time.time_ns()))
            try:
                fleet, _ = http('/api/v1/host/fleet')
                endpoint, _ = http('/api/v1/host/wtp-endpoint')
                out.write(json.dumps({'utc_ns':str(time.time_ns()),'fleet':fleet,'local_endpoint':endpoint})+'\n')
            except Exception as e:
                out.write(json.dumps({'utc_ns':str(time.time_ns()),'error':repr(e)})+'\n')
            time.sleep(1)
except Exception as e:
    (stage/'runner-error.json').write_text(json.dumps({'error':repr(e),'utc_ns':str(time.time_ns())})+'\n')
finally:
    errors = pause_all()
    (stage/'cleanup-errors.json').write_text(json.dumps(errors,indent=2)+'\n')
    (stage/'outputs-stopped').write_text(str(time.time_ns()))
    if capture is not None:
        try:
            capture.wait(timeout=max(1,slot+300-time.time()))
        except subprocess.TimeoutExpired:
            capture.terminate()
            try: capture.wait(timeout=5)
            except subprocess.TimeoutExpired: capture.kill(); capture.wait(timeout=5)
        (stage/'capture-exit.json').write_text(json.dumps({'exit_code':capture.returncode})+'\n')
    try:
        fleet, _ = http('/api/v1/host/fleet')
        endpoint, _ = http('/api/v1/host/wtp-endpoint')
        (stage/'final-controller.json').write_text(json.dumps({'fleet':fleet,'local_endpoint':endpoint},indent=2)+'\n')
    except Exception as e:
        (stage/'final-controller-error.json').write_text(json.dumps({'error':repr(e)})+'\n')
    journal = subprocess.run(['journalctl','-u','wsprrypi','-u','wsprrypi-fleet-controller-test','--since','@'+str(int(started)-30),'--no-pager','-o','short-iso-precise'],capture_output=True,text=True)
    (stage/'controller-journal.log').write_text(journal.stdout)
    (stage/'observer-finished').write_text(str(time.time_ns()))
