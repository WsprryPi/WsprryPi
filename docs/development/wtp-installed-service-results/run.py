import json,subprocess,time,uuid,sys,os,traceback
from pathlib import Path
OUT=Path('/private/tmp/wtp-installed-service-evidence');OUT.mkdir(exist_ok=True)
STAGE='/home/pi/wtp-installed-validation.yTbN3G'; CTRL='/home/pi/wtp-installed-controller.so4gXb';HOST='192.168.1.68'
rpc=subprocess.Popen(['ssh','-o','BatchMode=yes','wspr5',f'python3 -u {CTRL}/wtp-installed-rpc.py'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1)
record={'started_utc_ns':str(time.time_ns()),'cases':{},'rpc':[],'passed':False}
def save(): (OUT/'service-tests.json').write_text(json.dumps(record,indent=2)+'\n')
def call(**c):
 rpc.stdin.write(json.dumps(c)+'\n');rpc.stdin.flush();line=rpc.stdout.readline()
 if not line:raise RuntimeError('controller pipe closed')
 value=json.loads(line);record['rpc'].append({'request':c,'reply':value})
 assert value['ok'],value
 return value['value']
def sh(command,check=True,timeout=45,host='wspr4'):
 p=subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=4',host,command],capture_output=True,text=True,timeout=timeout)
 record.setdefault('commands',[]).append({'command':command,'host':host,'at_utc_ns':str(time.time_ns()),'code':p.returncode,'stdout':p.stdout,'stderr':p.stderr})
 if check and p.returncode:raise RuntimeError(p.stderr or p.stdout)
 return p.stdout.strip()
def http(path='wtp-endpoint',method='GET',body=None,revision=None):return call(op='http',host=HOST,path=path,method=method,body=body,revision=revision)
def state():
 code,_,body=http();assert code==200,body;return body
def wait_state(predicate=lambda s:bool(s.get('listener_running')),seconds=90):
 end=time.monotonic()+seconds;last=None
 while time.monotonic()<end:
  try:
   last=state()
   if predicate(last):return last
  except Exception as e:last=repr(e)
  time.sleep(1)
 raise RuntimeError('state wait timed out: '+str(last))
def openwire(name):return call(op='open',name=name,host=HOST)
def req(name,command,body=None,okay=True):return call(op='request',name=name,command=command,body=body,okay=okay)
def close(name):return call(op='close',name=name)
def info():return {'host_boot_id':sh('cat /proc/sys/kernel/random/boot_id'),'pid':sh('systemctl show wsprrypi -p MainPID --value'),'endpoint':state()}
def set_config(on):return sh(f'sudo -n python3 {STAGE}/config.py '+('on' if on else 'off'))
def restart():
 sh('sudo -n systemctl restart wsprrypi',timeout=45)
 return wait_state(lambda s:s.get('listener',{}).get('listening',False) or bool(s.get('listener_running')))
def reboot():
 old=sh('cat /proc/sys/kernel/random/boot_id');sh('sudo -n systemctl reboot',check=False)
 end=time.monotonic()+150
 while time.monotonic()<end:
  time.sleep(3)
  try:
   new=sh('cat /proc/sys/kernel/random/boot_id',check=False,timeout=8)
   if new and new!=old:return wait_state(lambda s:bool(s.get('listener_running')),seconds=75)
  except Exception:pass
 raise RuntimeError('host did not reboot and recover')
def mark(name,value):record['cases'][name]=value;save();print(json.dumps({'case':name,'passed':value.get('passed',False)}),flush=True)
def claim(name):
 w=openwire(name);r=req(name,'CLAIM',{'owner_id':w['owner'],'lease_ms':60000});return w,r
def job(name,duration=30,lead=5):
 w,r=claim(name);j=uuid.uuid4().hex;d=str(duration*1000000000)
 load=req(name,'LOAD',{'job_id':j,'profile':'rf-events/1','mode':'tone','total_duration_ns':d,'allow_frequency_adjustment':True,'events':[{'offset_ns':'0','duration_ns':d,'rf_on':True,'frequency_nhz':'14097100000000000'}]})
 start=(int(time.time())+lead)*1000000000
 arm=req(name,'ARM',{'job_id':j,'start_utc_ns':str(start),'max_start_uncertainty_ns':'500000000'})
 return {'wire':w,'claim':r,'job_id':j,'load':load,'arm':arm,'start_utc_ns':str(start),'frequency_hz':14097100,'duration_s':duration}
try:
 sh(f'test -f /home/pi/finished && sudo -n test -s {STAGE}/baseline.tar')
 sh('sudo -n systemctl stop wsprrypi');set_config(False);sh('sudo -n systemctl start wsprrypi')
 initial=wait_state(lambda s:bool(s.get('listener_running')))
 mark('installed-start',{'passed':True,'state':initial,'info':info(),'binary_sha256':sh('sha256sum /usr/local/bin/wsprrypi')})
 w,c=claim('first');caps=req('first','CAPS');assert caps['body']['engine']=='si5351',caps;record['installed_caps']=caps;req('first','RELEASE');close('first')
 before=info();after=reboot();now=info()
 assert not after['local_requested'] and not after['remote_owner'] and not after['output_unknown'],after
 assert now['host_boot_id']!=before['host_boot_id'] and after['device_id']==initial['device_id']
 w,c=claim('boot-off');req('boot-off','RELEASE');close('boot-off')
 mark('boot-local-disabled',{'passed':True,'before':before,'after':now,'claim':c})
 set_config(True);local=wait_state(lambda s:s.get('local_effective') and not s.get('remote_owner'))
 journal=sh('sudo -n cat /root/.wsprrypi-wtp/revocation-v1')
 before=info();after=reboot();now=info()
 assert after['local_requested'] and after['local_effective'] and after['remote_admission_closed'],after
 assert int(after['revocation_generation'])>=int(local['revocation_generation'])
 w=openwire('boot-on');busy=req('boot-on','CLAIM',{'owner_id':w['owner'],'lease_ms':60000},okay=False);close('boot-on')
 assert not busy['ok'],busy
 mark('boot-local-enabled',{'passed':True,'before':before,'after':now,'claim':busy,'journal_before':journal,'journal_after':sh('sudo -n cat /root/.wsprrypi-wtp/revocation-v1')})
 set_config(False);wait_state(lambda s:not s['local_requested'])
 w,c=claim('owned');before=info();after=restart();now=info()
 assert not after['remote_owner'] and not after['remote_output_active'] and not after['output_unknown'],after
 assert before['endpoint']['boot_id']!=after['boot_id'] and before['endpoint']['device_id']==after['device_id']
 close('owned');w,c=claim('fresh');req('fresh','RELEASE');close('fresh')
 mark('restart-owned-idle',{'passed':True,'before':before,'after':now,'new_claim':c})
 # Clock must be synchronized again following actual host boots.
 end=time.monotonic()+90
 while True:
  openwire('clock');clock=req('clock','GET_CLOCK');close('clock')
  if clock['body']['state']=='synchronized' and int(clock['body'].get('uncertainty_ns','9999999999'))<=500000000:break
  assert time.monotonic()<end,clock;time.sleep(2)
 helper='/home/pi/.cache/wsprrypi-qualification/native-v2/82562c1b937ba98816eb3ae1d27aa270729b35be862b1cf8d87d00b497b95438/wspq-capture-soapy'
 caplog=(OUT/'capture.log').open('w')
 capture=subprocess.Popen(['ssh','-o','BatchMode=yes','wspr5',f'{helper} --enable-physical-sdr sdrplay 2404058C60 14072100 7500000 20 250000 200000 0 false false 500000 40 {CTRL}/restart.cf32 {CTRL}/restart.json installed-service-restart'],stdout=caplog,stderr=subprocess.STDOUT)
 time.sleep(3);assert capture.poll() is None,'capture exited early'
 active=job('running',duration=30)
 while time.time_ns()<int(active['start_utc_ns'])+3_000_000_000:time.sleep(.1)
 active['running']=state();assert active['running']['remote_state']=='running' and active['running']['remote_output_active'],active
 active['restart_requested_utc_ns']=str(time.time_ns());active['after']=restart();active['restart_observed_utc_ns']=str(time.time_ns())
 assert not active['after']['remote_owner'] and not active['after']['remote_output_active'] and not active['after']['output_unknown'],active
 assert active['after']['boot_id']!=active['running']['boot_id']
 close('running');openwire('stale')
 active['old_job_arm']=req('stale','ARM',{'job_id':active['job_id'],'start_utc_ns':str((int(time.time())+5)*1000000000),'max_start_uncertainty_ns':'500000000'},okay=False)
 assert not active['old_job_arm']['ok'];close('stale')
 assert capture.wait(timeout=45)==0,'capture failed';caplog.close()
 active['later']=state();assert not active['later']['remote_job_id'] and not active['later']['remote_output_active']
 active['passed']=True;mark('restart-running-physical-job',active)
 armed=job('armed',duration=3,lead=15);armed['before']=state();assert armed['before']['remote_state']=='armed'
 armed['after']=restart();close('armed')
 time.sleep(max(0,(int(armed['start_utc_ns'])-time.time_ns())/1e9+4))
 armed['later']=state();assert not armed['later']['remote_job_id'] and not armed['later']['remote_output_active'] and not armed['later']['output_unknown']
 armed['passed']=True;mark('restart-armed-job',armed)
 # Preserve the current valid generation while testing an unreadable recovery state.
 sh('sudo -n systemctl stop wsprrypi')
 sh(f'sudo -n cp -p /root/.wsprrypi-wtp/revocation-v1 {STAGE}/valid-current-revocation-v1')
 try:
  sh("sudo -n sh -c 'printf invalid-test-journal > /root/.wsprrypi-wtp/revocation-v1'")
  sh('sudo -n systemctl start wsprrypi');time.sleep(5)
  bad=state();assert not bad.get('listener_running') and not bad.get('remote_output_active',False),bad
  blocked=False
  try:openwire('bad-journal')
  except AssertionError:blocked=True
  assert blocked,'invalid journal accepted a WTP connection'
  mark('invalid-journal-inhibits-admission',{'passed':True,'state':bad})
 finally:
  sh('sudo -n systemctl stop wsprrypi')
  sh(f'sudo -n cp -p {STAGE}/valid-current-revocation-v1 /root/.wsprrypi-wtp/revocation-v1')
  sh('sudo -n systemctl start wsprrypi');wait_state(lambda s:bool(s.get('listener_running')))
 # A persisted in-flight fixture validates installed process recovery without claiming Pico hardware acceptance.
 sh('sudo -n systemctl stop wsprrypi')
 sh(f'sudo -n python3 {STAGE}/fleet-fixture.py')
 sh('sudo -n systemctl start wsprrypi');wait_state(lambda s:bool(s.get('listener_running')));time.sleep(2)
 code,_,fleet=http('fleet');assert code==200,fleet
 row=fleet['assignments'][0] if 'assignments' in fleet else fleet['document']['assignments'][0]
 assert row['in_flight'] and row['last_start_ns']=='1800000000000000000',fleet
 assert fleet['outputs'][row['device_id']]['state']=='recovery_required',fleet
 before=fleet;restart();time.sleep(2);code,_,fleet=http('fleet')
 row=fleet['assignments'][0] if 'assignments' in fleet else fleet['document']['assignments'][0]
 assert row['in_flight'] and row['last_start_ns']=='1800000000000000000'
 assert fleet['outputs'][row['device_id']]['state']=='recovery_required'
 mark('persisted-fleet-recovery-barrier',{'passed':True,'fixture':True,'before':before,'after':fleet})
 record['passed']=True
except Exception as e:
 record['failure']=repr(e);record['traceback']=traceback.format_exc();print(record['traceback'],flush=True)
finally:
 try:
  call(op='exit')
 except Exception:pass
 rpc.stdin.close();rpc.wait(timeout=10);save()
 print(json.dumps({'passed':record['passed'],'failure':record.get('failure')}),flush=True)

sys.exit(0 if record["passed"] else 1)
