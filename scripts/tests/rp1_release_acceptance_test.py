#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Consumer acceptance delegates all provider artifact policy to public tools."""
import importlib.util
import json
from pathlib import Path
import shutil
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
        self.candidate=self.root/'candidate';self.candidate.mkdir()
        self.identity={'sourceCommit':'a'*40,'sourceTree':'b'*40}
        self.consumer={'sourceCommit':'c'*40,'sourceTree':'d'*40}
        self.ui={2,4,8,12}
        self.cli=set(self.ui)
        self.pin='v12.34.56'
        self.publication_tag=self.pin
        requirements=json.loads(A.client.requirements_path().read_bytes())
        self.requirements=requirements
        self.metadata={'schemaVersion':1,'contract':'rp1-gpclk-provider-identity-v1',
            'repository':A.client.REPOSITORY,'module':'rp1-gpclk-dkms','productVersion':'12.34.56',
            'debianVersion':'12.34.56-1','interfaces':requirements['interfaces'],
            'capabilities':requirements['capabilities']+['release-audit'],'uapiSha256':requirements['uapiSha256'],
            'compatibilityIdentities':{r:'v12.34.56-rp1-'+r for r in ['gpio4','gpio20']}}
        self.candidate_record(mask=15)

    def candidate_record(self, mask):
        tag='v'+self.metadata['productVersion']
        filename='rp1-gpclk-dkms_'+self.metadata['debianVersion']+'_all.deb'
        payload=self.candidate/filename
        payload.write_bytes(b'owned')
        package={'filename':filename,'sha256':A.client.sha256_file(payload)}
        uapi=self.metadata['uapiSha256']['consumer']
        provenance_routes={}
        drive_routes={}
        inventory=[]
        for route, number in A.ROUTE_IDS.items():
            boot, runtime=('1'*64, '2'*64) if route=='gpio4' else ('3'*64, '4'*64)
            route_identity={'routeId':number,'compatibilityId':self.metadata['compatibilityIdentities'][route]}
            path=f'usr/lib/rp1-gpclk-dkms/overlays/rp1-gpclk-{route}.dtbo'
            provenance_routes[route]={**route_identity,'dtboSha256':boot,'packagePath':path}
            inventory.append({'path':path,'sha256':boot})
            drive_routes[route]={**route_identity,'bootDtboSha256':boot,
                                 'runtimeDtboSha256':runtime,'supportedDriveMaMask':mask}
        manifest=self.candidate/A.MANIFEST
        versions={key:self.metadata[key] for key in ('productVersion','debianVersion')}
        manifest.write_text(json.dumps({**versions,'sourceCommit':self.identity['sourceCommit'],'releaseTag':tag,
                                        'package':package,'uapi':{'sha256':uapi},'packageInventory':inventory}))
        (self.candidate/A.PROVENANCE).write_text(json.dumps({
            **self.identity,**versions,'releaseTag':tag,'package':package,
            'installationManifestSha256':A.client.sha256_file(manifest),
            'uapi':{'rp1_gpclk.h':{'sha256':uapi}},'routes':provenance_routes,
        }))
        self.drives={'schemaVersion':1,'package':package,'uapiSha256':uapi,'routes':drive_routes}

    def args(self,*extra):return A.parser().parse_args(['--provider-source',str(self.source),'--output',str(self.root/'output'),'--candidate',str(self.candidate),*extra])
    def validate(self,args,*,bad=None,consumer_identities=None):
        self.calls=[]
        checked_candidate=None
        def invoke(path,operation,*arguments):
            self.calls.append(operation)
            if operation=='identity':return {'providerIdentity':self.metadata}
            if operation=='verify-release':
                self.assertEqual(arguments[arguments.index('--tag')+1],self.pin)
                audited=arguments[arguments.index('--compare-candidate')+1]
                self.assertNotEqual(audited,self.candidate)
                self.assertEqual(audited,checked_candidate)
                self.assertEqual(A.snapshot(audited),A.snapshot(self.candidate))
                return {'publicationVerified':True,'installationPerformed':False,'candidateMatched':True,
                        'releaseTag':self.publication_tag,'providerIdentity':self.metadata,**self.identity}
            raise AssertionError('unexpected lifecycle effect: '+operation)
        def run(command,**kwargs):
            nonlocal checked_candidate
            self.calls.append(command)
            if command[0]=='git':return subprocess.CompletedProcess(command,1,b'',b'')
            if command[0].endswith('build-release-candidate'):
                target=Path(command[command.index('--output')+1]);target.mkdir();(target/'fixture.bin').write_bytes(b'owned')
                return subprocess.CompletedProcess(command,0,b'',b'')
            value={'status':'PASS','classification':'release' if args.release or args.verify_publication else 'candidate',
                'independentlyRebuilt':True,'published':False,**self.identity}
            if args.release or args.verify_publication:
                checked_candidate=Path(command[command.index('--candidate')+1])
                self.assertNotEqual(checked_candidate,self.candidate)
                self.assertEqual(A.snapshot(checked_candidate),A.snapshot(args.candidate))
                value['driveCapabilities']=self.drives
            if bad:bad(value)
            return subprocess.CompletedProcess(command,0,json.dumps(value),'')
        with (mock.patch.object(A,'source_identity',return_value=self.identity),
              mock.patch.object(A.drive_selection,'consumer_identity',return_value=self.consumer,
                                side_effect=consumer_identities),
              mock.patch.object(A.drive_selection,'observe',
                                return_value=(self.ui,self.cli,self.pin,self.requirements)) as observe,
              mock.patch.object(A.client,'invoke',side_effect=invoke),
              mock.patch.object(A.subprocess,'run',side_effect=run)):
            result=A.accept(args)
        if args.release or args.verify_publication:
            observe.assert_called_once_with(args.consumer_source,self.consumer['sourceCommit'])
        return result
    def test_generic_candidate_acceptance_is_noninstalling_and_not_publication(self):
        result=self.validate(self.args())
        self.assertFalse(result['publicationVerified']);self.assertFalse(result['installationPerformed']);self.assertFalse(result['qualification'])
        self.assertNotIn('rp1DriveSelection',result)
        self.assertEqual(result['providerIdentity']['productVersion'],'12.34.56')
        self.assertTrue(any(isinstance(c,list) and c[0].endswith('validate-release-candidate') for c in self.calls))
    def test_publication_uses_only_public_provider_command_and_checks_matching_source(self):
        result=self.validate(self.args('--verify-publication','--release-tag','v12.34.56'))
        self.assertTrue(result['publicationVerified']);self.assertIn('verify-release',self.calls)
        self.assertEqual(result['rp1DriveSelection']['consumerSource'],self.consumer)

    def test_release_profile_compares_both_routes_even_before_installer_adoption(self):
        self.pin='v2.0.0'
        result=self.validate(self.args('--release'))
        recorded=result['rp1DriveSelection']
        self.assertEqual(recorded['providerTag'],'v12.34.56')
        self.assertEqual(recorded['schemaVersion'],1)
        self.assertEqual(recorded['package'],self.drives['package'])
        self.assertEqual(recorded['uapiSha256'],self.drives['uapiSha256'])
        self.assertEqual(set(recorded['routes']),{'gpio4','gpio20'})
        self.assertEqual(recorded['uiDriveMa'],[2,4,8,12])
        self.assertEqual(recorded['cliDriveMa'],[2,4,8,12])
        self.assertFalse(result['productionSelectable'])
        self.assertNotIn('verify-release',self.calls)

    def refuse_release(self, error, *options, **kwargs):
        with self.assertRaisesRegex(A.client.ContractError,error):
            self.validate(self.args(*options),**kwargs)
        self.assertFalse((self.root/'output/acceptance.json').exists())
        shutil.rmtree(self.root/'output',ignore_errors=True)

    def test_v101_drive_limit_and_old_validator_fail(self):
        # v1.0.1 tag 301ba9e0cae0dbcb33169a7dd328c10f79681071 publishes
        # RP1_GPCLK_DRIVE_SUPPORT_2_MA in QUERY regardless of route.
        self.identity['sourceCommit']='301ba9e0cae0dbcb33169a7dd328c10f79681071'
        self.metadata['productVersion']='1.0.1'
        self.metadata['debianVersion']='1.0.1-1'
        self.metadata['compatibilityIdentities']={route:'v1.0.1-rp1-'+route for route in A.ROUTE_IDS}
        self.candidate_record(mask=1)
        self.refuse_release(r'GPIO4 missing 4, 8, 12 mA; GPIO20 missing 4, 8, 12 mA','--release')
        self.refuse_release('capability evidence is missing','--release',
                            bad=lambda value:value.pop('driveCapabilities'))

    def test_one_route_mismatch_and_incomplete_equality(self):
        self.drives['routes']['gpio20']['supportedDriveMaMask']=7
        self.refuse_release('GPIO20 missing 12 mA','--release')
        self.ui=self.cli={2}
        self.candidate_record(mask=1)
        self.refuse_release(r'Pi UI missing 4, 8, 12 mA; Pi CLI missing 4, 8, 12 mA','--release')

    def test_ui_and_cli_drift(self):
        self.ui={2,4,8}
        self.cli={2,4,8,12,16}
        self.refuse_release(r'Pi UI missing 12 mA; Pi CLI extra 16 mA','--release')

    def test_production_sources_supply_observed_options(self):
        observer=A.drive_selection
        markup=(ROOT/observer.UI_VIEW).read_text()
        script=(ROOT/observer.UI_SCRIPT).read_text()
        header=(ROOT/observer.CLI_OPTIONS).read_text()
        parser=(ROOT/observer.CLI_PARSER).read_text()
        observer.require_cli_wiring(parser)
        with self.assertRaisesRegex(A.client.ContractError,'does not use the checked drive choices'):
            observer.require_cli_wiring(parser.replace('!is_supported_rp1_gpio_drive_ma(drive)', 'drive != 2'))
        commented = parser.replace('if (!is_supported_rp1_gpio_drive_ma(drive))',
                                   '/* if (!is_supported_rp1_gpio_drive_ma(drive)) */ if (drive != 2)')
        with self.assertRaisesRegex(A.client.ContractError,'does not use the checked drive choices'):
            observer.require_cli_wiring(commented)
        self.assertEqual(observer.ui_choices(markup,script),{2,4,8,12})
        self.assertEqual(observer.cli_choices(header),{2,4,8,12})
        removed=markup.replace('<option value="12">12 mA</option>',
                               '<option disabled value="12">12 mA</option>')
        self.assertNotEqual(removed,markup)
        self.assertEqual(observer.ui_choices(removed,script),{2,4,8})
        drift=header.replace('{2, 4, 8, 12}', '{2, 4, 8, 16}')
        self.assertEqual(observer.cli_choices(drift),{2,4,8,16})
        client_drift=script.replace('[2, 4, 8, 12].includes(Number(value))',
                                    '[2, 4, 8].includes(Number(value))')
        with self.assertRaisesRegex(A.client.ContractError,'rejects displayed drive choices 12'):
            observer.ui_choices(markup,client_drift)
        client_drift=script.replace('const valid = supportedRp1GpioDrive(field.value);',
                                    'const valid = Number(field.value) === 2;')
        with self.assertRaisesRegex(A.client.ContractError,'rejects displayed drive choices 4, 8, 12'):
            observer.ui_choices(markup,client_drift)
        with self.assertRaisesRegex(A.client.ContractError,'expected one complete'):
            observer.ui_choices('<template><select id="rp1_gpio_drive_ma">'
                                '<option value="2">2 mA</option></select></template>',script)
        with self.assertRaisesRegex(A.client.ContractError,'selector is unavailable'):
            observer.ui_choices('<fieldset disabled>'+markup+'</fieldset>',script)

    def test_selected_pi_requirements_must_match_checking_tool(self):
        self.requirements={**self.requirements,'schemaVersion':2}
        self.refuse_release('different provider requirements','--release')

    def test_overlay_must_match_manifest_inventory_as_well_as_release_metadata(self):
        manifest=self.candidate/A.MANIFEST
        value=json.loads(manifest.read_text())
        value['packageInventory'][0]['sha256']='e'*64
        manifest.write_text(json.dumps(value))
        provenance=self.candidate/A.PROVENANCE
        value=json.loads(provenance.read_text())
        value['installationManifestSha256']=A.client.sha256_file(manifest)
        provenance.write_text(json.dumps(value))
        self.refuse_release('GPIO4 drive overlay identity differs','--release')

    def test_invalid_evidence_and_substituted_artifacts(self):
        for key,value,problem in [
            ('schemaVersion',True,'version or fields'),
            ('schemaVersion',2,'version or fields'),
            ('uapiSha256','f'*64,'UAPI identity'),
            ('package',{'filename':self.drives['package']['filename'],'sha256':'f'*64},'package differs'),
            ('routes',{'gpio4':self.drives['routes']['gpio4']},'exactly once'),
        ]:
            with self.subTest(key=key,value=value):
                self.refuse_release(problem,'--release',
                    bad=lambda v:v.update(driveCapabilities={**self.drives,key:value}))
        for key,value,problem in [
            ('supportedDriveMaMask',True,'mask is invalid'),
            ('supportedDriveMaMask',-1,'mask is invalid'),
            ('supportedDriveMaMask',0x1f,'mask is invalid'),
            ('routeId',True,'route identity'),
            ('bootDtboSha256',self.drives['routes']['gpio20']['bootDtboSha256'],'overlay identity'),
            ('runtimeDtboSha256',self.drives['routes']['gpio20']['runtimeDtboSha256'],'overlay identities'),
        ]:
            with self.subTest(key=key,value=value):
                def alter(result):
                    routes={**self.drives['routes'],'gpio4':{**self.drives['routes']['gpio4'],key:value}}
                    result['driveCapabilities']={**self.drives,'routes':routes}
                self.refuse_release(problem,'--release',bad=alter)

    def test_changed_consumer_source_has_no_passing_record(self):
        self.refuse_release('consumer source changed','--release',
            consumer_identities=[self.consumer,{**self.consumer,'sourceTree':'e'*40}])

    def test_candidate_changed_after_snapshot_has_no_passing_record(self):
        def change_original(_):
            (self.candidate/self.drives['package']['filename']).write_bytes(b'substituted')
        self.refuse_release('candidate changed during acceptance','--release',bad=change_original)

    def test_publication_refuses_pin_mismatch_before_provider_effect(self):
        self.refuse_release('differs from the selected Pi installer','--verify-publication','--release-tag','v9.0.0')
        self.assertNotIn('verify-release',self.calls)
        self.pin='v9.0.0'
        self.refuse_release('differs from the selected Pi installer','--verify-publication')
        self.assertNotIn('verify-release',self.calls)

    def test_publication_does_not_select_latest_or_accept_another_tag(self):
        self.publication_tag='v9.0.0'
        self.refuse_release('published provider differs','--verify-publication')
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
        target=self.candidate/self.drives['package']['filename'];target.unlink();target.symlink_to('/dev/null')
        with self.assertRaises(A.client.ContractError):A.snapshot(self.candidate)
    def test_tool_has_no_product_lock_or_private_provider_import(self):
        source=(ROOT/'scripts/maintainer/accept_rp1_release.py').read_text()
        for forbidden in ['LOCK_PATH','LEGACY_REPOSITORY','configure_provider_repository','validate_manifest','download_release','1.0.1']:
            self.assertNotIn(forbidden,source)


if __name__=='__main__':unittest.main(verbosity=2)
