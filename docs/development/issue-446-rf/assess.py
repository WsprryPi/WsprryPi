"""Offline assessment of the four-burst Issue 446 matched RF comparison."""
import datetime, json, pathlib, sys
import numpy as np

root=pathlib.Path(sys.argv[1])
rows=json.loads((root/'frequency-windows.json').read_text())
meta=json.loads((root/'metadata.json').read_text())
tx=json.loads(pathlib.Path(sys.argv[2]).read_text()) if len(sys.argv)>2 else json.loads((root/'result.json').read_text())
start=datetime.datetime.fromisoformat(meta['timestamps']['retained_capture_start_utc'].replace('Z','+00:00')).timestamp()
events={e['event']:e for e in tx['events']}
groups=[]
for index in range(4):
    begin=events['request_begin-'+str(index)]['time_unix']-start
    end=events['bounded_tone-'+str(index)]['time_unix']-start
    transaction=events['bounded_tone-'+str(index)]['value']
    assert transaction['duration_ms']==5000 and transaction['completed']
    groups.append([r for r in rows if r['second']>=begin+.5 and r['second']+1<=end-.5])
order=['old','new','new','old']
data=[]; bursts=[]
for index,(kind,group) in enumerate(zip(order,groups)):
    interior=group
    assert len(interior)>=2, 'Too few steady carrier windows'
    assert all(r['tone']['power']>1000 and r['tone']['contrast_db']>30 and r['low_ref']['contrast_db']>30 for r in interior)
    values=[r['tone']['hz']-r['low_ref']['hz']+10000 for r in interior]
    bursts.append({'index':index,'build':kind,'interior_seconds':[r['second'] for r in interior], 'mean_reference_relative_offset_hz':float(np.mean(values)), 'stddev_hz':float(np.std(values,ddof=1))})
    data.extend((r['second']+.5, int(kind=='new'), value) for r,value in zip(interior,values))
t0=data[0][0]
x=np.array([[1,t-t0,new] for t,new,y in data])
y=np.array([y for t,new,y in data])
beta=np.linalg.lstsq(x,y,rcond=None)[0]
residual=y-x@beta
rms=float(np.sqrt(np.mean(residual**2)))
variance=float(np.sum(residual**2)/(len(y)-3))
se=float(np.sqrt((variance*np.linalg.inv(x.T@x))[2,2]))
expected=2.197265625
capture_ok=(meta['primary_outcome']=='success' and meta['overflow_count']==0 and meta['timeout_count']==0 and meta['clipping']['sample_count']==0 and meta['cleanup']['outcome']=='verified')
passed=bool(abs(beta[2]-expected)<=.3 and rms<.15 and capture_ok)
result={'scope':'Issue 446 old-spacing control versus installed fix, not absolute transmitter calibration','sequence':order,'burst_statistics':bursts,'fit_model':'reference-relative offset = baseline + drift_hz_per_s * elapsed_s + fixed_build_shift_hz * is_fixed_build','fit_baseline_hz':float(beta[0]),'fit_drift_hz_per_s':float(beta[1]),'fixed_minus_control_hz':float(beta[2]),'expected_shift_hz':expected,'difference_from_expected_hz':float(beta[2]-expected),'effect_standard_error_hz':se,'fit_residual_rms_hz':rms,'steady_window_count':len(y),'acceptance_error_hz':.3,'maximum_residual_rms_hz':.15,'capture_integrity_passed':capture_ok,'passed':passed,'limitations':['No absolute GPIO calibration claim; manual correction was fixed at 0 PPM.','Reference subtraction removes common receiver tuning drift; no receiver correction profile was applied.','A 10 ppm sample-rate error changes a 2.2 Hz matched difference by only 0.000022 Hz.','Control is current source with only legacy TONE spacing restored, not a historical released binary.']}
(root/'assessment.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
raise SystemExit(0 if passed else 1)
