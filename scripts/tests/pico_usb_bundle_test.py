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
        with self.assertRaisesRegex(bundle.Unavailable,'incompatible contract'):bundle.published(net,'devel')
    def test_large_commit_resolution_uses_compact_refs_and_rechecks_head(self):
        manifest,payload=bundle.published(self.net,'devel')
        self.assertEqual(manifest['source'],self.manifest['source'])
        self.assertEqual(payload,self.payload)
        self.assertEqual(self.net.requests.count('git/ref/heads/devel'),2)
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
    def test_pending_branch_and_branch_race_keep_retained_installer(self):
        identity=self.select()
        self.net.mutate=lambda r: {**r,'workflow_runs':[]} if 'workflow_runs' in r else r
        result=self.store.refresh(self.net,force=True)
        self.assertEqual(result['identity'],identity);self.assertIn('retry after CI',result['update']['reason'])
        net=Network(*fixture());count=0
        def move(r):
            nonlocal count
            if r.get('ref')=='refs/heads/devel':
                count+=1
                if count==2:r['object']['sha']='c'*40
            return r
        net.mutate=move
        with self.assertRaisesRegex(bundle.Unavailable,'branch changed'):bundle.published(net,'devel')
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
