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
    def test_verified_live_ci_and_exact_release_checks_fail_closed(self):
        changes=[lambda r:r.update(draft=True),lambda r:r.update(head_sha='c'*40),lambda r:r.update(conclusion='failure'),
                 lambda r:r.update(status='in_progress'),lambda r:r.update(path='other.yml'),
                 lambda r:r.update(assets=[]),lambda r:r.update(object={'type':'commit','sha':'c'*40,'url':'private'})]
        for change in changes:
            net=Network(*fixture())
            def mutate(value):
                if (change==changes[0] and 'draft' in value) or (change in changes[1:5] and 'head_sha' in value) or (change==changes[5] and 'tag_name' in value) or (change==changes[6] and 'object' in value):change(value)
                return value
            net.mutate=mutate
            with self.subTest(change=change),self.assertRaises(bundle.Unavailable):bundle.published(net,'devel')
        self.assertFalse((self.store.root/'active.json').exists())
    def test_incompatible_contract_runtime_and_payload_are_never_executed(self):
        for field,value in [('contract',2),('python_min',[99,0]),('transaction_schema','other'),('result_schema','other'),('firmware_manifest_schema',2)]:
            manifest,payload=fixture();manifest[field]=value
            with self.subTest(field=field),self.assertRaises(bundle.Unavailable):bundle.published(Network(manifest,payload),'devel')
        net=Network(*fixture());net.payload['usb_installer.py']+=b'changed'
        with self.assertRaises(bundle.Unavailable):bundle.published(net,'devel')
    def test_absent_update_keeps_verified_tool_and_reports_actual_reason(self):
        identity=self.select();self.net.failure=bundle.Unavailable('GitHub rate limit. Retry after tomorrow UTC.',error_code='github_rate_limited',retry_at=9999999999)
        selected=self.store.refresh(self.net,force=True)
        self.assertEqual(selected['identity'],identity);self.assertEqual(selected['update']['error_code'],'github_rate_limited')
        requests=len(self.net.requests);self.store.refresh(self.net);self.assertEqual(len(self.net.requests),requests)
    def test_failure_without_previous_installer_never_installs(self):
        self.net.failure=bundle.Unavailable('offline')
        with self.assertRaises(bundle.Unavailable):self.select()
        self.assertFalse((self.store.root/'active.json').exists())
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
