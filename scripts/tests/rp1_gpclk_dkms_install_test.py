#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Offline tests for the application client; provider mechanics live upstream."""
import copy
import contextlib
import io
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
import rp1_gpclk_dkms_install as client


def identity(version='12.34.56-rc.2+developer'):
    requirements=json.loads((ROOT/'config/rp1-provider-requirements.json').read_text())
    return {'schemaVersion':1,'contract':'rp1-gpclk-provider-identity-v1',
            'repository':client.REPOSITORY,'module':'rp1-gpclk-dkms',
            'productVersion':version,'debianVersion':'12.34.56~rc.2+developer-1',
            'interfaces':requirements['interfaces'],'capabilities':requirements['capabilities'],
            'uapiSha256':requirements['uapiSha256'],
            'compatibilityIdentities':{route:'v'+version+'-rp1-'+route for route in ['gpio4','gpio20']}}


class Tests(unittest.TestCase):
    def test_pinned_default_and_explicit_release_selection_for_both_application_channels(self):
        def release(tag, **extra):
            return {'tag_name':tag,'draft':False,'prerelease':'-' in tag,'published_at':'2026-11-06T00:00:00Z',
                    'assets':[{'name':name} for name in ['SHA256SUMS','rp1-gpclk-dkms-installation-manifest-v1.json']],**extra}
        releases=[release('v0.9.0'),release('v1.0.0-rc.1'),release('v1.0.0-rc.10'),
                  release('v1.0.0-rc.2'),release('v1.0.0'),release('v2.0.0-rc.10'),release('v9.0.0',draft=True),release('ci-devel'),release('v2.0.0-rc.01')]
        for selector,policy in [('auto','development'),('auto','production'),('release','explicit-source'),('release:v1.0.0-rc.1','explicit-source'),('release:1.0.0-rc.1','explicit-source')]:
            expected='v2.0.0-rc.10' if selector=='release' else 'v1.0.0' if selector=='auto' else 'v1.0.0-rc.1'
            response=releases if selector=='release' else release(expected)
            with tempfile.TemporaryDirectory() as raw,patch.object(client,'fetch_json',return_value=response), \
                 patch.object(client.subprocess,'run') as run,patch.object(client,'run_git',return_value='a'*40), \
                 patch.object(client,'invoke',return_value={'providerIdentity':identity('1.0.0-rc.10')}):
                state=Path(raw);source=state/'provider-source';(source/'scripts').mkdir(parents=True)
                (source/'scripts/provider_lifecycle.py').touch()
                _,selected=client.bootstrap(state,selector,policy)
                self.assertEqual(selected,'release:'+expected)
                if selector!='release':
                    self.assertTrue(client.fetch_json.call_args.args[0].endswith('/releases/tags/'+expected))
                self.assertEqual(run.call_args_list[-1].args[0][-1],'origin/devel')
        self.assertGreater(client.release_precedence('v1.0.0'),client.release_precedence('v1.0.0-rc.10'))
        self.assertEqual(client.release_precedence('v1.0.0+build'),client.release_precedence('v1.0.0'))

    def test_default_release_failures_do_not_clone_or_fall_back(self):
        good={'tag_name':'v1.0.0','draft':False,'prerelease':False,
              'published_at':'2026-11-11T17:47:05Z',
              'assets':[{'name':name} for name in
                        ['SHA256SUMS','rp1-gpclk-dkms-installation-manifest-v1.json']]}
        cases=[{**good,'draft':True},{**good,'published_at':None},
               {**good,'assets':[]},{**good,'tag_name':'v1.0.0-rc.2'},
               {**good,'tag_name':'v2.0.0'},{}]
        for policy in ['development','production']:
            for response in cases:
                with self.subTest(policy=policy,response=response),tempfile.TemporaryDirectory() as raw, \
                     patch.object(client,'fetch_json',return_value=response) as fetch, \
                     patch.object(client.subprocess,'run') as run:
                    with self.assertRaises(client.ContractError):client.bootstrap(Path(raw),'auto',policy)
                    fetch.assert_called_once_with('https://api.github.com/repos/'+client.REPOSITORY+'/releases/tags/v1.0.0')
                    run.assert_not_called()
            with tempfile.TemporaryDirectory() as raw, \
                 patch.object(client,'fetch_json',side_effect=OSError('release unavailable')) as fetch, \
                 patch.object(client.subprocess,'run') as run:
                with self.assertRaises(OSError):client.bootstrap(Path(raw),'auto',policy)
                self.assertEqual(fetch.call_count,1)
                run.assert_not_called()

    def test_obsolete_authorities_and_application_receipt_cannot_be_adopted(self):
        for repository in ('WsprryPi/RP1-DKMS', 'WsprryPi/RP1-GPCLK-DKMS'):
            value = identity(); value['repository'] = repository
            with self.assertRaises(client.ContractError): client.validate_consumer_provider({'identity': value})
            with self.assertRaises(client.ContractError): client.canonical_origin('https://github.com/'+repository+'.git')
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw); receipt = root/'unrecognized.json'
            receipt.write_bytes(b'foreign receipt')
            args = client.parser().parse_args(['prepare', '--state-dir', str(root), '--install', 'true',
                                              '--source', 'devel', '--wsprry-source', 'devel'])
            with patch.object(client, 'UNRECOGNIZED_RECORD', receipt), patch.object(client, 'bootstrap') as bootstrap:
                with self.assertRaises(client.ContractError): client.prepare_client(args)
                bootstrap.assert_not_called()
            self.assertEqual(receipt.read_bytes(), b'foreign receipt')

    def test_invalid_release_prefix_is_refused_before_fetch(self):
        with tempfile.TemporaryDirectory() as raw,patch.object(client,'fetch_json') as fetch:
            with self.assertRaises(client.ContractError):
                client.bootstrap(Path(raw),'release:vv1.0.0-rc.1','explicit-source')
            fetch.assert_not_called()

    def test_debug_runner_preserves_literal_arguments_and_failure_details(self):
        stdout=io.StringIO();stderr=io.StringIO()
        with contextlib.redirect_stdout(stdout),contextlib.redirect_stderr(stderr):
            runner=client.Runner(True)
            result=runner.run([sys.executable,'-c','import sys; print(sys.argv[1])','literal $(no-command)'])
            self.assertEqual(result.stdout,'literal $(no-command)\n')
            with self.assertRaisesRegex(client.ContractError,'expected failure'):
                runner.run([sys.executable,'-c','import sys; sys.exit("expected failure")'])
        self.assertIn('[RP1 DEBUG] command',stderr.getvalue())
        self.assertIn('expected failure',stderr.getvalue())

    def test_requirements_do_not_pin_the_product(self):
        for version in ['0.0.1','0.0.1+changed','2.0.0-rc.1','123.45.6']:
            client.validate_consumer_provider({'identity':identity(version)})

    def test_incompatible_interfaces_digests_capabilities_fail(self):
        for field in ['interfaces','uapiSha256','capabilities','repository','contract','schemaVersion']:
            changed=identity();changed[field]={} if field in ['interfaces','uapiSha256'] else [] if field=='capabilities' else 'wrong'
            with self.subTest(field=field),self.assertRaises(client.ContractError):
                client.validate_consumer_provider({'identity':changed})
        with self.assertRaises(client.ContractError):client.validate_consumer_provider({'version':'0.0.1'})

    def test_malformed_versions_and_contradictory_routes_fail(self):
        for version in ['', '01.2.3', '1.2', '1.2.3-01', '1.2.3+', '1.2.3 '+ 'x'*60]:
            with self.subTest(version=version),self.assertRaises(client.ContractError):
                client.validate_consumer_provider({'identity':identity(version)})
        value=identity();value['compatibilityIdentities']['gpio4']='wrong'
        with self.assertRaises(client.ContractError):client.validate_consumer_provider({'identity':value})

    def test_duplicate_and_oversized_json_fail(self):
        for raw in ['{"status":"ok","status":"error"}','x'*(client.MAX_API_BYTES+1)]:
            with self.assertRaises(client.ContractError):client.strict_json(raw)

    def test_provider_exit_and_operation_cannot_be_hidden(self):
        good={'schemaVersion':1,'contract':client.PUBLIC_CONTRACT,'operation':'apply','status':'ok'}
        for code,value in [(1,good),(0,{**good,'operation':'remove'}),(0,{**good,'status':'error'}),
                           (0,{**good,'schemaVersion':True})]:
            process=subprocess.CompletedProcess([],code,json.dumps(value))
            with patch.object(client.subprocess,'run',return_value=process),self.assertRaises(client.ContractError):
                client.invoke(Path('/verified/provider.py'),'apply')

    def test_client_calls_public_plan_then_effect_and_checks_absence(self):
        calls=[]
        def invoke(path,operation,*args):
            calls.append((operation,args))
            if operation=='status':return {'retirementPending':False}
            if operation=='removal-plan':return {'planSha256':'a'*64}
            return {'postconditions':{'providerAbsent':True,'transmissionAuthorized':False}}
        args=client.parser().parse_args(['remove'])
        with patch.object(client.ACTIVE_FACADE.__class__,'exists',return_value=True), \
             patch.object(client,'retained_facade',return_value=Path('/verified/provider.py')), \
             patch.object(client,'bootstrap',return_value=(Path('/verified/maintained.py'),'devel')), \
             patch.object(client,'invoke',side_effect=invoke):
            client.execute_client(args)
        self.assertEqual([x[0] for x in calls],['status','removal-plan','remove'])
        self.assertIn('a'*64,calls[-1][1])

    def test_old_authenticated_facade_upgrades_removal_tooling(self):
        args=client.parser().parse_args(['remove'])
        with patch.object(Path,'exists',return_value=True), \
             patch.object(client,'retained_facade',return_value=Path('/old/provider.py')), \
             patch.object(client,'bootstrap',return_value=(Path('/maintained/provider.py'),'devel')) as bootstrap, \
             patch.object(client,'invoke',side_effect=[{}, {'retirementPending':False},
                {'planSha256':'a'*64},{'postconditions':{'providerAbsent':True,'transmissionAuthorized':False}}]) as invoke:
            client.execute_client(args)
            bootstrap.assert_called_once()
            self.assertEqual(invoke.call_args.args[0],Path('/maintained/provider.py'))

    def test_removal_result_file_is_written_only_after_verified_absence(self):
        with tempfile.TemporaryDirectory() as raw:
            root=Path(raw).resolve();root.chmod(0o700)
            marker=root/'active.json';marker.write_text('{}')
            output=root/'result.json'
            args=client.parser().parse_args(['remove','--result-file',str(output)])
            with patch.object(client,'ACTIVE_FACADE',marker),patch.object(client,'retained_facade',return_value=root/'provider.py'),patch.object(client,'bootstrap',return_value=(root/'maintained.py','devel')):
                for absent in [False,True]:
                    with patch.object(client,'invoke',side_effect=[{'retirementPending':False}, {'planSha256':'a'*64},{'postconditions':{'providerAbsent':absent,'transmissionAuthorized':False}}]):
                        if not absent:
                            with self.assertRaises(client.ContractError):client.execute_client(args)
                            self.assertFalse(output.exists())
                        else:client.execute_client(args)
            self.assertEqual(json.loads(output.read_text()),{'schemaVersion':1,'contract':'wsprrypi-rp1-provider-removal-v1','outcome':'absent'})

    def test_absent_provider_preparation_and_repeat_removal_are_noops(self):
        for command in ['prepare-runtime-removal','remove']:
            args=client.parser().parse_args([command])
            with patch.object(Path,'exists',return_value=False),patch.object(client,'bootstrap',return_value=(Path('/verified/provider.py'),'devel')),patch.object(client,'invoke',return_value={'retirementPending':False}) as invoke:
                client.execute_client(args);self.assertEqual(invoke.call_args.args[1],'status')
        args=client.parser().parse_args(['remove','--remove','true'])
        with patch.object(Path,'exists',return_value=False),patch.object(client,'bootstrap',return_value=(Path('/verified/provider.py'),'devel')),patch.object(client,'invoke',return_value={'retirementPending':False}),self.assertRaises(client.ContractError):client.execute_client(args)

    def test_out_of_band_plan_change_stops_before_effect(self):
        with tempfile.TemporaryDirectory() as raw:
            state=Path(raw).resolve()
            (state/'client.json').write_text(json.dumps({'skip':False,'entrypoint':str(state/'provider-source/scripts/provider_lifecycle.py'),'planSha256':'a'*64}))
            args=client.parser().parse_args(['apply','--state-dir',str(state)])
            with patch.object(client,'invoke',return_value={'planSha256':'b'*64}) as invoke:
                with self.assertRaisesRegex(client.ContractError,'outside the acknowledged'):client.execute_client(args)
                self.assertEqual(invoke.call_count,1)

    def test_refuses_contradictory_removal_postconditions(self):
        args=client.parser().parse_args(['remove'])
        with patch.object(client.ACTIVE_FACADE.__class__,'exists',return_value=True), \
             patch.object(client,'retained_facade',return_value=Path('/verified/provider.py')), \
             patch.object(client,'bootstrap',return_value=(Path('/verified/maintained.py'),'devel')), \
             patch.object(client,'invoke',side_effect=[{'retirementPending':False}, {'planSha256':'a'*64},{'postconditions':{'providerAbsent':False,'transmissionAuthorized':False}}]), \
             self.assertRaises(client.ContractError):client.execute_client(args)

    def test_app_policy_selects_channel_without_hardware_or_provider_internals(self):
        self.assertEqual(client.source_policy('devel'),'development')
        self.assertEqual(client.source_policy('main'),'production')
        for value in ['HEAD','unknown','detached']:
            with self.assertRaises(client.ContractError):client.source_policy(value)
        source=(ROOT/'scripts/rp1_gpclk_dkms_install.py').read_text()
        for forbidden in ['runtime_source_identity','CONSUMER_PROVIDER_VERSION','def apply_development','def retain_provider_migration','ast.literal_eval']:
            self.assertNotIn(forbidden,source)

    def test_canonical_repository_accepts_https_and_ssh_without_other_authorities(self):
        for prefix in ['https://github.com/','ssh://git@github.com/','git@github.com:']:
            for suffix in ['', '.git', '.git/']:
                self.assertEqual(client.canonical_origin(prefix+client.REPOSITORY+suffix),client.REPOSITORY_URL)
        for value in ['https://attacker.example/'+client.REPOSITORY,'git@github.com:foreign/RP1-GPIO-CLK-DKMS.git',
                      'https://github.com/'+client.REPOSITORY+'.git?redirect=evil','http://github.com/'+client.REPOSITORY]:
            with self.assertRaises(client.ContractError):client.canonical_origin(value)

    def test_actual_clean_ssh_origin_checkout_bootstraps_without_network(self):
        provider=Path(os.environ.get('RP1_DKMS_SOURCE',ROOT.parent/'RP1-GPIO-CLK-DKMS'))
        with tempfile.TemporaryDirectory() as raw:
            root=Path(raw).resolve();root.chmod(0o700);source=root/'source';state=root/'state';state.mkdir(mode=0o700)
            # Synthetic Git metadata belongs only to this disposable fixture.
            import shutil
            source.mkdir()
            names = subprocess.check_output(['git', '-C', str(provider), 'ls-files', '-z',
                                             '--cached', '--others', '--exclude-standard']).split(b'\0')
            for raw_name in set(names) - {b''}:
                name = raw_name.decode(); original = provider/name
                if not original.is_file(): continue
                target = source/name; target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(original, target)
            for command in (['init', '-q'], ['config', 'user.name', 'Fixture'],
                            ['config', 'user.email', 'fixture@example.invalid'], ['add', '.'],
                            ['commit', '-qm', 'Synthetic candidate snapshot'],
                            ['remote', 'add', 'origin', client.REPOSITORY_URL]):
                subprocess.run(['git', '-C', str(source), *command], check=True, capture_output=True)
            subprocess.run(['git','-C',str(source),'remote','set-url','origin','git@github.com:'+client.REPOSITORY+'.git'],check=True)
            entrypoint,selected=client.bootstrap(state,'checkout:'+str(source),'explicit-source')
            self.assertTrue(entrypoint.is_file());self.assertEqual(selected,'checkout:'+str(state/'provider-source'))
            self.assertEqual(client.run_git(source,'remote','get-url','origin'),'git@github.com:'+client.REPOSITORY+'.git')

    def test_real_provider_public_identity_satisfies_compiled_consumer(self):
        provider=Path(os.environ.get('RP1_DKMS_SOURCE',ROOT.parent/'RP1-GPIO-CLK-DKMS'))
        p=subprocess.run([sys.executable,str(provider/'scripts/provider_lifecycle.py'),'identity'],capture_output=True,text=True,check=True)
        value=client.strict_json(p.stdout)
        self.assertEqual(value['contract'],client.PUBLIC_CONTRACT)
        client.validate_consumer_provider({'identity':value['providerIdentity']})
        # The portable adapter adds only host typedef/include guards.
        adapted=(ROOT/'src/WSPR-Transmitter/src/rp1_gpclk_uapi.h').read_text()
        canonical=(provider/'include/uapi/linux/rp1_gpclk.h').read_text()
        portable='''#ifdef __linux__
#include <linux/ioctl.h>
#include <linux/types.h>
#else
#include <stdint.h>
#include <sys/ioctl.h>
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __aligned_u64;
#endif'''
        self.assertEqual(adapted.replace(portable,'#include <linux/ioctl.h>\n#include <linux/types.h>'),canonical)


if __name__=='__main__':unittest.main(verbosity=2)
