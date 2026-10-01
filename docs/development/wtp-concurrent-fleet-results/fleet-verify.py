import json,sys
from pathlib import Path

stage=Path(sys.argv[1])
plan=json.loads((stage/'run-plan.json').read_text())
expected={str(x*1000000000) for x in plan['slots']}
reports={}
active={s:[] for s in expected}
errors=[]
local_active=[]
for line in (stage/'observations.jsonl').read_text().splitlines():
    obs=json.loads(line)
    if 'error' in obs:
        errors.append(obs)
        continue
    f=obs['fleet']
    if obs['local_endpoint']['local_work_active']:local_active.append(obs['utc_ns'])
    for a in f['assignments']:
        w=f['outputs'][a['device_id']]['wtp']
        report=w.get('last_report')
        if report: reports[(a['device_id'],report['job_id'])]={'name':a['name'],'device_id':a['device_id'],**report}
    for s in expected:
        if all(f['outputs'][a['device_id']]['wtp'].get('start_utc_ns')==s and (f['outputs'][a['device_id']]['wtp'].get('remote') or {}).get('output_active') is True for a in f['assignments']):
            active[s].append(obs['utc_ns'])
completed=[r for r in reports.values() if r['start_utc_ns'] in expected and r['outcome']=='complete']
final=json.loads((stage/'final-controller.json').read_text())
rows=final['fleet']['assignments']
ep=final['local_endpoint']
per_output={a['device_id']:{'name':a['name'],'completed':[r for r in completed if r['device_id']==a['device_id']]} for a in rows}
valid_complete=all(len(v['completed'])==3 and {r['start_utc_ns'] for r in v['completed']}==expected and all(r['execution']['ok'] and r['execution']['cleanup']['ok'] and r['job']['state']=='complete' and not r['job']['output_active'] for r in v['completed']) for v in per_output.values())
off=all(not a['enabled'] and not a['in_flight'] and not final['fleet']['outputs'][a['device_id']]['wtp']['owns'] and not final['fleet']['outputs'][a['device_id']]['wtp']['uncertain'] for a in rows) and not ep['local_requested'] and not ep['local_work_active'] and not ep['output_unknown']
result={'five_remote_targets_three_slots_passed':valid_complete and all(active.values()),'completed_jobs':len(completed),'per_output':per_output,'simultaneously_active_observations':active,'observer_errors':errors,'all_outputs_paused_known_off':off,'local_work_active_samples':local_active,'local_frame_passed':False,'local_frame_failure':'INI-driven RP1 scheduler remains route-inhibited; positional preparation exception excludes use_ini. Managed ExecStart also refuses extra direct-CLI confirmation argument.','all_seen_reports':list(reports.values())}
(stage/'controller-analysis.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k not in ('per_output','all_seen_reports','simultaneously_active_observations')}))
