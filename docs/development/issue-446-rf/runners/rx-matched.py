import json, pathlib, signal, subprocess, sys, time
sys.path.insert(0, '/home/pi/lbgpsdo')
from lbe142x import GPSDODevice
freq = int(sys.argv[1]); assert freq in (7040100,14097100)
root = pathlib.Path('/home/pi/issue446-rf')/'matched'
root.mkdir(parents=True, exist_ok=True)
with (root/'claimed').open('x') as f: f.write(str(time.time()))
fields = ('serial','sat_lock','pll_lock','ant_ok','out1','out2','pps1','f1','f2','fll','out1low','out2low')
events=[]
def event(name,value):
    events.append({'event':name,'time_unix':time.time(),'monotonic':time.monotonic(),'value':value})
    (root/'reference.json').write_text(json.dumps(events,indent=2)); print(name,flush=True)
def state(): return {k:getattr(device,k) for k in fields}
def abort(*_): raise RuntimeError('reference guard interrupted')
for sig in (signal.SIGTERM,signal.SIGHUP,signal.SIGINT): signal.signal(sig,abort)
device=None; before=None; proc=None; complete=False
try:
    device=GPSDODevice.open(serial='0673ED0FA107'); before=state(); event('before',before)
    assert not device.out1 and not device.out2 and not device.pps1
    assert device.sat_lock and device.pll_lock and device.ant_ok and not device.fll
    device.set_freq(0,freq+10000,False); device.set_level(0,True); device.enable(True,False); device.read()
    assert device.out1 and device.f1==freq+10000 and device.sat_lock and device.pll_lock
    event('reference_ready',state())
    capture='/home/pi/wsprrypi-qualification-runs/complete-test-deployment-284c7e04a3fdd079c46e782b/wspq-capture-soapy'
    args=[capture,'--enable-physical-sdr','sdrplay','2404058C60',str(freq-25000),'15000000','20','250000','200000','0','false','false','500000','70',str(root/'capture.cf32'),str(root/'metadata.json'),'issue446-'+str(freq)]
    event('capture_arguments',args)
    with (root/'capture.log').open('w') as log:
        proc=subprocess.Popen(args,stdout=log,stderr=subprocess.STDOUT)
        deadline=time.monotonic()+8
        while not any(p.stat().st_size>2000000 for p in root.glob('capture.cf32*')):
            assert proc.poll() is None, 'capture exited before receiving samples'
            assert time.monotonic()<deadline, 'capture readiness timeout'
            time.sleep(.1)
        event('capture_samples_ready',True)
        time.sleep(3)
        assert proc.poll() is None
        (root/'ready-for-tone').write_text(str(time.time())); event('ready_for_tone',True)
        end=time.monotonic()+70
        while proc.poll() is None:
            assert time.monotonic()<end, 'capture outer deadline'
            time.sleep(2); device.read(); event('lock_check',state())
            assert device.sat_lock and device.pll_lock and device.out1 and device.f1==freq+10000
        event('capture_exit',proc.returncode); assert proc.returncode==0
        complete=True
finally:
    if proc is not None and proc.poll() is None:
        proc.terminate()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired: proc.kill(); proc.wait()
    if device is not None:
        try:
            device.enable(False,False); device.set_pps(False); device.read(); event('disabled',state())
            assert not device.out1 and not device.out2 and not device.pps1
            if before:
                device.set_freq(0,before['f1'],False); device.set_level(0,before['out1low']); device.read(); event('restored',state())
                assert device.f1==before['f1'] and device.out1low==before['out1low']
        finally: device.close()
    event('complete',complete)
