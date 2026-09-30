import datetime,hashlib,json
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
root=Path('/private/tmp/wtp-ini-evidence')
m=json.loads((root/'ini-cancel.json').read_text())
r=json.loads((root/'ini-cancellation.json').read_text())
iq_path=root/'ini-cancel.cf32'
digest=hashlib.sha256(iq_path.read_bytes()).hexdigest()
assert digest==m['output']['sha256']
assert m['primary_outcome']=='success' and m['cleanup']['outcome']=='verified'
fs=m['actual_settings']['sample_rate_hz'];center=m['actual_settings']['center_frequency_hz'];n=32768
iq=np.fromfile(iq_path,dtype='<c8');assert len(iq)==m['retained_sample_count']
blocks=iq[:len(iq)//n*n].reshape(-1,n)
f=np.fft.fftshift(np.fft.fftfreq(n,1/fs))+center
view=(f>14096500)&(f<14097700)
power=np.abs(np.fft.fftshift(np.fft.fft(blocks*np.hanning(n),axis=1),axes=1)[:,view])**2/n**2
f=f[view];t=(np.arange(len(blocks))+.5)*n/fs
stamp=datetime.datetime.fromisoformat(m['timestamps']['retained_capture_start_utc'].replace('Z','+00:00')).timestamp()
start=int(r['chosen']['start_utc_ns'])/1e9-stamp
write=int(r['ini_write']['write_finished_utc_ns'])/1e9-stamp
abort=int(r['cancellation_observed_utc_ns'])/1e9-stamp
normal_end=start+r['chosen']['duration_s']
pre=(t>start-2)&(t<start-.3);on=(t>start+.4)&(t<write-.2)
post=(t>abort+.4)&(t<normal_end-.3)
near=np.abs(f-r['chosen']['frequency_hz'])<150
delta=power[on].mean(axis=0)-power[pre].mean(axis=0)
idx=np.flatnonzero(near)[np.argmax(delta[near])];carrier=f[idx]
channel=np.abs(f-carrier)<12
trace=power[:,channel].sum(axis=1)
baseline=float(np.median(trace[pre]));onpower=float(np.median(trace[on]));postpower=float(np.median(trace[post]))
on_db=float(10*np.log10(onpower/baseline));drop_db=float(10*np.log10(onpower/postpower))
post_db=float(10*np.log10(postpower/baseline))
summary={'acceptance_criterion':'Corresponding SDR signal visible during the job and disappearing on cancellation; visual review required.',
    'frequency_hz':r['chosen']['frequency_hz'],'sdr_peak_hz_uncalibrated':float(carrier),
    'signal_above_prejob_baseline_db':on_db,'signal_drop_after_cancellation_db':drop_db,
    'post_cancellation_vs_prejob_baseline_db':post_db,'capture_start_utc':m['timestamps']['retained_capture_start_utc'],
    'fft_window_seconds':n/fs,
    'scheduled_start_s':start,'ini_write_s':write,'abort_observed_s':abort,'uncancelled_end_s':normal_end,
    'sample_count':len(iq),'iq_sha256':digest,'overflow_count':m['overflow_count'],
    'clipped_sample_count':m['clipping']['sample_count'],'receiver_cleanup':m['cleanup']['outcome'],
    'boundary':'SDR visibility and cancellation only; uncalibrated frequency/power and approximate capture-to-host time alignment.'}
(root/'sdr-analysis.json').write_text(json.dumps(summary,indent=2)+'\n')
fig,(ax,trace_ax)=plt.subplots(2,1,figsize=(11,7),sharex=True,layout='constrained',height_ratios=[2,1])
db=10*np.log10(power+1e-30)
ax.pcolormesh(t,f-r['chosen']['frequency_hz'],db.T,shading='auto',cmap='magma',
              vmin=np.percentile(db,25),vmax=np.percentile(db,99.8))
ax.set(ylabel='Offset from 14.097100 MHz (Hz)',title='INI cancellation — wspr4 Si5351; RF output uncabled')
trace_ax.plot(t,10*np.log10(trace/baseline),color='#2266aa',lw=1.5)
trace_ax.set(ylabel='Signal / baseline (dB)',xlabel='Seconds from SDR capture start')
for axis in (ax,trace_ax):
    for x,label,color in [(start,'Scheduled start','#51d2b5'),(write,'INI Enable write','#ffb74d'),
                          (abort,'Abort observed','#55cfff'),(normal_end,'Original job end','#b6adce')]:
        axis.axvline(x,color=color,linestyle='--',lw=1,label=label)
    axis.set_xlim(3,20)
trace_ax.legend(loc='upper right',fontsize=8,ncol=2)
fig.savefig(root/'sdr-ini-cancellation.png',dpi=150)
print(json.dumps(summary,indent=2))
