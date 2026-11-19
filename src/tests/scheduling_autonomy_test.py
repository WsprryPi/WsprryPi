#!/usr/bin/env python3
"""Two actual-main simulated targets; no controller process during executions."""
import argparse
import configparser
import calendar
import copy
import http.server
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
from controller_only_test import free_port, request as raw_request


_last_request = {}

def request(port, *args, **kwargs):
    # Honor the real protected API's ten-requests/second budget. This fixture
    # paces initial setup too; it never relaxes production rate or CSRF policy.
    delay = .12 - (time.monotonic() - _last_request.get(port, 0))
    if delay > 0:
        time.sleep(delay)
    _last_request[port] = time.monotonic()
    return raw_request(port, *args, **kwargs)


def controller(port, targets):
    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == '/targets':
                rows=[]
                for device, target_port in targets.items():
                    with urllib.request.urlopen(f'http://127.0.0.1:{target_port}/api/control/v1/targets/{device}/status', timeout=3) as response:
                        rows.append(json.load(response))
                body=json.dumps({'controller_location':'QF56OB','observations':rows}).encode()
                self.send_response(200);self.send_header('Content-Type','application/json');self.end_headers();self.wfile.write(body)
            else:
                body=b'<!doctype html><title>Software autonomy evidence viewer</title><pre id="observations">Reading</pre><script>fetch("/targets").then(r=>r.json()).then(v=>document.getElementById("observations").textContent=JSON.stringify(v))</script>'
                self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(body)
        def log_message(self,*args):
            pass
    http.server.ThreadingHTTPServer(('127.0.0.1',port),Handler).serve_forever()


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--fixture');parser.add_argument('--controller',type=int);parser.add_argument('--targets');parser.add_argument('--output',required=False)
    args=parser.parse_args()
    if args.controller:
        controller(args.controller,json.loads(args.targets));return
    fixture=str(Path(args.fixture).resolve());out=Path(args.output).resolve();out.mkdir(parents=True,exist_ok=True)
    processes=[];manager=None;logs=[]
    def wait(check,label,seconds=15):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            for p in processes:assert p.poll() is None,'Target exited; inspect retained logs'
            try:
                value=check()
                if value:return value
            except (OSError,ValueError):pass
            time.sleep(.04)
        raise AssertionError(label)
    try:
        with tempfile.TemporaryDirectory(prefix='scheduling-autonomy-') as temporary:
            root=Path(temporary);owners=[];allocated=set()
            def target_port():
                for port in range(39000,39998,2):
                    if port in allocated:continue
                    try:
                        with socket.socket() as a,socket.socket() as b:
                            a.bind(('127.0.0.1',port));b.bind(('127.0.0.1',port+1))
                        allocated.add(port);return port
                    except OSError:continue
                raise AssertionError('No isolated target ports')
            for device,locator in [('a'*32,'EM18IG'),('b'*32,'MK88IG')]:
                directory=root/device;directory.mkdir(mode=0o700);ini=directory/'native.ini';ledger=directory/'ledger';port=target_port()
                cfg=configparser.ConfigParser(interpolation=None);cfg.optionxform=str;cfg.read(Path(__file__).resolve().parents[2]/'config/wsprrypi.ini')
                cfg['Operation'].update({'Transmit Backend':'wtp','Transmit':'false','Enable on Boot':'Never','Use LED':'false','Use Amp':'false','Use Shutdown':'false','Web Port':str(port),'Socket Port':str(port+1)})
                cfg['WSPR'].update({'Call Sign':'AA0NT','Grid Square':locator,'TX Power':'20'});cfg['WTP Server']['Enabled']='false'
                cfg['WTP'].update({'Transport':'network_plain','Hostname':'127.0.0.1','TCP Port':'31417','Device ID':'c'*32})
                with ini.open('w') as f:cfg.write(f)
                ini.chmod(0o600);Path(str(ini)+'.stock').write_bytes(ini.read_bytes());Path(str(ini)+'.stock').chmod(0o600)
                log=(out/(device[:1]+'-target.log')).open('w');logs.append(log)
                env={**os.environ,'TZ':'UTC','WSPRRYPI_DISABLE_HARDWARE_ACCESS':'1'}
                command=[fixture,str(ledger),device,'--backend','simulated','-i',str(ini),'--socket-loopback-only','--socket-loopback-family','ipv4','--no-offset','--no-system-clock-frequency-estimate']
                process=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT,env=env);processes.append(process)
                owners.append({'device':device,'locator':locator,'port':port,'ini':ini,'ledger':ledger,'process':process,'command':command,'progress':Path(str(ini)+'.recurrence-progress.json'),'trace':Path(str(ledger)+'.simulated-trace.json')})
            for owner in owners:
                port=owner['port'];base=f"/api/control/v1/targets/{owner['device']}/"
                def read(path):
                    status,body=request(port,'GET',path);return json.loads(body) if status==200 else None
                wait(lambda:(read('/api/control/v1/host/readiness') or {}).get('state')=='configuration_recovery','Initial setup readiness')
                recovery=read('/api/control/v1/host/configuration-recovery')
                headers={'Origin':f'http://127.0.0.1:{port}','Content-Type':'application/json','X-WsprryPico-Request':'1'}
                status,body,session=request(port,'POST','/api/control/v1/host/management-session',json.dumps({'contract':'Wsprry-Control/1','schema':'controller-browser-session'}),headers,with_headers=True);assert status==200,(status,body)
                headers.update({'Cookie':session['Set-Cookie'].split(';')[0],'X-WsprryPi-CSRF':session['X-WsprryPi-CSRF']})
                def mutate(path,method,body,expected=200):
                    result=None
                    for attempt in range(5):
                        session_status,session_body,session_headers=request(port,'POST','/api/control/v1/host/management-session',json.dumps({'contract':'Wsprry-Control/1','schema':'controller-browser-session'}),headers,with_headers=True)
                        assert session_status==200,(session_status,session_body)
                        headers.update({'Cookie':session_headers['Set-Cookie'].split(';')[0],'X-WsprryPi-CSRF':session_headers['X-WsprryPi-CSRF']})
                        result=request(port,method,path,json.dumps(body),{**headers,'If-Match':'"'+body['expected_revision']+'"'})
                        if result[0]!=409 or json.loads(result[1]).get('error',{}).get('code')!='capacity':break
                        # The owner refused before its write lock. Confirm the
                        # original receipt is absent and revision unchanged;
                        # never retry transport failures or unknown decisions.
                        status,receipt=request(port,'GET',base+'operations/'+body['operation_id'])
                        if status==200 and json.loads(receipt)['state']=='committed':return json.loads(receipt)
                        current=read(base+'settings')
                        assert status in (404,409) and current and current['revision']==body['expected_revision']
                        time.sleep(.12)
                    assert result[0]==expected,result
                    return json.loads(result[1])
                candidate=copy.deepcopy(recovery['draft']);candidate['Operation'].update({'Transmit':False,'Enable on Boot':'Never'});candidate['WSPR'].update({'Call Sign':'AA0NT','Grid Square':owner['locator'],'TX Power':20});candidate['WTP Server']['Enabled']=False
                for key in ['TLS CA File','TLS Client Certificate','TLS Client Key']:candidate['WTP'][key]=''
                mutate('/api/control/v1/host/configuration-recovery','PUT',{'contract':'Wsprry-Control/1','schema':'configuration-recovery-save','binding':recovery['binding'],'capability_revision':recovery['capability_revision'],'operation_id':secrets.token_hex(16),'expected_revision':recovery['revision'],'configuration':candidate})
                wait(lambda:(read('/api/control/v1/host/readiness') or {}).get('state')=='ready','Ready actual main')
                context=read(base+'context');saved=read(base+'settings');assert context['binding']['device_id']==owner['device']
                candidate=copy.deepcopy(saved['saved']);candidate['configuration']['Operation'].update({'Mode':'QRSS','Transmit':True,'Enable on Boot':'Follow'})
                start=time.gmtime(time.time()+20);owner['first_start_utc_ns']=calendar.timegm(start)*1000000000;candidate['configuration']['CW'].update({'Message':'E','Base Frequency':14097000,'Dot Seconds':.02,'Start Minute':start.tm_min,'Start Second':start.tm_sec,'Repeat Minutes':1,'Fade Shape':'none','Fade In Ms':0,'Fade Out Ms':0,'Fade Slice Ms':5})
                solar=candidate['scheduling']['solar'];solar['enabled']=True;solar['location']={'source':'locator','locator':owner['locator']};solar['day']['allow']=solar['night']['allow']=True;solar['morning']['enabled']=solar['evening']['enabled']=False;solar['day']['cw_base_hz']='14097000';solar['night']['cw_base_hz']='10140000'
                body={'contract':'Wsprry-Control/1','schema':'target-save','binding':context['binding'],'capability_revision':context['capability_revision'],'operation_id':secrets.token_hex(16),'expected_revision':saved['revision'],'candidate':candidate}
                bad=copy.deepcopy(body);bad['candidate']['scheduling']['solar']['enabled']=False;bad['candidate']['scheduling']['solar']['morning'].update({'enabled':False,'lower_mdeg':6000,'upper_mdeg':-6000});mutate(base+'settings','PUT',bad,400);assert read(base+'settings')['revision']==saved['revision'],'Inactive malformed controls must refuse before writes'
                body['operation_id']=secrets.token_hex(16);mutate(base+'settings','PUT',body)
                context=read(base+'context');saved=read(base+'settings');assert saved['saved']['scheduling']['enabled'] is False
                mutate(base+'enable','POST',{'contract':'Wsprry-Control/1','schema':'target-enable','binding':context['binding'],'capability_revision':context['capability_revision'],'operation_id':secrets.token_hex(16),'expected_revision':saved['revision'],'candidate_digest':saved['candidate_digest']})
                owner['initial_progress']=json.loads(owner['progress'].read_text())['record'];owner['ini_bytes']=owner['ini'].read_bytes();owner['saved']=read(base+'settings');owner['events']={};owner['admissions']={}
            ports={o['device']:o['port'] for o in owners};controller_port=free_port()
            command=[sys.executable,str(Path(__file__).resolve()),'--controller',str(controller_port),'--targets',json.dumps(ports)]
            manager=subprocess.Popen(command,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,env={**os.environ,'TZ':'Australia/Sydney'})
            def controller_read():
                with urllib.request.urlopen(f'http://127.0.0.1:{controller_port}/targets',timeout=2) as response:return json.load(response)
            before=wait(controller_read,'Controller observations')
            # The browser is an observation client only. Closing it cannot tick
            # either target; actual main owns all recurrence and simulated output.
            probe=Path(__file__).resolve().parents[2]/'WsprryPi-UI/tests/scheduling_autonomy_browser_probe.js'
            subprocess.run(['node',str(probe),f'http://127.0.0.1:{controller_port}/'],check=True,timeout=25)
            manager.terminate();assert manager.wait(timeout=5)==-15;manager=None
            absent=time.time_ns();assert all(o['first_start_utc_ns']>absent for o in owners),'Browser and controller must close before either first execution';end=time.monotonic()+110
            while time.monotonic()<end:
                complete=True
                for owner in owners:
                    assert owner['process'].poll() is None
                    record=json.loads(owner['progress'].read_text())['record'];flight=record['in_flight']
                    if flight:owner['admissions'].setdefault(flight['occurrence_id'],{'observed_at_utc_ns':str(time.time_ns()),'record':flight})
                    try:
                        trace=json.loads(owner['trace'].read_text())
                        if any(e['kind']=='execution_start' for e in trace['events']):owner['events'].setdefault(str(trace['plan_id']),{'observed_at_utc_ns':str(time.time_ns()),'trace':trace})
                    except (FileNotFoundError,json.JSONDecodeError):pass
                    complete=complete and len(owner['events'])>=2
                    assert owner['ini'].read_bytes()==owner['ini_bytes'],'No controller-side sunrise/config writes'
                if complete:break
                time.sleep(.04)
            assert all(len(o['events'])==2 for o in owners),'Two target-owned executions required during controller absence'
            # Let completion settle into the next future reservation, then prove
            # reconnect does not replace it or replay either completed plan.
            def next_reservations():
                records={o['device']:json.loads(o['progress'].read_text())['record'] for o in owners}
                return records if all(v['in_flight'] and int(v['in_flight']['first_start_ns'])>time.time_ns()+5000000000 for v in records.values()) else None
            before_reconnect=wait(next_reservations,'Settled future reservations',5)
            plans_before={o['device']:json.loads(o['trace'].read_text())['plan_id'] for o in owners}
            manager=subprocess.Popen(command,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,env={**os.environ,'TZ':'Pacific/Honolulu'})
            after=wait(controller_read,'Controller reconnect observations');assert {v['selection'] for v in after['observations']}=={'day','night'}
            time.sleep(1.2)
            for owner in owners:
                current=json.loads(owner['progress'].read_text())['record'];prior=before_reconnect[owner['device']]
                assert current['in_flight']==prior['in_flight'] and current['consumed_through_utc_ns']==prior['consumed_through_utc_ns'],'Reconnect must not admit or replay work'
                assert json.loads(owner['trace'].read_text())['plan_id']==plans_before[owner['device']],'Reconnect must not execute another plan'
            report={'reconnect_reservations':before_reconnect,'reconnect_plan_ids':plans_before,'schema':'issue473-phase7-autonomy/1','controller_absent_since_utc_ns':str(absent),'controller_before':before,'controller_after':after,'targets':[],'limits':'Actual main, protected APIs, native recurrence, journals and simulated execution; fixture-only independent identities and target-local healthy clock samples. No physical hardware, LAN, services or RF.'}
            for owner in owners:
                assert any(a['observed_at_utc_ns']>str(absent) and a['record']['occurrence_id']!=(owner['initial_progress']['in_flight'] or {}).get('occurrence_id') for a in owner['admissions'].values()),'Fresh autonomous admission while controller absent'
                # QRSS preserves the existing 4-tone configuration reference offset;
                # compare actual RF-on execution events, not configure/start reference.
                frequencies={event['frequency_hz'] for row in owner['events'].values() for event in row['trace']['events'] if event['kind']=='event' and event['rf_on']};assert len(frequencies)==1 and frequencies.issubset({14097000,10140000}),frequencies
                assert owner['ini'].read_bytes()==owner['ini_bytes'];assert owner['saved']['saved']['scheduling']['solar']['location']['locator']==owner['locator']
                assert all(any(event['kind']=='event' and event['rf_on'] for event in row['trace']['events']) for row in owner['events'].values()),'Actual simulated execution events required'
                record=json.loads(owner['progress'].read_text())['record'];assert record['consumed_through_utc_ns']>=owner['initial_progress']['consumed_through_utc_ns']
                report['targets'].append({'device':owner['device'],'locator':owner['locator'],'command':owner['command'],'admissions':owner['admissions'],'executions':owner['events'],'final_progress':record})
            assert {next(iter({e['frequency_hz'] for v in o['events'].values() for e in v['trace']['events'] if e['kind']=='event' and e['rf_on']})) for o in owners}=={14097000,10140000}
            (out/'evidence.json').write_text(json.dumps(report,indent=2)+'\n')
            print('PASS two actual-main targets at independent sites: browser closed, controller absent, fresh admissions and two simulated executions each, opposite solar selections/frequencies, retained configuration, reconnect without replay, and disabled-field API refusal')
    finally:
        for process in ([manager] if manager else [])+processes:
            if process.poll() is None:process.terminate();process.wait(timeout=10)
        for log in logs:log.close()

if __name__=='__main__':main()
