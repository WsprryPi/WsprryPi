#!/usr/bin/env python3
"""Wide-channel edge traces expose duty-cycle chopping hidden by narrow filtering."""
import argparse,json,pathlib
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=argparse.ArgumentParser();p.add_argument('directory',type=pathlib.Path);a=p.parse_args()
root=a.directory;record=json.loads((root/'analysis.json').read_text());ident=record['identity'];fs=ident['sample_rate_hz'];fig,axes=plt.subplots(2,2,figsize=(12,6));summary={}
for row,name in enumerate(['keyed-none','keyed-raised_cosine']):
 d=root/name;x=np.fromfile(d/'capture.cf32',dtype='<c8');n=np.arange(len(x));z=x*np.exp(-2j*np.pi*(ident['frequency_hz']-ident['center_hz'])*n/fs)
 hz=abs(np.fft.fftfreq(len(z),1/fs));filt=np.where(hz<=3500,1,np.where(hz<4500,.5*(1+np.cos(np.pi*(hz-3500)/1000)),0));z=np.fft.ifft(np.fft.fft(z)*filt)[12::25];t=(12+25*np.arange(len(z)))/fs;power=abs(z)**2
 onset=record['cases'][name]['on_intervals_s'][0][0];level=np.median(power[(t>onset+.1)&(t<onset+.3)]);power/=level
 selected=(t>onset-.04)&(t<onset+.7);np.savetxt(d/'wide-envelope.csv',np.column_stack((t[selected]-onset,power[selected])),delimiter=',',header='seconds_relative_to_narrow_threshold,relative_power',comments='')
 for col,(lo,hi) in enumerate([(-.025,.045),(.455,.65)]):
  axes[row,col].plot((t[selected]-onset)*1000,power[selected],lw=.7);axes[row,col].set(xlim=(lo*1000,hi*1000),ylim=(-.05,1.5),title=name+(' rise' if col==0 else ' fall'),xlabel='Milliseconds from narrow-channel rise threshold',ylabel='Relative RF power');axes[row,col].grid(alpha=.3)
 # Last falling threshold after each key, compared with half-second duration.
 durations=[]
 for start,end in record['cases'][name]['on_intervals_s']:
  durations.append(end-start)
 summary[name]={'narrow_threshold_on_durations_s':durations,'median_on_duration_s':float(np.median(durations))}
fig.suptitle(ident['stage']+' / '+ident['band']+' / 3.5 kHz passband RF envelope');fig.tight_layout();fig.savefig(root/'keying-edges.png',dpi=150);fig.savefig(root/'keying-edges.pdf');(root/'keying-edges.json').write_text(json.dumps(summary,indent=2))
