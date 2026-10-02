"""Adversarial checks of actual fault/recovery checkpoints, not pass labels."""
import json
import sys
from pathlib import Path

stage = Path(sys.argv[1])
plan = json.loads((stage / 'plan.json').read_text())
ids = plan['expected_ids']
accepted_first_four = '--accepted-first-four' in sys.argv[2:]
checks = []
for case in (plan['cases'][:4] if accepted_first_four else plan['cases']):
    record = json.loads((stage / (case['name'] + '-result.json')).read_text())
    affected = case['blocked'] or (list(ids) if case['name']=='controller-crash' else ['wspr2'])
    before = record['after_reconnect_before_recover']
    after = record['after_recovery']
    for name in affected:
        status = before['remote'][name]['status']
        assert not status['output_active'], (case['name'], name)
        if name.startswith('Pico'):
            assert not before['remote'][name]['owner_and_remote_job_id_observed']
        else:
            assert status['owner_id'] is None
        assert status['state'] in ('empty','complete','aborted','missed')
        wtp = after['fleet']['outputs'][ids[name]]['wtp']
        assert wtp['identity']['device_id']==ids[name]
        assert wtp['identity']['product']==('WsprryPico' if name.startswith('Pico') else 'WsprryPi')
        assert wtp['remote'] is not None and not wtp['remote']['owner_id'] and not wtp['remote']['output_active']
        assert not wtp['uncertain'] and not wtp['safety_fault'] and not wtp['owns']
        if case['name']=='target-restart':
            old_boot=record['running_before_fault'][name]['hello']['boot_id']
            new_boot=before['remote'][name]['hello']['boot_id']
            assert old_boot!=new_boot
            assert wtp['identity']['boot_id']==new_boot==wtp['remote']['boot_id']
    for name in ids:
        running = record['recovered_running'][name]
        assert running['hello']['device_id']==ids[name]
        assert running['status']['state']=='running' and running['status']['output_active']
        assert int(running['utc_ns'])//1000000000 >= case['clean_slot']
        output = record['after_clean_slot']['fleet']['outputs'][ids[name]]['wtp']
        report = output['last_report']
        assert report['outcome']=='complete' and int(report['start_utc_ns'])==case['clean_slot']*1000000000
        assert report['execution']['ok'] and report['execution']['cleanup']['ok']
        assert report['job']['state']=='complete' and not report['job']['output_active']
        if name not in affected:
            report = before['fleet']['outputs'][ids[name]]['wtp']['last_report']
            assert report['outcome']=='complete' and int(report['start_utc_ns'])==case['fault_slot']*1000000000
    checks.append({'name':case['name'], 'affected':affected, 'known_off_before_recovery':True,
                   'fresh_expected_identity_unowned_after_recovery':True,
                   'all_five_future_slot_completions':True, 'unaffected_fault_slot_completions':True})
result = {'cases':checks, 'checkpoint_assertions_passed':True,
    'scope':'Original first four cases only; target-restart failure excluded.' if accepted_first_four else 'Full recorded plan',
    'pico_ownership_evidence':'Fresh controller WTP STATUS after recovery; USB INFO has no ownership fields.'}
(stage / ('accepted-case-verification.json' if accepted_first_four else 'case-verification.json')).write_text(json.dumps(result,indent=2)+'\n')
print(str(len(checks)) + ' fault/recovery checkpoint assertions passed')
