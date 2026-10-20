#!/usr/bin/env python3
import argparse,json,pathlib
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=argparse.ArgumentParser();p.add_argument('before',type=pathlib.Path);p.add_argument('after',type=pathlib.Path);a=p.parse_args()
b=json.loads((a.before/'analysis.json').read_text());c=json.loads((a.after/'analysis.json').read_text())
for field in ['frequency_hz','gpsdo_frequency_hz','ppm','reference_hz','center_hz','sample_rate_hz','bandwidth_hz','gain_db']:
 assert b['identity'][field]==c['identity'][field],field
fig,axes=plt.subplots(4,2,figsize=(14,12));table=[]
for i,name in enumerate(b['cases']):
 for path,rec,label in [(a.before,b,'Before'),(a.after,c,'Candidate')]:
  env=np.loadtxt(path/name/'envelope.csv',delimiter=',',skiprows=1);psd=np.loadtxt(path/name/'spectrum.csv',delimiter=',',skiprows=1)
  t0=rec['cases'][name]['on_intervals_s'][0][0]
  axes[i,0].plot(env[:,0]-t0,10*np.log10(np.maximum(env[:,1],1e-12)),label=label,lw=.7)
  axes[i,1].plot(psd[:,0],psd[:,1],label=label,lw=.7)
 axes[i,0].set(title=name,ylabel='Relative RF channel power (dB)',ylim=(-40,3),xlim=(-.1,10.5));axes[i,1].set(title='Spectrum: carrier interiors / complete modulation sequence',ylabel='dB relative to carrier bin',ylim=(-80,5),xlim=(-2000,2000))
 row={'case':name}
 for label,rec in [('before',b['cases'][name]),('after',c['cases'][name])]:
  row[label]={k:rec[k] for k in ['frequency_mean_hz','frequency_span_hz','twenty_hz_fraction_within_5khz']}
  if 'transitions' in rec:
   row[label]['median_notch_below_minus10db_ms']=float(np.median([v['samples_below_minus_10db'] for v in rec['transitions']]))
   row[label]['median_absolute_phase_step_rad']=float(np.median([abs(v['phase_step_rad_extrapolated']) for v in rec['transitions']]))
  if 'burst_means_hz' in rec:row[label]['burst_means_hz']=rec['burst_means_hz']
 table.append(row)
for ax in axes.flat:ax.grid(alpha=.25);ax.legend()
axes[-1,0].set_xlabel('Seconds relative to first RF rise');axes[-1,1].set_xlabel('Offset from requested carrier (Hz)');fig.suptitle(c['identity']['stage']+' versus fresh devel before / '+c['identity']['band']);fig.tight_layout();fig.savefig(a.after/'versus-before.png',dpi=150);fig.savefig(a.after/'versus-before.pdf');(a.after/'versus-before.json').write_text(json.dumps(table,indent=2));print(json.dumps(table,indent=2))
