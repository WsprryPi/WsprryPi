#!/usr/bin/env python3
"""Assemble compact, source-bound campaign evidence without copying raw IQ."""
import argparse, json, pathlib, re, shutil, statistics
p=argparse.ArgumentParser();p.add_argument('artifacts',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
stages=['before-r2','step1','step2','step3-r2','step4','step5'];rows=[];manifest=[];timings=[]
runs=[(stage,band) for stage in stages for band in ['40m','2m']]+[('step5-r2','40m')]
for stage,band in runs:
  name=stage+'-'+band;root=a.artifacts/name;record=json.loads((root/'analysis.json').read_text());ident=record['identity'];cleanup=json.loads((root/'cleanup.json').read_text())
  assert cleanup.get('tx_after',cleanup)['output_enable_register']==255
  assert cleanup['service']=='active' and cleanup['installed_hashes']==ident['installed_hashes']
  assert not cleanup.get('errors') and all(not cleanup['gpsdo'][key] for key in ['out1','out2','pps1'])
  target=a.output/name;target.mkdir(exist_ok=True)
  for file in ['analysis.json','identity.json','cleanup.json','comparison.pdf','versus-before.pdf','versus-before.json','keying-edges.pdf','keying-edges.json']:
   if (root/file).exists():shutil.copy2(root/file,target/file)
  if stage=='step5':
   shutil.copy2(root/'versus-before.png',target/'versus-before.png')
   shutil.copy2(root/'keying-edges.png',target/'keying-edges.png')
  for case,data in record['cases'].items():
   shutil.copy2(root/case/'transmitter.log',target/(case+'-transmitter.txt'))
   meta=json.loads((root/case/'metadata.json').read_text())
   assert meta['primary_outcome']=='success' and meta['overflow_count']==0 and meta['clipping']['sample_count']==0
   manifest.append({'run':name,'case':case,'source_revision':ident['source_revision'],'binary_sha256':ident['binary_sha256'],'iq_sha256':meta['output']['sha256'],'samples':meta['retained_sample_count'],'capture_start_utc':meta['timestamps']['retained_capture_start_utc'],'raw_path':str(root/case/'capture.cf32')})
  c=record['cases']['carrier-none'];t=record['cases']['transitions-none'];k=record['cases']['keyed-none'];f=record['cases']['keyed-raised_cosine']
  durations=[int(n) for n in re.findall(r'programming_us=(\d+)',(root/'transitions-none/transmitter.log').read_text())]
  if len(durations)>1:timings.append({'run':name,'first_us':durations[0],'retune_count':len(durations)-1,'retune_median_us':statistics.median(durations[1:]),'retune_range_us':[min(durations[1:]),max(durations[1:])]})
  rows.append({'run':name,'source':ident['source_revision'],'extra':ident['extra'],'carrier_offset_hz':c['frequency_mean_hz'],'burst_mean_span_hz':max(c['burst_means_hz'])-min(c['burst_means_hz']),'transition_gap_ms':statistics.median(v['samples_below_minus_10db'] for v in t['transitions']),'phase_step_rad':statistics.median(abs(v['phase_step_rad_extrapolated']) for v in t['transitions']),'transition_concentration_percent':100*t['twenty_hz_fraction_within_5khz'],'hard_concentration_percent':100*k['twenty_hz_fraction_within_5khz'],'fade_concentration_percent':100*f['twenty_hz_fraction_within_5khz']})
(a.output/'measurements.json').write_text(json.dumps(rows,indent=2));(a.output/'capture-manifest.json').write_text(json.dumps(manifest,indent=2))
(a.output/'programming-times.json').write_text(json.dumps(timings,indent=2)+'\n')
lines=['# Measurement tables','', 'All offsets use the simultaneously captured GPSDO. PPM remains +3.470680.', '', '| Run | Carrier offset (Hz) | Burst mean span (Hz) | Transition gap metric (ms) | Phase step metric (rad) | Transition concentration (%) |', '|---|---:|---:|---:|---:|---:|']
for r in rows:lines.append(f"| {r['run']} | {r['carrier_offset_hz']:+.4f} | {r['burst_mean_span_hz']:.4f} | {r['transition_gap_ms']:.0f} | {r['phase_step_rad']:.4f} | {r['transition_concentration_percent']:.4f} |")
lines+=['','Gap metric counts 1 ms samples below -10 dB in a transition window after 5 ms smoothing. Zero means no resolved gap by this method. Phase steps are extrapolated estimates. Concentration is power within +/-20 Hz of the indicated carrier divided by power within +/-5 kHz, including noise; it is not occupied bandwidth.','', '| Run | Hard-key concentration (%) | Raised-cosine duty-fade concentration (%) |','|---|---:|---:|']
for r in rows:lines.append(f"| {r['run']} | {r['hard_concentration_percent']:.4f} | {r['fade_concentration_percent']:.4f} |")
lines+=['','## Programming duration','', 'These are complete applyTone durations, including status reads and any readiness waits, not isolated I2C wire time. Instrumentation began in step 3; the initial baseline has no directly comparable duration log. Active writes remain individual. No before/after bus-speed percentage is claimed.','', '| Run | First programming (us) | Retune median (us) | Retune range (us) |','|---|---:|---:|---:|']
for r in timings:lines.append(f"| {r['run']} | {r['first_us']} | {r['retune_median_us']:.0f} | {r['retune_range_us'][0]}–{r['retune_range_us'][1]} |")
lines+=['','## Rendered comparisons','', '| Stage | 40 m | 2 m |','|---|---|---|']
for stage in stages[1:]:lines.append(f'| {stage} | [PDF]({stage}-40m/versus-before.pdf) | [PDF]({stage}-2m/versus-before.pdf) |')
lines+=['','The final-source 40 m repetition is [retained separately](step5-r2-40m/versus-before.pdf). Final-source 2 m and guarded-default 40 m failed during transitions; guarded-default 2 m was not run. These failures are recorded in [rejected attempts](rejected-attempts.json), not included in the successful comparison table.']
(a.output/'measurements.md').write_text('\n'.join(lines)+'\n')
rejected=[]
for name in ['step3-40m','step5-r2-2m','final-default-40m']:
 root=a.artifacts/name;ident=json.loads((root/'identity.json').read_text());cleanup=json.loads((root/'cleanup.json').read_text());target=a.output/name;target.mkdir(exist_ok=True)
 assert cleanup.get('tx_after',cleanup)['output_enable_register']==255 and cleanup['service']=='active'
 assert cleanup['installed_hashes']==ident['installed_hashes'] and not cleanup.get('errors')
 assert all(not cleanup['gpsdo'][key] for key in ['out1','out2','pps1'])
 for file in ['identity.json','cleanup.json','coordinator-failure.log']:
  destination=target/(file.replace('.log','.txt'))
  if file.endswith('.log'):destination.write_text((root/file).read_text().rstrip()+'\n')
  else:shutil.copy2(root/file,destination)
 tx_logs=list((root/'transitions-none').glob('*.log'))
 for file in tx_logs:shutil.copy2(file,target/(file.stem+'.txt'))
 rejected.append({'run':name,'source_revision':ident['source_revision'],'extra':ident['extra'],'outcome':'failed during transitions; remaining scenarios not run','cleanup':'verified','evidence_directory':name})
(a.output/'rejected-attempts.json').write_text(json.dumps(rejected,indent=2)+'\n')
print(json.dumps({'successful_captures':len(manifest),'retained_complex_samples':sum(r['samples'] for r in manifest),'runs':len(rows)},indent=2))
