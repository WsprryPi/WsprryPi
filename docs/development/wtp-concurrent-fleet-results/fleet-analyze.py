import datetime, json, sys
from pathlib import Path
import numpy as np

stage=Path(sys.argv[1])
meta=json.loads((stage/'capture.json').read_text())
plan=json.loads((stage/'run-plan.json').read_text())
x=np.memmap(stage/'capture.cf32',dtype='<c8')
rate=250000
n=65536
step=125000
center=14075100
targets={'wspr4':14098100,'wspr1':14099100,'Pico B':14102100}
all_freq=[14097100,14098100,14099100,14100100,14101100,14102100]
f=np.fft.fftfreq(n,1/rate)
window=np.hanning(n)
t0=datetime.datetime.fromisoformat(meta['timestamps']['retained_capture_start_utc'].replace('Z','+00:00')).timestamp()
masks={k:np.abs(f-(v-center))<300 for k,v in targets.items()}
noise={}
for k,v in targets.items():
    d=np.abs(f-(v-center))
    mask=(d>500)&(d<3000)
    for carrier in all_freq: mask &= np.abs(f-(carrier-center))>400
    noise[k]=mask
series=[]
for i in range(0,len(x)-n,step):
    ps=np.abs(np.fft.fft(np.array(x[i:i+n])*window))**2
    row={'utc_seconds':t0+(i+n/2)/rate}
    for k in targets:
        bins=np.flatnonzero(masks[k]);peak=bins[np.argmax(ps[bins])]
        row[k]={'snr_db':float(10*np.log10(ps[peak]/max(float(np.median(ps[noise[k]])),1e-30))),'peak_hz':float(center+f[peak])}
    series.append(row)
slots=[]
for start in plan['slots']:
    during=[r for r in series if start+1<=r['utc_seconds']<=start+9]
    after=[r for r in series if start+13<=r['utc_seconds']<=start+18]
    rec={'start_utc_seconds':start,'outputs':{}}
    for k in targets:
        snr=[r[k]['snr_db'] for r in during]
        off=[r[k]['snr_db'] for r in after]
        rec['outputs'][k]={'samples':len(snr),'minimum_on_snr_db':min(snr,default=None),'median_on_snr_db':float(np.median(snr)) if snr else None,'maximum_off_snr_db':max(off,default=None),'median_peak_hz':float(np.median([r[k]['peak_hz'] for r in during])) if snr else None,'corresponding_signal_observed':bool(snr and min(snr)>30)}
    rec['three_signals_concurrent']=all(v['corresponding_signal_observed'] for v in rec['outputs'].values())
    slots.append(rec)
result={'criterion':'Corresponding SDR signal; 30 dB peak over masked local median noise throughout slot interior. No calibration or full RF-chain qualification.','capture_metadata':meta,'slots':slots,'all_three_slots_observed':all(r['three_signals_concurrent'] for r in slots),'fft_size':n,'step_samples':step,'sample_rate':rate,'timestamps':'FFT window centres from retained capture UTC; not precision edge timing.'}
(stage/'rf-analysis.json').write_text(json.dumps(result,indent=2)+'\n')
(stage/'rf-series.json').write_text(json.dumps(series)+'\n')
print(json.dumps({'all_three_slots_observed':result['all_three_slots_observed'],'slots':slots}))
