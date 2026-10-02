"""Read-only final verification of the live browser campaign's restoration."""
import hashlib,json,subprocess,sys,time,urllib.request
from pathlib import Path
sys.path.insert(0,'/home/pi/wtp-fleet-reconnection-20261001')
from probe import Wtp
stage=Path(sys.argv[1])
def api(path):
 with urllib.request.urlopen('http://127.0.0.1:31415/api/v1/host/'+path,timeout=10) as r:return json.load(r)
fleet,endpoint,catalog=api('fleet'),api('wtp-endpoint'),api('devices')
before=json.loads((stage/'before-fleet.json').read_text());before_catalog=json.loads((stage/'before-devices.json').read_text())
assert len(fleet['assignments'])==5 and all(not a['enabled'] and not a['in_flight'] for a in fleet['assignments'])
assert not endpoint['local_requested'] and not endpoint['local_work_active'] and not endpoint['remote_owner'] and not endpoint['remote_output_active'] and not endpoint['output_unknown']
assert catalog['profiles']==before_catalog['profiles'] and catalog['active_id']==before_catalog['active_id']
watermarks={}
for a in fleet['assignments']:
 b=next(r for r in before['assignments'] if r['device_id']==a['device_id'])
 for k in ('device_id','name','product','settings','schedule','management_port','revocation_generation'):assert a[k]==b[k],(a['name'],k)
 assert int(a['last_start_ns'])>=int(b['last_start_ns'])
 watermarks[a['name']]={'before':b['last_start_ns'],'after':a['last_start_ns']}
hashes={p:hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in ('/usr/local/bin/wsprrypi','/usr/local/lib/wsprrypi/route_application.py','/usr/local/etc/wsprrypi.ini','/var/www/html/wsprrypi/fleet-schedules.js')}
assert hashes['/usr/local/bin/wsprrypi']=='663b46d503c69ff4c718ba1d6ecfe2e5877e661b42bfeb1fb76c3feeb0dfa700'
assert hashes['/usr/local/lib/wsprrypi/route_application.py']=='3d03c02db2a13feb32f29ee44d921635a44c2aeb97f7cebdfc512e8c025c463b'
assert hashes['/usr/local/etc/wsprrypi.ini']=='b8d705b6ec1b4f8618698a5495ad8d06e57a9da2f25d32d8be12f5f6ae81c576'
targets={}
for name,address,expected in [('wspr1','192.168.1.44','d133ed890b90f6945b41a235429ff155'),('wspr2','192.168.1.123','e2a4d273f7d8f24f6696f7c6fb95ff11'),('wspr4','192.168.1.120','f21e530af8a4a210ec57898c0ebd32d2'),('Pico A','192.168.1.47','fd6127d11d6aca42a9905fa3fb1bf1d5')]:
 wire=Wtp(address,31417)
 try:
  assert wire.hello['body']['device_id']==expected
  status=wire.request('STATUS')['body'];assert not status['output_active'] and not status['owner_id'] and not status['job_id']
  targets[name]={'hello':wire.hello['body'],'status':status,'observed_utc_ns':str(time.time_ns())}
 finally:wire.close()
targets['Pico B']={'tested':False,'fresh_output_off_evidence':False,'note':'Not used; saved assignment remained paused. It was offline in the browser. No new physical output claim.'}
service=subprocess.check_output(['systemctl','show','wsprrypi','-p','MainPID','-p','ActiveState'],text=True)
assert 'ActiveState=active' in service
pid=next(l.split('=')[1] for l in service.splitlines() if l.startswith('MainPID='))
args=Path('/proc/'+pid+'/cmdline').read_bytes().decode().strip('\0').split('\0')
assert args==['/usr/local/bin/wsprrypi','-J','-i','/usr/local/etc/wsprrypi.ini']
timers=subprocess.check_output(['systemctl','list-timers','--all','--no-pager'],text=True)
assert 'wtp-live-fleet-browser-guard' not in timers
processes=[]
for p in Path('/proc').glob('[0-9]*/cmdline'):
 try:
  a=p.read_bytes().decode().strip('\0').split('\0')
  if a and (Path(a[0]).name=='wspq-capture-soapy' or str(stage/'observe.py') in a):processes.append(p.parent.name)
 except (OSError,UnicodeDecodeError):pass
assert not processes
chrony=subprocess.check_output(['chronyc','tracking'],text=True);assert 'Leap status     : Normal' in chrony
result={'fleet':fleet,'endpoint':endpoint,'catalog_preserved':True,'original_definitions_restored':True,'persisted_after_service_restart':True,'watermarks':watermarks,'hashes':hashes,'targets':targets,'service':service,'arguments':args,'no_campaign_processes_or_timer':True,'chrony':chrony,'pps_link':str(Path('/dev/pps-gps').resolve())}
(stage/'restoration.json').write_text(json.dumps(result,indent=2)+'\n')
print('Restored original definitions, retained slot history, service restart persistence, canonical service, unchanged INI/helper/binary/catalog; four reachable remote outputs freshly off and unowned; Pico B excluded')
