import datetime,hashlib,json
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
root=Path('/private/tmp/wtp-installed-service-evidence')
m=json.loads((root/'restart.json').read_text());r=json.loads((root/'service-tests.json').read_text())['cases']['restart-running-physical-job']
p=root/'restart.cf32';digest=hashlib.sha256(p.read_bytes()).hexdigest()
assert digest==m['output']['sha256'] and m['primary_outcome']=='success' and m['cleanup']['outcome']=='verified'
fs=m['actual_settings']['sample_rate_hz'];center=m['actual_settings']['center_frequency_hz'];n=32768
iq=np.fromfile(p,dtype='<c8');assert len(iq)==m['retained_sample_count']
b=iq[:len(iq)//n*n].reshape(-1,n);f=np.fft.fftshift(np.fft.fftfreq(n,1/fs))+center;view=(f>14096500)&(f<14097700)
power=np.abs(np.fft.fftshift(np.fft.fft(b*np.hanning(n),axis=1),axes=1)[:,view])**2/n**2;f=f[view];t=(np.arange(len(b))+.5)*n/fs
stamp=datetime.datetime.fromisoformat(m['timestamps']['retained_capture_start_utc'].replace('Z','+00:00')).timestamp()
start=int(r['start_utc_ns'])/1e9-stamp;stop=int(r['restart_requested_utc_ns'])/1e9-stamp;ready=int(r['restart_observed_utc_ns'])/1e9-stamp
pre=(t>start-2)&(t<start-.3);on=(t>start+.4)&(t<stop-.2);post=(t>ready+.5)&(t<min(ready+8,t[-1]))
assert min(pre.sum(),on.sum(),post.sum())>4
near=np.abs(f-r['frequency_hz'])<150;delta=power[on].mean(axis=0)-power[pre].mean(axis=0);idx=np.flatnonzero(near)[np.argmax(delta[near])];carrier=f[idx]
trace=power[:,np.abs(f-carrier)<12].sum(axis=1);baseline=float(np.median(trace[pre]));onpower=float(np.median(trace[on]));postpower=float(np.median(trace[post]))
s={'frequency_hz':r['frequency_hz'],'uncalibrated_peak_hz':float(carrier),'signal_above_baseline_db':float(10*np.log10(onpower/baseline)),'signal_drop_after_restart_db':float(10*np.log10(onpower/postpower)),'post_restart_vs_baseline_db':float(10*np.log10(postpower/baseline)),'capture_start_utc':m['timestamps']['retained_capture_start_utc'],'scheduled_start_s':start,'restart_requested_s':stop,'service_ready_s':ready,'uncancelled_end_s':start+r['duration_s'],'fft_window_seconds':n/fs,'sample_count':len(iq),'iq_sha256':digest,'overflow_count':m['overflow_count'],'clipped_sample_count':m['clipping']['sample_count'],'receiver_cleanup':m['cleanup']['outcome'],'boundary':'Corresponding signal visibility and disappearance on service restart; approximate host/capture time alignment and uncalibrated frequency/power.'}
(root/'sdr-analysis.json').write_text(json.dumps(s,indent=2)+'\n')
fig,(ax,bx)=plt.subplots(2,1,figsize=(11,7),sharex=True,layout='constrained',height_ratios=[2,1]);db=10*np.log10(power+1e-30)
ax.pcolormesh(t,f-r['frequency_hz'],db.T,shading='auto',cmap='magma',vmin=np.percentile(db,25),vmax=np.percentile(db,99.8));ax.set(ylabel='Offset from 14.097100 MHz (Hz)',title='Installed service restart — wspr4 Si5351; RF output uncabled')
bx.plot(t,10*np.log10(trace/baseline),color='#2266aa',lw=1.5);bx.set(ylabel='Signal / baseline (dB)',xlabel='Seconds from SDR capture start')
for a in [ax,bx]:
 for x,label,color in [(start,'Scheduled start','#51d2b5'),(stop,'Restart requested','#ffb74d'),(ready,'Service ready','#55cfff')]:a.axvline(x,color=color,linestyle='--',lw=1,label=label)
 a.set_xlim(max(0,start-3),min(t[-1],ready+12))
bx.legend(loc='upper right',fontsize=9,ncol=3);fig.savefig(root/'sdr-service-restart.png',dpi=150)
print(json.dumps(s,indent=2))
