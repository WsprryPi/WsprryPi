#!/usr/bin/env python3
"""Package migration, retained recovery and fail-before-publication tests."""
import copy
import importlib.util
import json
import os
import subprocess
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

from pico_usb_bundle_fixture import POLICY, encoded, fixture

SCRIPTS=Path(__file__).resolve().parents[1]
def load(name):
    spec=importlib.util.spec_from_file_location(name,SCRIPTS/(name+'.py'))
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

deploy=load('pico_usb_deploy');entry=load('pico_usb_entry');bundle=deploy.bundle


class DeploymentTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.base=Path(self.temp.name).resolve()
        self.root=self.base/'root';self.repository=self.base/'repo'
        (self.repository/'config').mkdir(parents=True);(self.repository/'scripts').mkdir()
        for name in ('pico_usb_entry.py','pico_usb_bundle.py'):
            shutil.copyfile(SCRIPTS/name,self.repository/'scripts'/name)
        (self.repository/'config/pico-usb-installer.json').write_bytes(encoded(POLICY))
        self.manifest,self.payload=fixture();self.source=self.base/'pico-source'
        (self.source/'scripts/ci').mkdir(parents=True)
        (self.source/'scripts/usb_installer.py').write_bytes(self.payload['usb_installer.py'])
        (self.source/'scripts/ci/release-manifest-1.schema.json').write_bytes(self.payload['release-manifest-1.schema.json'])
        self.target=self.root/'usr/local/lib/wsprrypi/pico-usb'
    def tearDown(self):self.temp.cleanup()
    def install(self,**kwargs):deploy.provision('install',self.repository,self.root,**kwargs)
    def seed(self):self.install(development_source=self.source)
    def store(self):return bundle.Store(self.target,POLICY,owner=False)
    def legacy(self):
        (self.target/'scripts/ci').mkdir(parents=True)
        files={}
        for name,data in self.payload.items():
            relative='scripts/'+('ci/' if name.endswith('.json') else '')+name
            (self.target/relative).write_bytes(data);files[relative]=bundle.sha(data)
        (self.target/'entry.py').write_bytes(b'# legacy adapter\n')
        prior={'schema':'wsprrypi-pico-usb-deployment/1','revision':'a'*40,'installer_version':'0.1.1',
               'files':files,'entry_sha256':bundle.sha((self.target/'entry.py').read_bytes())}
        (self.target/'deployment.json').write_bytes(encoded(prior))
        return prior
    def test_development_deployment_never_claims_ci_or_reintroduces_source_pin(self):
        self.seed();store=self.store();manifest,_=store.load(store.active()['identity'])
        self.assertEqual(manifest['origin']['kind'],'unpublished-working-tree')
        self.assertEqual(json.loads((self.target/'policy.json').read_text()),POLICY)
        self.assertNotIn('revision',json.loads((self.target/'policy.json').read_text()))
    def test_legacy_migration_keeps_exact_core_for_old_recovery(self):
        prior=self.legacy();self.install();store=self.store()
        identity=json.loads((self.target/'legacy.json').read_text())['identity'];manifest,payload=store.load(identity)
        self.assertEqual(payload,self.payload);self.assertEqual(manifest['origin']['previous_deployment'],prior)
        self.assertFalse((self.target/'scripts').exists())
        self.assertEqual(json.loads((self.target/'deployment.json').read_text())['schema'],'wsprrypi-pico-usb-deployment/2')
    def test_adapter_upgrade_retains_bindings_private_state_and_holds(self):
        self.seed();store=self.store();store.bind('a'*32,store.active()['identity'])
        backup=self.root/'var/lib/wsprrypi/pico-usb/private.bin';backup.write_bytes(b'private firmware')
        self.install();self.assertEqual(backup.read_bytes(),b'private firmware');self.assertEqual(store.planned('a'*32),store.active()['identity'])
        hold=self.root/'var/lib/wsprrypi/pico-usb-control/C.json';hold.write_text('{}')
        with self.assertRaisesRegex(ValueError,'maintenance'):self.install()
        self.assertTrue(hold.exists());self.assertEqual(backup.read_bytes(),b'private firmware')
    def test_legacy_bytecode_and_administrator_files_are_preserved_without_execution(self):
        self.legacy();cache=self.target/'scripts/__pycache__';cache.mkdir()
        cached=cache/'usb_installer.cpython-313.pyc';cached.write_bytes(b'old bytecode')
        custom=self.target/'scripts/administrator.txt';custom.write_bytes(b'keep this')
        self.install()
        self.assertEqual(cached.read_bytes(),b'old bytecode');self.assertEqual(custom.read_bytes(),b'keep this')
        self.assertFalse((self.target/'scripts/usb_installer.py').exists())
    def test_changed_adapter_and_symlinks_are_never_overwritten(self):
        self.seed();path=self.target/'entry.py';path.write_text('# administrator change')
        with self.assertRaises(ValueError):self.install()
        self.assertEqual(path.read_text(),'# administrator change')
        path.unlink();outside=self.base/'outside';outside.write_bytes(b'private');path.symlink_to(outside)
        with self.assertRaises(ValueError):self.install()
        self.assertEqual(outside.read_bytes(),b'private')
    def test_uninstall_keeps_retained_installer_and_private_recovery(self):
        self.seed();store=self.store();identity=store.active()['identity'];state=self.root/'var/lib/wsprrypi/pico-usb';bucket=store.snapshot(state,identity)
        private=state/'private.bin';private.write_bytes(b'private')
        deploy.provision('uninstall',self.repository,self.root)
        self.assertFalse((self.target/'entry.py').exists());self.assertTrue((bucket/'usb_installer.py').exists())
        self.assertEqual(private.read_bytes(),b'private')
        deploy.provision('uninstall',self.repository,self.root)
        self.install()
        self.assertTrue((self.target/'entry.py').exists());self.assertEqual(private.read_bytes(),b'private')
    def test_unavailable_first_install_publishes_no_executable(self):
        with patch.object(bundle.Network,'get',side_effect=bundle.Unavailable('offline')):
            with self.assertRaises(bundle.Unavailable):self.install()
        self.assertFalse((self.target/'entry.py').exists())
    def test_uninstalling_legacy_package_still_retains_its_recovery_core(self):
        self.legacy();deploy.provision('uninstall',self.repository,self.root)
        store=self.store();identity=json.loads((self.target/'legacy.json').read_text())['identity']
        self.assertEqual(store.load(identity)[1],self.payload)
        self.install();self.assertTrue((self.target/'entry.py').exists())
    def test_verified_source_bytes_execute_even_if_backing_files_change(self):
        # invoke receives verified buffers; file replacement cannot change them.
        manifest,payload=fixture();original=copy.deepcopy(payload)
        (self.source/'scripts/usb_installer.py').write_bytes(b'raise RuntimeError("changed")')
        with patch.object(entry,'ROOT',self.target):
            code,result=entry.invoke(manifest,original,['discover'])
        self.assertEqual(code,0);self.assertEqual(result['installer_contract'],1);self.assertEqual(result['marker'],'0.1.1')


class RuntimeProvisionTests(unittest.TestCase):
    def test_failed_deployment_reports_child_diagnostic_and_skips_rule_reload(self):
        with tempfile.TemporaryDirectory() as name:
            root=Path(name)
            (root/'scripts').mkdir()
            (root/'scripts/pico_usb_deploy.py').write_text(
                'import sys\nprint("specific Pico deployment failure", file=sys.stderr)\nsys.exit(17)\n')
            result=subprocess.run(['bash','-c',r'''
source "$INSTALL_SCRIPT"
source "$RUNTIME_SCRIPT"
logW() { printf '[WARN] %s\n' "$1"; }
warn() { printf '[WARN] %s\n' "$1" >&2; }
logD() { :; }
udevadm() { printf invoked >"$SENTINEL"; }
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
DRY_RUN=false
if pico_usb_runtime_provision install "$REPOSITORY"; then exit 99; fi
[[ ! -e "$SENTINEL" ]]
'''],text=True,capture_output=True,timeout=30,env={**os.environ,
                'INSTALL_SCRIPT':str(SCRIPTS/'install.sh'),
                'RUNTIME_SCRIPT':str(SCRIPTS/'pico_usb_runtime_install.sh'),
                'REPOSITORY':str(root),'SENTINEL':str(root/'reload'),'TMPDIR':str(root)})
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('Failed: Deploy identified shared Pico USB installer.',result.stdout)
            self.assertIn('specific Pico deployment failure',result.stdout)
            self.assertNotIn('Complete: Deploy identified shared Pico USB installer.',result.stdout)
            self.assertFalse(list(root.glob('wsprrypi-pico-deploy.*')))
    def test_dry_run_creates_no_diagnostics_or_deployment(self):
        with tempfile.TemporaryDirectory() as name:
            root=Path(name)
            result=subprocess.run(['bash','-c',r'''
source "$INSTALL_SCRIPT"
source "$RUNTIME_SCRIPT"
FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=
DRY_RUN=true
pico_usb_runtime_provision install "$REPOSITORY"
'''],text=True,capture_output=True,timeout=30,env={**os.environ,
                'INSTALL_SCRIPT':str(SCRIPTS/'install.sh'),
                'RUNTIME_SCRIPT':str(SCRIPTS/'pico_usb_runtime_install.sh'),
                'REPOSITORY':str(root),'TMPDIR':str(root)})
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('Complete: (dry) Deploy identified shared Pico USB installer.',result.stdout)
            self.assertEqual(list(root.iterdir()),[])


if __name__=='__main__':unittest.main()
