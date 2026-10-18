#!/usr/bin/env python3
"""Hardware-free package lifecycle and immutable download validation."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch
import sys

SCRIPT=Path(__file__).resolve().parents[1]/'pico_usb_deploy.py'
spec=importlib.util.spec_from_file_location('deploy',SCRIPT); deploy=importlib.util.module_from_spec(spec); spec.loader.exec_module(deploy)

class DeploymentTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(); self.root=Path(self.temp.name).resolve()/'root'; self.repository=Path(self.temp.name).resolve()/'repo'
        (self.repository/'config').mkdir(parents=True);(self.repository/'scripts').mkdir()
        (self.repository/'scripts/pico_usb_entry.py').write_text('# fixture entry\n')
        self.payload={'scripts/usb_installer.py':b'# fixture core\n','scripts/ci/release-manifest-1.schema.json':b'{}\n'}
        self.descriptor={'schema':'wsprrypi-pico-usb-deployment/1','installer_version':'0.1.0','revision':'a'*40,
                         'files':{name:hashlib.sha256(data).hexdigest() for name,data in self.payload.items()}}
        (self.repository/'config/pico-usb-installer.json').write_text(json.dumps(self.descriptor))
        self.urls=[]
    def tearDown(self): self.temp.cleanup()
    def fetch(self,url):
        self.urls.append(url);self.assertIn('/'+'a'*40+'/',url)
        return self.payload[url.split('/'+'a'*40+'/',1)[1]]
    def install(self): deploy.provision('install',self.repository,self.root,self.fetch)
    def test_install_upgrade_remove_retains_private_state(self):
        self.install();target=self.root/'usr/local/lib/wsprrypi/pico-usb'
        self.assertEqual(len(self.urls),2);self.assertEqual((target/'entry.py').stat().st_mode&0o777,0o644)
        backup=self.root/'var/lib/wsprrypi/pico-usb/private.bin';backup.write_bytes(b'private fixture')
        self.assertEqual(backup.parent.stat().st_mode&0o777,0o700)
        self.install();deploy.provision('uninstall',self.repository,self.root)
        self.assertFalse((target/'entry.py').exists());self.assertEqual(backup.read_bytes(),b'private fixture')
    def test_bad_download_never_publishes_code(self):
        with self.assertRaises(ValueError): deploy.provision('install',self.repository,self.root,lambda _:b'changed')
        self.assertFalse((self.root/'usr/local/lib/wsprrypi/pico-usb').exists())
    def test_symlink_and_modified_code_are_retained(self):
        self.install();entry=self.root/'usr/local/lib/wsprrypi/pico-usb/entry.py';entry.write_text('administrator changes')
        with self.assertRaises(ValueError): self.install()
        with self.assertRaises(ValueError): deploy.provision('uninstall',self.repository,self.root)
        self.assertEqual(entry.read_text(),'administrator changes')
    def test_pending_member_blocks_package_replacement(self):
        self.install();hold=self.root/'var/lib/wsprrypi/pico-usb-control/C.json';hold.write_text('{}')
        with self.assertRaisesRegex(ValueError,'maintenance'): self.install()
        self.assertTrue(hold.exists())

    def test_explicit_development_source_is_hash_bound_and_labelled(self):
        source=self.repository/'pico-source'
        for name,data in self.payload.items():
            path=source/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data+b'development')
        deploy.provision('install',self.repository,self.root,lambda _:self.fail('unexpected download'),development_source=source)
        target=self.root/'usr/local/lib/wsprrypi/pico-usb'
        descriptor=json.loads((target/'deployment.json').read_text())
        self.assertEqual(descriptor['development_source']['kind'],'unpublished-working-tree')
        self.assertEqual(descriptor['development_source']['base_revision'],'a'*40)
        for name,data in self.payload.items():
            self.assertEqual((target/name).read_bytes(),data+b'development')
            self.assertEqual(descriptor['files'][name],hashlib.sha256(data+b'development').hexdigest())
        self.assertEqual(json.loads((self.repository/'config/pico-usb-installer.json').read_text()),self.descriptor)

class EntryBufferTests(unittest.TestCase):
    def test_executes_exact_verified_core_and_schema_when_files_are_replaced(self):
        entry_spec=importlib.util.spec_from_file_location('entry',SCRIPT.with_name('pico_usb_entry.py'))
        entry=importlib.util.module_from_spec(entry_spec);entry_spec.loader.exec_module(entry)
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary).resolve();(root/'scripts/ci').mkdir(parents=True)
            core=root/'scripts/usb_installer.py';schema=root/'scripts/ci/release-manifest-1.schema.json'
            good=b"INSTALLER_VERSION='0.1.0'\ndef main(argv=None,schema_bytes=None): return 27 if schema_bytes==b'original schema' else 99\n"
            core.write_bytes(good);schema.write_bytes(b'original schema')
            descriptor={'schema':'wsprrypi-pico-usb-deployment/1','installer_version':'0.1.0','files':{
                'scripts/usb_installer.py':hashlib.sha256(good).hexdigest(),
                'scripts/ci/release-manifest-1.schema.json':hashlib.sha256(b'original schema').hexdigest()}}
            (root/'deployment.json').write_text(json.dumps(descriptor))
            original_read=Path.read_bytes;original_stat=Path.lstat;reads=[]
            def read(path):
                value=original_read(path);reads.append(path)
                if path==core:core.write_bytes(b"INSTALLER_VERSION='0.1.0'\ndef main(**kwargs): return 99\n")
                if path==schema:schema.write_bytes(b'changed schema')
                return value
            def protected(path,*args,**kwargs):
                info=original_stat(path,*args,**kwargs)
                return SimpleNamespace(st_mode=info.st_mode&~0o022,st_uid=0)
            with patch.object(entry,'__file__',str(root/'entry.py')),patch.object(Path,'lstat',protected), \
                 patch.object(Path,'read_bytes',read),patch.object(sys,'argv',['entry.py','discover']):
                self.assertEqual(entry.main(),27)
            self.assertEqual(reads.count(core),1);self.assertEqual(reads.count(schema),1)

if __name__=='__main__':unittest.main()
