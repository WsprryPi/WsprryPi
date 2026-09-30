import importlib.util,json,subprocess,sys,time,uuid
from pathlib import Path

repo=Path('/Users/lbussy/GitHub/WsprryPi')
spec=importlib.util.spec_from_file_location('wtp_probe',repo/'tools/wtp-pi/probe.py')
probe=importlib.util.module_from_spec(spec);spec.loader.exec_module(probe)
out=Path('/private/tmp/wtp-ini-evidence');out.mkdir(exist_ok=True)
stage='/home/pi/wtp-ini-validation.pW1gkI'
capture_root='/home/pi/wtp-ini-capture.kdILz5'
host='192.168.1.68'
helper='/home/pi/.cache/wsprrypi-qualification/native-v2/82562c1b937ba98816eb3ae1d27aa270729b35be862b1cf8d87d00b497b95438/wspq-capture-soapy'
def ssh(target,command):
    return subprocess.run(['ssh','-o','BatchMode=yes',target,command],check=True,
                          capture_output=True,text=True,timeout=20).stdout
def endpoint():
    status,_,body=probe.http(host,31415,'GET','wtp-endpoint')
    assert status==200,body
    return body
def set_enable(value):
    return json.loads(ssh('wspr4',f'sudo -n python3 {stage}/set-enable.py {stage}/validation.ini {value}'))

result={'case':'physical-ini-enable','host':'wspr4','receiver_host':'wspr5',
        'stage':stage,'capture_root':capture_root,'started_at_utc_ns':str(time.time_ns()),
        'outbound_wtp':[],'passed':False}
wire=None;capture=None;job=None
log=(out/'capture.log').open('w')
try:
    result['before']=endpoint()
    assert not result['before']['local_requested'] and not result['before']['remote_owner'],result['before']
    wire=probe.Wtp(host,31418)
    result['hello']=wire.hello
    original_request=wire.request
    def request(op,body=None,**kwargs):
        result['outbound_wtp'].append({'at_utc_ns':str(time.time_ns()),'op':op,'body':body})
        return original_request(op,body,**kwargs)
    wire.request=request
    result['caps']=wire.request('CAPS')
    assert result['caps']['body']['engine']=='si5351'
    result['clock']=wire.request('GET_CLOCK')
    assert result['clock']['body']['state']=='synchronized',result['clock']
    command=(f'{helper} --enable-physical-sdr sdrplay 2404058C60 14072100 6250000 '
             f'20 250000 200000 0 false false 500000 35 {capture_root}/ini-cancel.cf32 '
             f'{capture_root}/ini-cancel.json pi-wtp-ini-cancellation')
    result['capture_command']=command
    capture=subprocess.Popen(['ssh','-o','BatchMode=yes','wspr5',command],stdout=log,stderr=subprocess.STDOUT)
    time.sleep(3)
    assert capture.poll() is None,'capture exited before transmission'
    result['claim']=wire.request('CLAIM',{'owner_id':wire.owner,'lease_ms':60000})
    job=uuid.uuid4().hex
    result['load']=wire.request('LOAD',{'job_id':job,'profile':'rf-events/1','mode':'tone',
        'total_duration_ns':'10000000000','allow_frequency_adjustment':True,
        'events':[{'offset_ns':'0','duration_ns':'10000000000','rf_on':True,'frequency_nhz':'14097100000000000'}]})
    start=(int(time.time())+5)*1000000000
    result['chosen']={'job_id':job,'frequency_hz':14097100,'duration_s':10,'start_utc_ns':str(start)}
    result['arm']=wire.request('ARM',{'job_id':job,'start_utc_ns':str(start),'max_start_uncertainty_ns':'500000000'})
    while time.time_ns()<start+2_000_000_000:time.sleep(.05)
    result['running']=endpoint()
    assert result['running']['remote_state']=='running' and result['running']['remote_output_active'],result['running']
    result['ini_write']=set_enable('true')
    result['observations']=[]
    deadline=time.monotonic()+5
    while time.monotonic()<deadline:
        state=endpoint();now=time.time_ns()
        result['observations'].append({'at_utc_ns':str(now),'state':state})
        if state['local_effective'] and not state['remote_output_active'] and not state['remote_owner']:break
        time.sleep(.05)
    result['after']=state
    result['cancellation_observed_utc_ns']=str(now)
    result['observed_latency_from_ini_commit_ms']=(now-int(result['ini_write']['write_finished_utc_ns']))/1e6
    assert state['remote_state']=='aborted' and state['remote_job_id']==job,state
    assert not state['remote_output_active'] and not state['output_unknown'] and not state['remote_owner'],state
    assert state['local_requested'] and state['local_effective'] and state['remote_admission_closed'],state
    assert not state['finishing_remote_job'] and not state['local_work_active'],state
    assert now<start+10_000_000_000,'observed only after natural job end'
    assert int(state['revocation_generation'])>int(result['running']['revocation_generation'])
    result['terminal_status']=wire.request('STATUS')
    assert result['terminal_status']['body']['state']=='aborted',result['terminal_status']
    retry=probe.Wtp(host,31418)
    try:
        result['claim_while_local']=retry.request('CLAIM',{'owner_id':retry.owner,'lease_ms':60000},okay=False)
        assert not result['claim_while_local']['ok'],'remote reclaimed locally enabled output'
    finally:retry.close()
    result['after_claim_attempt']=endpoint()
    assert result['after_claim_attempt']['local_effective'] and not result['after_claim_attempt']['remote_owner']
    assert capture.wait(timeout=35)==0,'SDR capture failed'
    result['capture_exit_code']=capture.returncode
    result['passed']=True
except Exception as error:
    result['failure']=repr(error)
    raise
finally:
    if wire is not None:
        try:
            if job:wire.request('ABORT',{'job_id':job},okay=False)
            wire.request('RELEASE',okay=False)
        except Exception as error:result['wire_cleanup_error']=repr(error)
        result['wire']=wire.records;wire.close()
    try:
        state=endpoint()
        if state['local_requested']:result['cleanup_ini_write']=set_enable('false')
        time.sleep(1)
        result['cleanup_endpoint']=endpoint()
    except Exception as error:result['cleanup_error']=repr(error)
    try:result['stop_requested']=ssh('wspr4',f'sudo -n touch {stage}/stop-test')==''
    except Exception as error:result['stop_error']=repr(error)
    if capture is not None and capture.poll() is None:
        try:capture.wait(timeout=40)
        except subprocess.TimeoutExpired:
            capture.terminate();result['capture_cleanup_error']='SSH capture did not finish within bounded wait'
    log.close()
    (out/'ini-cancellation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('passed','observed_latency_from_ini_commit_ms','failure') if k in result}))
