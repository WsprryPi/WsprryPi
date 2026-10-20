"""Bounded, volatile GPSDO reference for user-authorized conducted Si5351 test."""
import json, pathlib, signal, sys, time
sys.path.insert(0, '/home/pi/lbgpsdo')
from lbe142x import GPSDODevice
root=pathlib.Path(sys.argv[1]); root.mkdir(parents=True,exist_ok=True)
fields=('serial','sat_lock','pll_lock','ant_ok','out1','out2','pps1','f1','f2','fll','out1low','out2low')
def record(name,d):
    state={k:getattr(d,k) for k in fields};state['utc_unix']=time.time()
    (root/(name+'.json')).write_text(json.dumps(state,indent=2));return state

def stop(signum,frame): raise RuntimeError('Guard received stop signal')
for s in (signal.SIGTERM,signal.SIGINT,signal.SIGHUP):signal.signal(s,stop)
d=None;before=None
try:
    d=GPSDODevice.open(serial='0673ED0FA107')
    before=record('before',d)
    assert not d.out1 and not d.out2 and not d.pps1, 'Unexpected existing GPSDO output'
    assert d.sat_lock and d.pll_lock and d.ant_ok and not d.fll, 'GPSDO not PLL locked'
    d.set_freq(0,7050100,False)
    d.set_level(0,True)
    d.read()
    assert d.sat_lock and d.pll_lock and d.ant_ok and not d.fll
    deadline=time.monotonic()+180
    d.enable(True,False)
    d.read()
    assert d.out1 and not d.out2 and not d.pps1 and d.f1==7050100 and d.out1low
    assert d.sat_lock and d.pll_lock
    record('ready',d)
    i=0
    while time.monotonic()<deadline and not (root/'stop').exists():
        time.sleep(1)
        if i%5==0:
            d.read();record('status-%03d'%i,d)
            assert d.sat_lock and d.pll_lock and d.out1 and not d.out2 and not d.pps1
        i+=1
except BaseException as exc:
    (root/'error.txt').write_text(repr(exc));raise
finally:
    if d is not None:
        try:
            d.enable(False,False);d.set_pps(False);d.read()
            assert not d.out1 and not d.out2 and not d.pps1
            record('disabled',d)
            if before:
                d.set_freq(0,before['f1'],False);d.set_level(0,before['out1low']);d.read()
            record('after',d)
        finally:d.close()
