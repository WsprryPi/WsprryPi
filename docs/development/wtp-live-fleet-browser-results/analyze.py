"""Join live browser schedules, WTP completions, independent status and SDR IQ.

Run on wspr5 with NumPy; raw IQ remains outside Git.
"""
import datetime,json,sys
from pathlib import Path
import numpy as np
stage=Path(sys.argv[1])
ids={'wspr2':'e2a4d273f7d8f24f6696f7c6fb95ff11','Pico A':'fd6127d11d6aca42a9905fa3fb1bf1d5'}
rows=[json.loads(s) for s in (stage/'controller-observations.jsonl').read_text().splitlines()]
reports={}; local_off=True
for r in rows:
 if 'fleet' not in r: continue
 local_off &= not r['wtp-endpoint']['local_requested'] and not r['wtp-endpoint']['local_work_active'] and not r['wtp-endpoint']['output_unknown']
 for a in r['fleet']['assignments']:
  if a['device_id'] not in ids.values():
   assert not a['enabled'] and not a['in_flight'],a['name']
 for name,id in ids.items():
  report=r['fleet'].get('outputs',{}).get(id,{}).get('wtp',{}).get('last_report')
  if report and report['outcome']=='complete':reports[(name,report['job_id'])]=report
meta=json.loads((stage/'browser-capture.json').read_text())
t0=datetime.datetime.fromisoformat(meta['timestamps']['retained_capture_start_utc'].replace('Z','+00:00')).timestamp()
iq=np.memmap(stage/'browser.cf32',dtype='<c8',mode='r'); rate=250000;n=65536
freq=np.fft.fftfreq(n,1/rate)+14075100;window=np.hanning(n)
results=[]
for (name,jid),report in reports.items():
 start=int(report['start_utc_ns'])/1e9
 first=start==1790937961
 hz=(14101100 if name=='wspr2' else 14100100)+(0 if first else 200)
 duration=8 if first else 6
 assert start in (1790937961,1790938093),start
 assert report['execution']['ok'] and report['execution']['cleanup']['ok']
 assert report['job']['authoritative'] and report['job']['state']=='complete' and not report['job']['output_active']
 mask=np.abs(freq-hz)<100; noise=(np.abs(freq-hz)>500)&(np.abs(freq-hz)<3000)
 for carrier in (14098100,14099100,14100100,14100300,14101100,14101300,14102100):noise &= np.abs(freq-carrier)>300
 on=[];off=[];peaks=[]
 for lo,hi,out in ((start+.6,start+duration-.6,on),(start+duration+1,start+duration+4,off)):
  for utc in np.arange(lo,hi,.25):
   offset=round((utc-t0)*rate)-n//2;assert offset>=0 and offset+n<=len(iq)
   power=np.abs(np.fft.fft(np.array(iq[offset:offset+n])*window))**2
   bins=np.flatnonzero(mask);peak=bins[np.argmax(power[bins])]
   out.append(float(10*np.log10(power[peak]/max(float(np.median(power[noise])),1e-30))))
   if out is on:peaks.append(float(freq[peak]))
 observations=[json.loads(s) for s in (stage/('wspr2-observations.jsonl' if name=='wspr2' else 'Pico-A-observations.jsonl')).read_text().splitlines()]
 active=[r for r in observations if 'status' in r and start+.1<int(r['utc_ns'])/1e9<start+duration-.1 and r['status']['output_active']]
 assert active,(name,jid)
 if name=='wspr2':assert all(r['status']['job_id']==jid for r in active)
 results.append({'target':name,'job_id':jid,'start_utc_ns':report['start_utc_ns'],'frequency_hz':hz,'duration_seconds':duration,
  'report':report,'independent_active_observations':len(active),'independent_job_id_available':name=='wspr2',
  'minimum_on_snr_db':min(on),'median_on_snr_db':float(np.median(on)),'median_off_snr_db':float(np.median(off)),
  'median_peak_hz':float(np.median(peaks)), 'corresponding_signal_observed':min(on)>30 and float(np.median(on))-float(np.median(off))>15})
assert len(results)==4 and local_off
out={'jobs':results,'local_off_throughout':local_off,'capture':meta,
 'criterion':'Corresponding SDR signal: minimum 30 dB peak over nearby masked noise and median on/off contrast over 15 dB. No calibrated frequency, edge timing or RF-chain qualification.',
 'all_corresponding_signals_observed':all(r['corresponding_signal_observed'] for r in results)}
(stage/'acceptance.json').write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps({k:v for k,v in out.items() if k not in ('capture','jobs')}))
