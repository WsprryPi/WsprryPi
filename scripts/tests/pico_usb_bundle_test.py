#!/usr/bin/env python3
"""Behavioral distribution/recovery/cache tests; no USB or network access."""
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from email.message import Message

from pico_usb_bundle_fixture import API, Network, POLICY, SERIAL, encoded, fixture

SCRIPTS=Path(__file__).resolve().parents[1]
def load(name):
    spec=importlib.util.spec_from_file_location(name,SCRIPTS/(name+'.py'))
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
bundle=load('pico_usb_bundle');entry=load('pico_usb_entry')


class DistributionTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.base=Path(self.temp.name).resolve()
        self.store=bundle.Store(self.base/'installer',POLICY,owner=False)
        self.cache=self.base/'cache';self.cache.mkdir();self.state=self.base/'state';self.state.mkdir()
        self.manifest,self.payload=fixture();self.net=Network(self.manifest,self.payload)
    def tearDown(self):self.temp.cleanup()
    def select(self):return self.store.refresh(self.net,force=True)['identity']
    def invoke(self,*args):return entry.run(list(args),bundle,self.store,self.cache,self.state)
    def test_compatible_update_changes_no_pi_policy_or_adapter(self):
        initial=self.select();policy=copy.deepcopy(POLICY)
        newer=Network(*fixture(101,'0.1.2'))
        current=self.store.refresh(newer,force=True)
        self.assertNotEqual(initial,current['identity']);self.assertEqual(current['update']['state'],'current')
        self.assertEqual(self.store.load(current['identity'])[0]['installer_version'],'0.1.2')
        self.assertEqual(POLICY,policy)
    def test_erase_without_image_plan_retains_exact_installer_for_offline_recovery(self):
        identity=self.select()
        with patch.object(self.store,'refresh',return_value={'identity':identity,'update':{'state':'current'}}):
            code,result=self.invoke('erase','--device',SERIAL,'--board','pico2_w','--yes')
        self.assertEqual(code,0);self.assertEqual(result['operation'],'erase')
        self.assertEqual(list(self.cache.iterdir()),[])
        transaction=Path(result['transaction'])
        self.assertTrue((transaction.parent.parent/'usb-installer.json').is_file())
        newer=self.store.refresh(Network(*fixture(101,'0.1.2')),force=True)
        self.assertNotEqual(newer['identity'],identity)
        with patch.object(self.store,'refresh',side_effect=AssertionError('recovery must stay offline')):
            code,result=self.invoke('recover','--transaction',str(transaction),'--device',SERIAL,'--board','pico2_w','--yes')
        self.assertEqual(code,0);self.assertTrue(result['erase_verified'])
        self.assertEqual(result['installer_version'],'0.1.1')
        self.assertEqual(result['erased_bytes'],4194304)
    def test_old_core_refuses_erase_without_leaving_an_unrecoverable_hold(self):
        payload=dict(self.payload)
        payload['usb_installer.py']=payload['usb_installer.py'].replace(b'def erase_transaction(args): pass',b'')
        code,result=entry.invoke(self.manifest,payload,['erase'])
        self.assertEqual(code,2);self.assertFalse(result['recovery_required'])
        self.assertIn('lacks full erase',result['error'])
    def test_erase_retains_expired_host_backups_before_and_after_execution(self):
        identity=self.select();bucket=self.store.snapshot(self.state,identity);backups=[]
        for index in range(4):
            directory=bucket/(SERIAL+'-old-'+str(index));directory.mkdir()
            (directory/'transaction.json').write_bytes(encoded({'schema':'wsprrypico-usb-transaction/1',
                'serial':SERIAL,'stage':'complete','recovery_required':False}))
            backup=directory/'before.bin';backup.write_bytes(b'private fixture backup');backups.append(backup)
            os.utime(directory,(1,1))
        with patch.object(self.store,'refresh',return_value={'identity':identity,'update':{'state':'current'}}):
            code,result=self.invoke('erase','--device',SERIAL,'--board','pico2_w','--yes')
        self.assertEqual(code,0);self.assertEqual(result['operation'],'erase')
        self.assertTrue(all(p.read_bytes()==b'private fixture backup' for p in backups))
    def test_verified_ci_and_immutable_source_checks_fail_closed(self):
        changes=[('head_sha','c'*40),('conclusion','failure'),('status','in_progress'),
                 ('path','other.yml'),('workflow_id',124),('event','pull_request'),
                 ('repository',{'full_name':'fork/WsprryPico'}),('head_repository',{'full_name':'fork/WsprryPico'}),
                 ('run_attempt',2),('size',1),('sha','c'*40),('encoding','hex')]
        for field,value in changes:
            net=Network(*fixture())
            def mutate(record):
                if field in record:record[field]=value
                return record
            net.mutate=mutate
            with self.subTest(field=field),self.assertRaises(bundle.Unavailable):bundle.published(net,'devel')
        self.assertFalse((self.store.root/'active.json').exists())
    def test_incompatible_contract_runtime_and_payload_are_never_executed(self):
        for field,value in [('contract',2),('python_min',[99,0]),('transaction_schema','other'),('result_schema','other'),('firmware_manifest_schema',2)]:
            manifest,payload=fixture();manifest[field]=value
            with self.subTest(field=field),self.assertRaises(bundle.Unavailable):bundle.validate(manifest,'devel')
        for name in self.payload:
            net=Network(*fixture());net.payload[name]+=b'changed'
            with self.subTest(name=name),self.assertRaises(bundle.Unavailable):bundle.published(net,'devel')
        net=Network(*fixture());net.payload['usb_installer.py']=net.payload['usb_installer.py'].replace(b'wsprrypico-usb-result/1',b'wsprrypico-usb-result/2');net.original=dict(net.payload)
        with self.assertRaisesRegex(bundle.Unavailable,'No verified compatible'):bundle.published(net,'devel')
    def test_large_commit_resolution_uses_compact_refs_and_immutable_source(self):
        manifest,payload=bundle.published(self.net,'devel')
        self.assertEqual(manifest['source'],self.manifest['source'])
        self.assertEqual(payload,self.payload)
        self.assertEqual(self.net.requests.count('git/ref/heads/devel'),1)
        self.assertFalse(any(path.startswith('commits/') for path in self.net.requests))
    def test_malformed_or_wrong_branch_refs_are_rejected(self):
        for reference in (
                {'ref':'refs/heads/main','object':{'type':'commit','sha':'a'*40}},
                {'ref':'refs/heads/devel','object':{'type':'tag','sha':'a'*40}},
                {'ref':'refs/heads/devel','object':{'type':'commit','sha':'invalid'}},
                {'ref':'refs/heads/devel','object':None}):
            net=Network(*fixture())
            net.mutate=lambda record:reference if 'ref' in record else record
            with self.subTest(reference=reference),self.assertRaises(bundle.Unavailable):
                bundle.published(net,'devel')
    def test_no_successful_ci_keeps_retained_installer(self):
        identity=self.select()
        self.net.mutate=lambda r: {**r,'workflow_runs':[]} if 'workflow_runs' in r else r
        result=self.store.refresh(self.net,force=True)
        self.assertEqual(result['identity'],identity)
        self.assertIn('No verified compatible',result['update']['reason'])
    def test_pending_tip_bootstraps_latest_verified_ancestor(self):
        self.net.head='c'*40
        selected=self.select()
        manifest,payload=self.store.load(selected)
        self.assertEqual(manifest['source'],self.manifest['source']);self.assertEqual(payload,self.payload)
        self.assertTrue(any(p.startswith('compare/') and 'page=2' in p for p in self.net.requests))
        self.assertFalse(any('head_sha=' in p for p in self.net.requests))
    def test_incompatible_newer_sources_fall_back_without_execution(self):
        changes=(
            lambda p:p['usb_installer.py'].replace(b'wsprrypico-usb-result/1',b'wsprrypico-usb-result/2'),
            lambda p:p['usb_installer.py'].replace(b'schema_bytes=None',b'other_schema=None'),
            lambda p:b'import sys\nif sys.version_info < (99,0):\n    raise SystemExit(2)\n'+p['usb_installer.py'],
            lambda p:p['usb_installer.py']+b'\nunsupported syntax here\n',
            lambda p:p['usb_installer.py'].replace(b"INSTALLER_VERSION='0.1.2'",b"INSTALLER_VERSION='invalid'"),
        )
        for change in changes:
            with self.subTest(change=change):
                latest=Network(*fixture(101,'0.1.2'))
                latest.payload['usb_installer.py']=change(latest.payload)
                latest.original=dict(latest.payload);latest.older=[self.net]
                with patch.object(entry,'invoke',side_effect=AssertionError('resolver executed installer')):
                    manifest,payload=bundle.published(latest,'devel')
                self.assertEqual(manifest['source'],self.manifest['source']);self.assertEqual(payload,self.payload)
    def test_verified_sources_outside_branch_history_are_skipped(self):
        latest=Network(*fixture(101,'0.1.2'));latest.head='c'*40;latest.older=[self.net]
        latest.off_branch.add(latest.manifest['source'])
        manifest,_=bundle.published(latest,'devel')
        self.assertEqual(manifest['source'],self.manifest['source'])
    def test_bad_integrity_is_not_treated_as_an_incompatible_version(self):
        latest=Network(*fixture(101,'0.1.2'));latest.older=[self.net]
        latest.payload['usb_installer.py']+=b'changed'
        with self.assertRaisesRegex(bundle.Unavailable,'checksum mismatch'):bundle.published(latest,'devel')
        self.assertFalse(any('?ref='+self.manifest['source'] in p for p in latest.requests))
    def test_successful_runs_are_paginated_and_duplicate_sources_checked_once(self):
        versions=[Network(*fixture(110-i,'0.1.2')) for i in range(10)]
        latest=versions[0]
        for source in versions:
            source.manifest['source']=latest.head
            source.payload['usb_installer.py']=source.payload['usb_installer.py'].replace(b'wsprrypico-usb-result/1',b'wsprrypico-usb-result/2')
            source.original=dict(source.payload)
        latest.older=[*versions[1:],self.net]
        manifest,_=bundle.published(latest,'devel')
        self.assertEqual(manifest['source'],self.manifest['source'])
        self.assertTrue(any('runs?branch=devel&status=success&per_page=10&page=2' in p for p in latest.requests))
        self.assertEqual(sum(p.endswith('attempts/1') for p in latest.requests),2)
    def test_successful_run_search_is_bounded(self):
        self.net.payload['usb_installer.py']=self.net.payload['usb_installer.py'].replace(b'wsprrypico-usb-result/1',b'wsprrypico-usb-result/2')
        self.net.original=dict(self.net.payload);self.net.older=[self.net]*39
        with self.assertRaisesRegex(bundle.Unavailable,'No verified compatible'):bundle.published(self.net,'devel')
        self.assertEqual(sum('runs?branch=' in p for p in self.net.requests),3)
    def test_cold_plan_fetches_core_only_after_explicit_request(self):
        with patch.object(bundle,'Network',return_value=self.net):
            code,result=self.invoke('plan','--output',str(self.cache/('1'*32)))
        self.assertEqual(code,0);self.assertTrue(self.net.requests)
        self.assertEqual(result['installer_version'],'0.1.1');self.assertTrue(result['ok'])
        self.assertEqual(self.store.planned('1'*32),self.store.active()['identity'])
    def test_cold_erase_bootstraps_and_freezes_its_core(self):
        with patch.object(bundle,'Network',return_value=self.net):
            code,result=self.invoke('erase','--device',SERIAL,'--board','pico2_w','--yes')
        self.assertEqual(code,0);self.assertEqual(result['operation'],'erase')
        self.assertTrue((Path(result['transaction']).parent.parent/'usb-installer.json').is_file())
    def test_discovery_without_a_core_never_downloads_one(self):
        with patch.object(bundle,'Network',side_effect=AssertionError('discovery requested network')):
            with self.assertRaisesRegex(bundle.Unavailable,'download and check an image first'):self.invoke('discover')
        self.assertIsNone(self.store.active())
    def test_failed_cold_bootstrap_can_retry_without_a_partial_plan_or_transaction(self):
        self.net.failure=bundle.Unavailable('offline')
        with patch.object(bundle,'Network',return_value=self.net):
            with self.assertRaisesRegex(bundle.Unavailable,'offline'):
                self.invoke('plan','--output',str(self.cache/('1'*32)))
            self.assertIsNone(self.store.active());self.assertEqual(list(self.cache.iterdir()),[])
            self.assertFalse((self.store.root/'plans').exists());self.assertFalse(list(self.state.rglob('transaction.json')))
            self.net.failure=None
            self.assertEqual(self.invoke('plan','--output',str(self.cache/('1'*32)))[0],0)
    def test_failed_cold_erase_bootstrap_requires_no_device_recovery(self):
        self.net.failure=bundle.Unavailable('offline')
        with patch.object(bundle,'Network',return_value=self.net),patch.object(entry,'invoke',side_effect=AssertionError('USB invoked')):
            code,result=self.invoke('erase','--device',SERIAL,'--board','pico2_w','--yes')
        self.assertEqual(code,2);self.assertFalse(result['ok']);self.assertFalse(result['recovery_required'])
        self.assertEqual(result['operation'],'erase');self.assertEqual(result['error'],'offline')
        self.assertIsNone(self.store.active());self.assertFalse(list(self.state.rglob('transaction.json')))
    def test_malformed_ancestry_does_not_trigger_older_source_fallback(self):
        for field,value in [('base_commit',None),('merge_base_commit',{}),('status','invalid')]:
            net=Network(*fixture());net.head='c'*40
            def mutate(record):
                if 'base_commit' in record:record[field]=value
                return record
            net.mutate=mutate
            with self.subTest(field=field),self.assertRaisesRegex(bundle.Unavailable,'Invalid installer branch ancestry'):
                bundle.published(net,'devel')
    def test_channel_selection_never_substitutes_another_branch(self):
        self.net.mutate=lambda record: {**record,'head_branch':'main'} if 'head_branch' in record else record
        with self.assertRaisesRegex(bundle.Unavailable,'canonical source'):bundle.published(self.net,'devel')
        self.assertFalse(any('/contents/' in p for p in self.net.requests))
        main=Network(*fixture());main.manifest['channel']='main'
        manifest,_=bundle.published(main,'main');self.assertEqual(manifest['channel'],'main')
    def test_failed_verify_job_and_incomplete_jobs_refuse_source(self):
        for mode in ('failure','skipped','missing','extra'):
            net=Network(*fixture())
            def mutate(r):
                if 'jobs' in r:
                    if mode=='missing':r['jobs'].pop()
                    elif mode=='extra':r['jobs'].append(dict(r['jobs'][0]))
                    else:r['jobs'][0]['conclusion']=mode
                return r
            net.mutate=mutate
            with self.subTest(mode=mode),self.assertRaises(bundle.Unavailable):bundle.published(net,'devel')
    def test_malformed_source_or_api_metadata_retains_checked_installer(self):
        identity=self.select()
        for field,value in [('content',7),('repository',[]),('head_repository',[]),('jobs',[1]),('workflow_runs',[1])]:
            net=Network(*fixture())
            def mutate(r):
                if field in r:r[field]=value
                return r
            net.mutate=mutate
            result=self.store.refresh(net,force=True)
            self.assertEqual(result['identity'],identity)
            self.assertEqual(result['update']['state'],'retained')
    def test_legacy_receipt_metadata_stays_usable_without_network(self):
        manifest,payload=fixture()
        manifest.update(tag='retained-transaction-fixture',workflow='retired-workflow.yml')
        identity=self.store.save(manifest,payload)
        bucket=self.store.snapshot(self.state,identity)
        tx=bucket/(SERIAL+'-pending')/'transaction.json';tx.parent.mkdir();tx.write_text('{}')
        with patch.object(bundle,'Network',side_effect=AssertionError('offline recovery')):
            self.assertEqual(self.store.recovery(tx,self.state)[0],manifest)
    def test_absent_update_keeps_verified_tool_and_reports_actual_reason(self):
        identity=self.select();self.net.failure=bundle.Unavailable('GitHub rate limit. Retry after tomorrow UTC.',error_code='github_rate_limited',retry_at=9999999999)
        selected=self.store.refresh(self.net,force=True)
        self.assertEqual(selected['identity'],identity);self.assertEqual(selected['update']['error_code'],'github_rate_limited')
        requests=len(self.net.requests);self.store.refresh(self.net);self.assertEqual(len(self.net.requests),requests)
    def test_failure_without_previous_installer_never_installs(self):
        self.net.failure=bundle.Unavailable('offline')
        with self.assertRaises(bundle.Unavailable):self.select()
        self.assertFalse((self.store.root/'active.json').exists())
    def test_identical_legacy_bytes_migrate_despite_cross_workflow_run_order(self):
        manifest,payload=fixture();manifest.update(run_id=200,workflow='retired-workflow.yml')
        old=self.store.save(manifest,payload)
        bundle.write_json(self.store.root/'active.json',{'identity':old,'check_after':0,'update':{'state':'current'}})
        self.store.bind('a'*32,old)
        result=self.store.refresh(self.net,force=True)
        self.assertEqual(result['update']['state'],'current')
        self.assertNotEqual(result['identity'],old)
        self.assertEqual(self.store.load(result['identity'])[1],payload)
        self.assertEqual(self.store.planned('a'*32),old)
        self.assertEqual(self.store.load(old)[0],manifest)
    def test_channel_cannot_roll_back_retained_newer_installer(self):
        self.store.refresh(Network(*fixture(101,'0.1.2')),force=True)
        current=self.store.refresh(self.net,force=True)
        self.assertIn('newer verified version',current['update']['reason'])
        self.assertEqual(self.store.load(current['identity'])[0]['installer_version'],'0.1.2')
    def test_tampered_and_symlinked_retained_code_fail_even_when_offline(self):
        identity=self.select();path=self.store.root/'bundles'/identity/'usb_installer.py';data=path.read_bytes()
        path.write_bytes(data+b'changed')
        with self.assertRaises(bundle.Unavailable):self.store.refresh(self.net)
        path.unlink();outside=self.base/'outside';outside.write_bytes(data);path.symlink_to(outside)
        with self.assertRaises(bundle.Unavailable):self.store.load(identity)
    def test_upgrade_and_offline_recovery_use_checked_original_after_update(self):
        self.select();plan='1'*32
        code,result=self.invoke('plan','--output',str(self.cache/plan));self.assertEqual(code,0)
        self.assertEqual(result['marker'],'0.1.1')
        self.store.refresh(Network(*fixture(101,'0.1.2')),force=True)
        code,result=self.invoke('upgrade','--bundle',str(self.cache/plan),'--state-dir',str(self.state))
        self.assertEqual(result['marker'],'0.1.1');path=Path(result['transaction'])
        self.assertEqual(json.loads(path.read_text())['saved'],'untouched')
        # A restart can lose global installer caches; the transaction snapshot
        # alone must still recover with its original implementation offline.
        import shutil
        shutil.rmtree(self.store.root/'bundles'/self.store.planned(plan))
        with patch.object(bundle,'Network',side_effect=AssertionError('recovery requested network')):
            code,result=self.invoke('recover','--transaction',str(path))
        self.assertEqual(code,0);self.assertEqual(result['marker'],'0.1.1');self.assertEqual(result['saved'],'untouched')
    def test_lost_snapshot_refuses_instead_of_substituting_current_tool(self):
        identity=self.select();bucket=self.store.snapshot(self.state,identity)
        tx=bucket/(SERIAL+'-pending')/'transaction.json';tx.parent.mkdir();tx.write_text('{}')
        (bucket/'usb_installer.py').unlink()
        with patch.object(entry,'invoke',side_effect=AssertionError('unsafe substitute')):
            with self.assertRaises(bundle.Unavailable):self.invoke('recover','--transaction',str(tx))
    def test_legacy_recovery_keeps_frozen_pre_migration_core(self):
        identity=self.select();bundle.write_json(self.store.root/'legacy.json',{'identity':identity})
        tx=self.state/(SERIAL+'-old')/'transaction.json';tx.parent.mkdir();tx.write_text('{}')
        self.store.refresh(Network(*fixture(101,'0.1.2')),force=True)
        self.assertEqual(self.store.recovery(tx,self.state)[0]['installer_version'],'0.1.1')
    def test_global_retention_is_bounded_across_many_installer_versions(self):
        keep=None;pending=[]
        for n in range(20):
            manifest,payload=fixture(100+n,'0.1.'+str(n));identity=self.store.save(manifest,payload)
            bundle.write_json(self.store.root/'active.json',{'identity':identity,'check_after':0,'update':{'state':'current'}})
            bucket=self.store.snapshot(self.state,identity)
            tx=bucket/(SERIAL+'-'+str(n))/'transaction.json';tx.parent.mkdir()
            unresolved=n in (1,2)
            tx.write_bytes(encoded({'schema':'wsprrypico-usb-transaction/1','serial':SERIAL,'stage':'failed' if unresolved else 'complete','recovery_required':unresolved}))
            stamp=int(bundle.time.time())+n
            os.utime(tx.parent,(stamp,stamp))
            if unresolved:pending.append(tx)
            plan=f'{n:032x}';out=self.cache/plan;out.mkdir();(out/'firmware.uf2').write_bytes(b'firmware')
            os.utime(out,(stamp,stamp));self.store.bind(plan,identity)
            self.store.prune(self.cache,self.state);keep=identity
        self.assertEqual(len(list(self.cache.iterdir())),3)
        self.assertEqual(len(list((self.store.root/'plans').iterdir())),3)
        self.assertLessEqual(len(list((self.store.root/'bundles').iterdir())),3)
        records=list(self.state.rglob('transaction.json'));self.assertEqual(len(records),4)
        self.assertTrue(all(path.exists() for path in pending))
        self.assertEqual(self.store.prune(self.cache,self.state),2)
    def test_eight_recovery_holds_block_new_work_without_evicting_them(self):
        identity=self.select();bucket=self.store.snapshot(self.state,identity)
        for n in range(8):
            tx=bucket/(SERIAL+'-'+str(n))/'transaction.json';tx.parent.mkdir()
            tx.write_bytes(encoded({'schema':'wsprrypico-usb-transaction/1','serial':SERIAL,'stage':'failed','recovery_required':True}))
        plan='a'*32;(self.cache/plan).mkdir();self.store.bind(plan,identity)
        with patch.object(entry,'invoke',side_effect=AssertionError('unsafe new operation')):
            with self.assertRaisesRegex(ValueError,'Eight unresolved'):self.invoke('upgrade','--bundle',str(self.cache/plan))
        self.assertEqual(len(list(self.state.rglob('transaction.json'))),8)
    def test_metadata_duplicates_and_external_recovery_paths_are_rejected(self):
        with self.assertRaises(bundle.Unavailable):bundle.decode(b'{"contract":1,"contract":2}')
        self.select();outside=self.base/'outside'/'serial'/'transaction.json';outside.parent.mkdir(parents=True);outside.write_text('{}')
        with self.assertRaises(bundle.Unavailable):self.store.recovery(outside,self.state)
    def test_recovery_does_not_depend_on_channel_or_cache_indexes(self):
        identity=self.select();bucket=self.store.snapshot(self.state,identity)
        tx=bucket/(SERIAL+'-pending')/'transaction.json';tx.parent.mkdir()
        tx.write_bytes(encoded({'schema':'wsprrypico-usb-transaction/1','serial':SERIAL,'stage':'awaiting_bootsel','recovery_required':True,'installer_version':'0.1.1','saved':'untouched'}))
        (self.store.root/'active.json').write_text('corrupt index')
        self.cache.rmdir();self.cache.symlink_to(self.base/'missing-cache')
        with patch.object(bundle,'Network',side_effect=AssertionError('network')):
            _,result=self.invoke('recover','--transaction',str(tx))
        self.assertEqual(result['stage'],'complete');self.assertEqual(result['saved'],'untouched')
    def test_local_build_reference_survives_installer_version_retirement(self):
        identity=self.select();image=b'local image';reference={'schema':'wsprrypico-local-reference/1','serial':SERIAL,'uf2_sha256':bundle.sha(image)}
        bundle.atomic(self.state/('.local-image-'+SERIAL+'.uf2'),image)
        bundle.write_json(self.state/('.local-reference-'+SERIAL+'.json'),reference)
        bucket=self.store.snapshot(self.state,identity)
        self.assertEqual((bucket/('.local-image-'+SERIAL+'.uf2')).read_bytes(),image)
        self.store.prune(self.cache,self.state)
        self.assertEqual((self.state/('.local-image-'+SERIAL+'.uf2')).read_bytes(),image)
        self.assertFalse(bucket.exists())
    def test_http_rate_limit_headers_and_sanitized_reason(self):
        headers=Message();headers['X-RateLimit-Remaining']='0';headers['X-RateLimit-Reset']='1792357300';headers['Retry-After']='120'
        error=bundle.urllib.error.HTTPError(API+'releases/tags/x',403,'private address',headers,io.BytesIO(b'{"message":"private account"}'))
        with patch.object(bundle.urllib.request,'urlopen',side_effect=error),patch.object(bundle.time,'time',return_value=1792357200):
            with self.assertRaises(bundle.Unavailable) as caught:bundle.Network().api('releases/tags/x')
        self.assertEqual(caught.exception.details['retry_at'],1792357321)
        self.assertNotIn('private',str(caught.exception));self.assertIn('UTC',str(caught.exception))


if __name__=='__main__':unittest.main()
