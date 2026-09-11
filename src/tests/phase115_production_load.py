#!/usr/bin/env python3
"""Opt-in isolated actual-production/browser load; never submits RF jobs.

Run inside the approved client network/mount namespace. The production program
owns its WTP connection and ordinary scheduler policy. This worker observes its
host API and issues manual-equivalent authenticated Pico page/asset/status GETs.
USB and host-health observers are independent campaign workers.
"""
import argparse
import configparser
import contextlib
import fcntl
import hashlib
import http.client
import json
import os
from pathlib import Path
import signal
import socket
import ssl
import subprocess
import threading
import time
import urllib.request

from phase115_tls_observer_test import decode

DEVICE='fd6127d11d6aca42a9905fa3fb1bf1d5'
NAME='wsprrypico-0a60df.local'
PEER_SHA='06496fe4d7a1ab45791d85cb0797fa55f76b8dc7ee931f9c7fa70823fef46016'


def require(value,message):
    if not value: raise ValueError(message)


def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_ini(path):
    config=configparser.ConfigParser(interpolation=None,strict=True)
    # Match the application parser: canonical INI keys are case-sensitive.
    config.optionxform=str
    require(config.read(path)==[str(path)],'INI unavailable')
    require(not config.defaults(),'INI DEFAULT values cannot stand in for explicit settings')
    require(all(config.get('Operation',key).lower()=='false'
                for key in ('Transmit','Use LED','Use Amp','Use Shutdown')) and
            config.get('Operation','Enable on Boot')=='Never' and
            config.get('Operation','Transmit Backend')=='wtp','Ancillary/output policy')
    require(config.get('WTP','Transport')=='network' and config.get('WTP','Hostname')==NAME and
            config.get('WTP','TLS Server Identity')==NAME and config.get('WTP','Device ID')==DEVICE and
            config.getint('WTP','TCP Port')==18443 and not config.get('WTP','Endpoint') and
            config.get('WTP','Allow Frequency Adjustment').lower()=='true' and
            config.getint('WTP','Start Uncertainty ns')==500000000,'WTP admission')
    require(config.getint('Operation','Web Port')==31425 and
            config.getint('Operation','Socket Port')==31426,'Isolated host ports')
    require(all(not value.strip() for key,value in config['Band GPIO'].items()
                if 'active high' not in key.lower()),'GPIO selectors must be empty')
    require(config.getfloat('Calibration','PPM')==0 and
            config.get('GPIO','Use System Clock Frequency Estimate').lower()=='false' and
            config.getfloat('GPIO','Frequency Residual PPM')==0 and
            config.getfloat('GPIO','Manual PPM')==0,'Correction must remain zero')
    require(config.getboolean('Experimental','Allow Unqualified Frequency') and
            config.getboolean('Experimental','Allow Non-Amateur Frequency'),
            '135.5 kHz fixture policy must be explicit')


def browser_schedule(start,end,stop,get,clock=time.monotonic,grant_control=None):
    """Preserve 0.2 Hz status while spreading each reload's assets over its next slots."""
    status_at=page_at=start;pages=[];asset_budget=0.0
    while clock()<end and not stop.is_set():
        now=clock()
        if now>=page_at:
            require(not pages,'Previous browser page reload incomplete')
            pages.extend(('/', '/style.css', '/app.js'));page_at+=30
        if now>=status_at:
            require(now-status_at<=1,'Browser nominal sampling fell behind')
            get('/api/v1/status');status_at+=5
            slots_left=round((page_at-status_at)/5)+1
            # A completed status may already have consumed the next polling slot.
            # Keep its due successor ahead of assets or control; do not drop either.
            granted=(grant_control is not None and clock()<status_at and
                     len(pages)<slots_left and grant_control(status_at))
            # Do not create sampling debt by starting an asset that its measured
            # cost predicts will exceed the existing one-second status allowance. All assets must
            # still finish in their original thirty-second reload window.
            if pages and not granted and clock()<status_at and clock()+asset_budget<=status_at+1:
                asset_start=clock();get(pages.pop(0))
                asset_budget=max(asset_budget,clock()-asset_start)
        stop.wait(max(0,min(.1,status_at-clock(),end-clock())))
    require(not pages,'Final browser page reload incomplete')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--seconds',type=int,required=True)
    parser.add_argument('--browser',action='store_true')
    parser.add_argument('--boot',required=True)
    parser.add_argument('--run',action='store_true')
    args=parser.parse_args()
    require(1<=args.seconds<=1800,'Declared load interval must be at most 30 minutes')
    if not args.run:
        print('Plan only; no process, device, namespace or network accessed.');return
    root=args.root.resolve(strict=True)
    require(root.is_dir() and root.stat().st_mode & 0o077==0,'Private load directory required')
    os.umask(0o077)
    plan=json.loads((root/'load.json').read_text())
    require(plan['boot_id']==args.boot and type(plan['seconds']) is int and plan['seconds']==args.seconds and
            plan['browser'] is args.browser and plan['device_id']==DEVICE,'Frozen workload differs')
    require(plan['address'] in ('10.77.15.10','10.77.15.20'),'Isolated DUT address')
    require(os.readlink('/proc/self/ns/net')==plan['netns'] and
            os.readlink('/proc/self/ns/mnt')==plan['mountns'],'Wrong client namespaces')
    binary,ini,observer=[Path(plan[key]) for key in ('binary','ini','observer')]
    require(digest(binary)==plan['binary_sha256'] and digest(ini)==plan['ini_sha256'] and
            digest(observer)==plan['observer_sha256'],'Application/input identity changed')
    validate_ini(ini)
    ctx=ssl.create_default_context(cafile=plan['ca'])
    ctx.minimum_version=ctx.maximum_version=ssl.TLSVersion.TLSv1_3
    ctx.load_cert_chain(plan['browser_cert'],plan['browser_key']);ctx.set_alpn_protocols(['http/1.1'])
    lock=threading.Lock(); stop=threading.Event(); failures=[]; sequence=0
    log=(root/'load-events.jsonl').open('x')
    def note(kind,value):
        nonlocal sequence
        with lock:
            log.write(json.dumps(dict(sequence=sequence,kind=kind,value=value,
                utc_ns=time.time_ns(),monotonic_ns=time.monotonic_ns()))+'\n')
            log.flush();os.fsync(log.fileno());sequence+=1
    def interrupted(signum,frame):
        failures.append('interrupted '+str(signum));stop.set()
    signal.signal(signal.SIGTERM,interrupted);signal.signal(signal.SIGINT,interrupted)
    opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
    def host_status():
        began=time.monotonic_ns()
        with opener.open('http://127.0.0.1:31425/api/v1/status',timeout=3) as response:
            data=response.read(131073);require(len(data)<=131072,'Oversized host status')
            value=json.loads(data)
        note('production_host_status',dict(began_monotonic_ns=began,value=value))
        return value
    @contextlib.contextmanager
    def browser_lane():
        if 'browser_lock' not in plan:
            yield;return
        path=Path(plan['browser_lock'])
        require(path==root/'browser.lock','Shared browser lane path')
        with path.open('a') as lane:
            deadline=time.monotonic()+1
            while True:
                try: fcntl.flock(lane,fcntl.LOCK_EX|fcntl.LOCK_NB);break
                except BlockingIOError:
                    require(time.monotonic()<deadline,'Browser sampling lane delayed')
                    time.sleep(.01)
            yield
    def pico_get(path):
        with browser_lane(): pico_get_locked(path)
    def pico_get_locked(path):
        began=time.monotonic_ns();end=time.monotonic()+15
        raw=socket.create_connection((plan['address'],18443),timeout=15)
        try:
            raw.settimeout(max(.001,end-time.monotonic()))
            with ctx.wrap_socket(raw,server_hostname=NAME) as stream:
                require(stream.selected_alpn_protocol()=='http/1.1' and
                        hashlib.sha256(stream.getpeercert(binary_form=True)).hexdigest()==PEER_SHA,
                        'Browser TLS identity')
                def expire():
                    try: stream.shutdown(socket.SHUT_RDWR)
                    except OSError: pass
                timer=threading.Timer(max(.001,end-time.monotonic()),expire)
                timer.daemon=True;timer.start()
                try:
                    stream.settimeout(max(.001,end-time.monotonic()))
                    stream.sendall(f'GET {path} HTTP/1.1\r\nHost: {NAME}:18443\r\nConnection: close\r\n\r\n'.encode())
                    response=http.client.HTTPResponse(stream);response.begin()
                    body=response.read(131073);response.close()
                finally:
                    timer.cancel();timer.join(1)
                require(response.status==200 and len(body)<=131072 and time.monotonic()<=end,
                        'Browser HTTP status/size/deadline')
                if path=='/api/v1/status':
                    value=json.loads(body);require(value['job']['boot_id']==args.boot,'Browser boot changed')
                note('browser_get',dict(path=path,began_monotonic_ns=began,status=response.status,
                                       body_hex=body.hex(),peer_sha256=PEER_SHA))
        finally: raw.close()
    def grant_control(until):
        pending=root/'browser-control-pending.json'
        if not pending.exists():return False
        value=json.loads(pending.read_text())
        require(set(value)=={'request_id','boot_id'} and value['boot_id']==args.boot and
                len(value['request_id'])==32 and all(c in '0123456789abcdef' for c in value['request_id']),
                'Invalid browser control intent')
        permit=root/'browser-control-permit.json'
        require(not permit.exists() or json.loads(permit.read_text())['request_id']!=value['request_id'],
                'Previous browser control did not consume its permit')
        temporary=permit.with_suffix('.tmp')
        with temporary.open('w') as out:
            json.dump(dict(value,end_monotonic_ns=int(until*1e9)),out);out.flush();os.fsync(out.fileno())
        temporary.replace(permit)
        note('browser_control_permit',dict(value,end_monotonic_ns=int(until*1e9)))
        return True
    def browser(start,end):
        try:
            browser_schedule(start,end,stop,pico_get,grant_control=grant_control if plan.get('browser_control_lane') else None)
            note('browser_finish',{'completed':not stop.is_set()})
        except BaseException as error:
            failures.append(str(error));note('browser_failure',{'type':type(error).__name__,'error':str(error)})
            stop.set()
    process=None; thread=None; exitcode=None; completed=False
    note('start',plan)
    try:
        with (root/'production.log').open('xb') as output:
            process=subprocess.Popen([str(binary),'--backend','wtp','-i',str(ini),
                '--socket-loopback-only','--socket-loopback-family','ipv4',
                '--allow-unqualified-frequency','--allow-non-amateur-frequency'],cwd=root,
                stdout=output,stderr=subprocess.STDOUT,env=dict(os.environ,
                    LD_PRELOAD=str(observer),PHASE115_TLS_LOG=str(root/'production-tls.bin')))
            deadline=time.monotonic()+40;status=None
            while time.monotonic()<deadline and not stop.is_set():
                require(process.poll() is None,'Production exited before readiness')
                try: status=host_status()
                except OSError: time.sleep(.2);continue
                if status.get('host',{}).get('identity'): break
                time.sleep(.2)
            require(status and status.get('host',{}).get('identity'),'Missing production identity')
            identity=status['host']['identity'];network=status['host']['network']
            require(identity['device_id']==DEVICE and identity['boot_id']==args.boot and
                    network['resolved_address']==plan['address'] and
                    network['authenticated_identity']==NAME,'Production peer identity')
            start=time.monotonic();end=start+args.seconds
            (root/'ready.json').write_text(json.dumps(dict(start_monotonic_ns=time.monotonic_ns(),
                                                          boot_id=args.boot))+'\n')
            note('nominal_begin',{'seconds':args.seconds})
            if args.browser:
                thread=threading.Thread(target=browser,args=(start,end));thread.start()
            next_status=start
            while time.monotonic()<end and not stop.is_set():
                require(process.poll() is None,'Production process died')
                if time.monotonic()>=next_status:
                    status=host_status();identity=status.get('host',{}).get('identity')
                    require(identity and identity['device_id']==DEVICE and identity['boot_id']==args.boot,
                            'Production identity/boot lost')
                    next_status+=1
                stop.wait(.05)
            if thread: thread.join(16);require(not thread.is_alive(),'Browser worker did not finish')
            require(not failures,'Nominal load worker failed')
            note('nominal_finish',{'seconds':args.seconds})
            completed=True
    except BaseException as error:
        failures.append(type(error).__name__+': '+str(error))
        note('load_failure',{'type':type(error).__name__,'error':str(error)})
        raise
    finally:
        stop.set()
        if thread:
            thread.join(16)
            if thread.is_alive(): failures.append('Browser worker did not stop')
        if process:
            if process.poll() is None:
                process.terminate()
                try: process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill();process.wait(timeout=5);failures.append('Production needed SIGKILL')
            exitcode=process.returncode
            note('production_exit',{'exit':exitcode})
        if completed and not failures and exitcode==0:
            try:
                rows=decode((root/'production-tls.bin').read_bytes())
                note('observer_framing_checked',{'records':len(rows),'sha256':digest(root/'production-tls.bin'),
                                               'wire_audit_required':True})
            except BaseException as error:
                failures.append('Observer framing: '+str(error))
        note('finish',{'result':'CAPTURED_REQUIRES_INDEPENDENT_AUDIT' if completed and not failures and exitcode==0
                      else 'FAILED','failures':failures,'exit':exitcode})
        log.close()
    require(completed and not failures and exitcode==0,'Production load did not finish cleanly')


if __name__=='__main__':main()
