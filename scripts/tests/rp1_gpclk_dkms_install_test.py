#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Offline tests for the application client; provider mechanics live upstream."""
import copy
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
            if operation=='removal-plan':return {'planSha256':'a'*64}
            return {'postconditions':{'providerAbsent':True,'transmissionAuthorized':False}}
        args=client.parser().parse_args(['remove'])
        with patch.object(client.ACTIVE_FACADE.__class__,'exists',return_value=True), \
             patch.object(client,'retained_facade',return_value=Path('/verified/provider.py')), \
             patch.object(client,'invoke',side_effect=invoke):
            client.execute_client(args)
        self.assertEqual([x[0] for x in calls],['removal-plan','remove'])
        self.assertIn('a'*64,calls[-1][1])

    def test_removal_result_file_is_written_only_after_verified_absence(self):
        with tempfile.TemporaryDirectory() as raw:
            root=Path(raw).resolve();root.chmod(0o700)
            marker=root/'active.json';marker.write_text('{}')
            output=root/'result.json'
            args=client.parser().parse_args(['remove','--result-file',str(output)])
            with patch.object(client,'ACTIVE_FACADE',marker),patch.object(client,'retained_facade',return_value=root/'provider.py'):
                for absent in [False,True]:
                    with patch.object(client,'invoke',side_effect=[{'planSha256':'a'*64},{'postconditions':{'providerAbsent':absent,'transmissionAuthorized':False}}]):
                        if not absent:
                            with self.assertRaises(client.ContractError):client.execute_client(args)
                            self.assertFalse(output.exists())
                        else:client.execute_client(args)
            self.assertEqual(json.loads(output.read_text()),{'schemaVersion':1,'contract':'wsprrypi-rp1-provider-removal-v1','outcome':'absent'})

    def test_absent_provider_preparation_and_repeat_removal_are_noops(self):
        for command in ['prepare-runtime-removal','remove']:
            args=client.parser().parse_args([command])
            with patch.object(Path,'exists',return_value=False),patch.object(client,'invoke') as invoke:
                client.execute_client(args);invoke.assert_not_called()
        args=client.parser().parse_args(['remove','--remove','true'])
        with patch.object(Path,'exists',return_value=False),self.assertRaises(client.ContractError):client.execute_client(args)

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
             patch.object(client,'invoke',side_effect=[{'planSha256':'a'*64},{'postconditions':{'providerAbsent':False,'transmissionAuthorized':False}}]), \
             self.assertRaises(client.ContractError):client.execute_client(args)

    def test_app_policy_selects_channel_without_hardware_or_provider_internals(self):
        self.assertEqual(client.source_policy('devel'),'development')
        self.assertEqual(client.source_policy('main'),'production')
        for value in ['HEAD','unknown','detached']:
            with self.assertRaises(client.ContractError):client.source_policy(value)
        source=(ROOT/'scripts/rp1_gpclk_dkms_install.py').read_text()
        for forbidden in ['runtime_source_identity','CONSUMER_PROVIDER_VERSION','def apply_development','def retain_provider_migration','ast.literal_eval']:
            self.assertNotIn(forbidden,source)

    def test_real_provider_public_identity_satisfies_compiled_consumer(self):
        provider=Path(os.environ.get('RP1_DKMS_SOURCE',ROOT.parent/'RP1-DKMS'))
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
