import hashlib,json,subprocess,urllib.request
from pathlib import Path
root=Path('/home/pi/wtp-fleet-reconnection-20261001')
def api(path):
 with urllib.request.urlopen('http://127.0.0.1:31415/api/v1/host/'+path,timeout=10) as r:return json.load(r)
def command(args):
 r=subprocess.run(args,capture_output=True,text=True)
 return {'exit_code':r.returncode,'stdout':r.stdout,'stderr':r.stderr}
fleet,endpoint=api('fleet'),api('wtp-endpoint')
assert not endpoint['local_requested'] and not endpoint['local_work_active'] and not endpoint['output_unknown']
assert len(fleet['assignments'])==5 and all(not a['enabled'] and not a['in_flight'] for a in fleet['assignments'])
before=json.loads((root/'preflight-controller.json').read_text())
original=before['fleet'] if 'fleet' in before else before
for a in fleet['assignments']:
 b=next(r for r in original['assignments'] if r['device_id']==a['device_id'])
 for key in ('device_id','product','settings','schedule','management_port','revocation_generation'):
  assert a[key]==b[key],(a['device_id'],key)
 assert int(a['last_start_ns'])>=int(b['last_start_ns'])
result={'fleet':fleet,'endpoint':endpoint,'assignment_definitions_preserved':True,'consumed_slot_history_preserved':True}
files=['/usr/local/bin/wsprrypi','/usr/local/lib/wsprrypi/route_application.py','/usr/local/etc/wsprrypi.ini']
result['sha256']={p:hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in files}
assert result['sha256'][files[0]]=='663b46d503c69ff4c718ba1d6ecfe2e5877e661b42bfeb1fb76c3feeb0dfa700'
assert result['sha256'][files[1]]=='3d03c02db2a13feb32f29ee44d921635a44c2aeb97f7cebdfc512e8c025c463b'
assert result['sha256'][files[2]]=='b8d705b6ec1b4f8618698a5495ad8d06e57a9da2f25d32d8be12f5f6ae81c576'
result['routes']=command(['ip','-4','route','show'])
assert result['routes']['stdout']==(root/'before-routes.txt').read_text()
result['timers']=command(['systemctl','list-timers','--all','--no-pager'])
assert not any('wtp-fleet-reconnect-guard' in l for l in result['timers']['stdout'].splitlines())
result['service']=command(['systemctl','show','wsprrypi','-p','MainPID','-p','ActiveState','-p','Restart','-p','RestartUSec'])
assert 'ActiveState=active' in result['service']['stdout']
pid=next(l.split('=')[1] for l in result['service']['stdout'].splitlines() if l.startswith('MainPID='))
result['process_arguments']=Path('/proc/'+pid+'/cmdline').read_bytes().decode().strip('\0').split('\0')
assert result['process_arguments']==['/usr/local/bin/wsprrypi','-J','-i','/usr/local/etc/wsprrypi.ini']
result['campaign_processes']=[]
for p in Path('/proc').glob('[0-9]*/cmdline'):
 try:
  args=p.read_bytes().decode().strip('\0').split('\0')
  if args and (Path(args[0]).name=='wspq-capture-soapy' or any(a.startswith(str(root)) and a.endswith('usb_observer.py') for a in args)):
   result['campaign_processes'].append({'pid':p.parent.name,'arguments':args})
 except (FileNotFoundError,PermissionError,UnicodeDecodeError):pass
assert not result['campaign_processes']
result['chrony']=command(['chronyc','tracking'])
assert 'Leap status     : Normal' in result['chrony']['stdout']
result['pps_link']=str(Path('/dev/pps-gps').resolve())
(root/'restoration.json').write_text(json.dumps(result,indent=2)+'\n')
print('Canonical service, unchanged INI/helper/definitions, monotonic history, original routes, no campaign timers/processes; GPS/PPS synchronized')
