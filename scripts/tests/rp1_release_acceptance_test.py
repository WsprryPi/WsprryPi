#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Consumer acceptance delegates all provider artifact policy to public tools."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('accept_rp1',ROOT/'scripts/maintainer/accept_rp1_release.py')
A=importlib.util.module_from_spec(spec);spec.loader.exec_module(A)


class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name).resolve();self.source=self.root/'source';self.source.mkdir()
        self.candidate=self.root/'candidate';self.candidate.mkdir();(self.candidate/'fixture.bin').write_bytes(b'owned')
        self.identity={'sourceCommit':'a'*40,'sourceTree':'b'*40}
        requirements=json.loads(A.client.requirements_path().read_bytes())
        self.metadata={'schemaVersion':1,'contract':'rp1-gpclk-provider-identity-v1',
            'repository':A.client.REPOSITORY,'module':'rp1-gpclk-dkms','productVersion':'12.34.56',
            'debianVersion':'12.34.56-1','interfaces':requirements['interfaces'],
            'capabilities':requirements['capabilities']+['release-audit'],'uapiSha256':requirements['uapiSha256'],
            'compatibilityIdentities':{r:'v12.34.56-rp1-'+r for r in ['gpio4','gpio20']}}
    def args(self,*extra):return A.parser().parse_args(['--provider-source',str(self.source),'--output',str(self.root/'output'),'--candidate',str(self.candidate),*extra])
    def validate(self,args,*,bad=None):
        self.calls=[]
        def invoke(path,operation,*arguments):
            self.calls.append(operation)
            if operation=='identity':return {'providerIdentity':self.metadata}
            if operation=='verify-release':return {'publicationVerified':True,'installationPerformed':False,'candidateMatched':True,'providerIdentity':self.metadata,**self.identity}
            raise AssertionError('unexpected lifecycle effect: '+operation)
        def run(command,**kwargs):
            self.calls.append(command)
            if command[0]=='git':return subprocess.CompletedProcess(command,1,b'',b'')
            if command[0].endswith('build-release-candidate'):
                target=Path(command[command.index('--output')+1]);target.mkdir();(target/'fixture.bin').write_bytes(b'owned')
                return subprocess.CompletedProcess(command,0,b'',b'')
            value={'status':'PASS','classification':'release' if args.release or args.verify_publication else 'candidate',
                'independentlyRebuilt':True,'published':False,**self.identity}
            if bad:bad(value)
            return subprocess.CompletedProcess(command,0,json.dumps(value),'')
        with mock.patch.object(A,'source_identity',return_value=self.identity),mock.patch.object(A.client,'invoke',side_effect=invoke),mock.patch.object(A.subprocess,'run',side_effect=run):return A.accept(args)
    def test_generic_candidate_acceptance_is_noninstalling_and_not_publication(self):
        result=self.validate(self.args())
        self.assertFalse(result['publicationVerified']);self.assertFalse(result['installationPerformed']);self.assertFalse(result['qualification'])
        self.assertEqual(result['providerIdentity']['productVersion'],'12.34.56')
        self.assertTrue(any(isinstance(c,list) and c[0].endswith('validate-release-candidate') for c in self.calls))
    def test_publication_uses_only_public_provider_command_and_checks_matching_source(self):
        result=self.validate(self.args('--verify-publication','--release-tag','v12.34.56'))
        self.assertTrue(result['publicationVerified']);self.assertIn('verify-release',self.calls)
    def test_contradictory_independent_results_fail_before_acceptance(self):
        for field,bad in [('status','FAIL'),('classification','other'),('independentlyRebuilt',False),('published',True),('sourceCommit','c'*40),('sourceTree','d'*40)]:
            with self.subTest(field=field),self.assertRaises(A.client.ContractError):self.validate(self.args(),bad=lambda v:v.update({field:bad}))
            self.assertFalse((self.root/'output/acceptance.json').exists())
            import shutil
            shutil.rmtree(self.root/'output')
    def test_build_uses_reported_version_and_public_builder(self):
        args=self.args();args.candidate=None
        result=self.validate(args)
        build=next(c for c in self.calls if isinstance(c,list) and c[0].endswith('build-release-candidate'))
        self.assertEqual(build[build.index('--version')+1],self.metadata['productVersion'])
        self.assertEqual(build[build.index('--source-commit')+1],self.identity['sourceCommit'])
        self.assertFalse(result['installationPerformed'])

    def test_publication_requires_advertised_audit_capability(self):
        self.metadata['capabilities'].remove('release-audit')
        with self.assertRaisesRegex(A.client.ContractError,'lacks non-installing'):self.validate(self.args('--verify-publication'))
        self.assertFalse((self.root/'output').exists())
    def test_source_authentication_refuses_dirty_hidden_foreign_and_malformed_git_state(self):
        values={('rev-parse','--show-toplevel'):str(self.source),('remote','get-url','origin'):'git@github.com:'+A.client.REPOSITORY+'.git',('status','--porcelain=v1','--untracked-files=all'):'',('ls-files','-v'):'H source',('rev-parse','HEAD'):'a'*40,('rev-parse','HEAD^{tree}'):'b'*40}
        def run(source,*args):return values[args]
        with mock.patch.object(A.client,'run_git',side_effect=run):
            self.assertEqual(A.source_identity(self.source),self.identity)
            for key,bad in [(('remote','get-url','origin'),'https://github.com/foreign/provider.git'),(('status','--porcelain=v1','--untracked-files=all'),' M source'),(('ls-files','-v'),'h source'),(('rev-parse','HEAD'),'short')]:
                old=values[key];values[key]=bad
                with self.assertRaises(A.client.ContractError):A.source_identity(self.source)
                values[key]=old

    def test_source_mismatch_during_acceptance_fails(self):
        with mock.patch.object(A,'source_identity',side_effect=[self.identity,{'sourceCommit':'c'*40,'sourceTree':'b'*40}]),mock.patch.object(A.client,'invoke',return_value={'providerIdentity':self.metadata}),mock.patch.object(A.subprocess,'run',side_effect=[subprocess.CompletedProcess([],1,b'',b''),subprocess.CompletedProcess([],0,json.dumps({'status':'PASS','classification':'candidate','published':False,'independentlyRebuilt':True,**self.identity}))]),self.assertRaises(A.client.ContractError):A.accept(self.args())
    def test_snapshot_rejects_symlinks_and_retained_substitution(self):
        target=self.candidate/'fixture.bin';target.unlink();target.symlink_to('/dev/null')
        with self.assertRaises(A.client.ContractError):A.snapshot(self.candidate)
    def test_tool_has_no_product_lock_or_private_provider_import(self):
        source=(ROOT/'scripts/maintainer/accept_rp1_release.py').read_text()
        for forbidden in ['LOCK_PATH','LEGACY_REPOSITORY','configure_provider_repository','validate_manifest','download_release','1.0.1']:
            self.assertNotIn(forbidden,source)


if __name__=='__main__':unittest.main(verbosity=2)
