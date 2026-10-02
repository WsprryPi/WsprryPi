"""Verify recorded Fleet slots and independent target state observations."""
import json
import sys
from pathlib import Path

stage = Path(sys.argv[1])
plan = json.loads((stage / 'plan.json').read_text())
accepted_first_four = '--accepted-first-four' in sys.argv[2:]
cases_to_check = plan['cases'][:4] if accepted_first_four else plan['cases']
all_slots = list(range(plan['first_slot'], plan['last_slot'] + 1, 120))
slots = [s for s in all_slots if s <= cases_to_check[-1]['clean_slot']] if accepted_first_four else all_slots
warmup = [json.loads((stage / 'startup-round.json').read_text())['observed_startup_slot']] if (stage / 'startup-round.json').exists() else []
observed_slots = warmup + all_slots
clean = ([] if plan.get('restart_only') else [plan['first_slot']]) + [c['clean_slot'] for c in cases_to_check]
if not accepted_first_four:
    clean += [plan['last_slot']]
reports, jobs, watermarks, regressions, errors = {}, {}, {}, [], []
ids = plan['expected_ids']
by_id = {v:k for k,v in ids.items()}
for line in (stage / 'controller-observations.jsonl').open():
    observation = json.loads(line)
    if 'fleet' not in observation:
        errors.append(observation)
        continue
    for assignment in observation['fleet']['assignments']:
        identity = assignment['device_id']
        watermark = int(assignment['last_start_ns'])
        if watermark < watermarks.get(identity, 0):
            regressions.append({'device_id':identity, 'utc_ns':observation['utc_ns'], 'watermark':watermark})
        watermarks[identity] = watermark
        wtp = observation['fleet']['outputs'][identity]['wtp']
        report = wtp.get('last_report')
        if report:
            reports[(identity, report['job_id'])] = report
        remote = wtp.get('remote') or {}
        if remote.get('state') == 'running' and remote.get('output_active'):
            key = (identity, remote.get('boot_id'), remote.get('job_id'))
            entry = jobs.setdefault(key, {'name':by_id[identity], 'device_id':identity,
                'boot_id':remote.get('boot_id'), 'job_id':remote.get('job_id'),
                'first_running_utc_ns':observation['utc_ns'], 'starts':set(), 'observations':0})
            entry['starts'].add(wtp.get('start_utc_ns'))
            entry['observations'] += 1
independent = {}
unexpected = []
for name in ids:
    observations, terminal, observer_errors, identity_errors = {s:[] for s in observed_slots}, {}, [], []
    for line in (stage / (name.replace(' ','-') + '-observer.jsonl')).open():
        row = json.loads(line)
        if 'status' not in row:
            observer_errors.append(row)
            continue
        if row['hello']['device_id'] != ids[name] or row['hello']['product'] != ('WsprryPico' if name.startswith('Pico') else 'WsprryPi'):
            identity_errors.append(row['hello'])
        state = row['status']
        if state['state'] == 'running' and state['output_active']:
            now = int(row['utc_ns']) / 1e9
            matching = [s for s in observed_slots if s-.5 <= now <= s+11]
            if not matching:
                unexpected.append({'name':name, 'utc_ns':row['utc_ns'], 'status':state})
            for s in matching:
                observations[s].append(row['utc_ns'])
        for record in state.get('terminal_records',[]):
            terminal[(state['boot_id'], record['job_id'])] = record
    independent[name] = {'per_slot_running_counts':{str(s):len(v) for s,v in observations.items()},
        'terminal_records':list(terminal.values()), 'observer_errors':observer_errors, 'identity_errors':identity_errors,
        'owner_and_job_ids_independently_observed':not name.startswith('Pico')}
per_output = {}
for name, identity in ids.items():
    completed = [r for (i,j),r in reports.items() if i == identity and
        int(r['start_utc_ns']) // 1000000000 in clean and r['outcome']=='complete']
    starts = [int(r['start_utc_ns']) // 1000000000 for r in completed]
    per_output[name] = {'clean_slots':clean, 'completed_clean_reports':completed,
        'all_clean_slots_complete': sorted(starts)==sorted(clean) and all(
            r['execution']['ok'] and r['execution']['cleanup']['ok'] and
            r['job']['state']=='complete' and not r['job']['output_active'] for r in completed)}
by_slot = {}
for value in jobs.values():
    value['starts'] = sorted(value['starts'])
    for start in value['starts']:
        by_slot.setdefault((value['device_id'],start),set()).add(value['job_id'])
duplicates = [{'device_id':i,'start_utc_ns':s,'job_ids':sorted(j)} for (i,s),j in by_slot.items() if len(j)>1]
cases = {c['name']:json.loads((stage / (c['name']+'-result.json')).read_text()) for c in cases_to_check
         if (stage / (c['name']+'-result.json')).exists()}
result = {'scope': 'Original first four cases only; original target-restart failure remains excluded and recorded.' if accepted_first_four else 'Full recorded plan',
    'all_requested_cases_passed':len(cases)==len(cases_to_check) and all(c.get('passed') for c in cases.values()),
    'all_clean_rounds_complete':all(v['all_clean_slots_complete'] for v in per_output.values()),
    'five_independent_running_outputs_in_all_requested_slots':all(all(v['per_slot_running_counts'][str(s)] for s in slots) for v in independent.values()),
    'startup_slots_recorded_separately':warmup,
    'per_output':per_output, 'independent':independent, 'observed_running_jobs':list(jobs.values()),
    'observed_reports':list(reports.values()), 'duplicate_slot_jobs':duplicates,
    'watermark_regressions':regressions, 'unexpected_running':unexpected,
    'controller_observation_errors':errors,
    'no_duplicate_or_replayed_slot_observed':not duplicates and not regressions and not unexpected,
    'expected_identity_observed_on_all_outputs':not any(v['identity_errors'] for v in independent.values()),
    'note':'Pico USB INFO reports actual shared JobService state/output; ownership and remote job IDs come from controller protocol records and independent final LAN inspection.'}
(stage / ('accepted-controller-analysis.json' if accepted_first_four else 'controller-analysis.json')).write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if isinstance(v,bool)}))
