#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Repeatable application cleanup ownership tests with no system effects."""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import install_ownership as owner


class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name).resolve()
        self.source=self.root/'source';(self.source/'scripts').mkdir(parents=True)
        (self.source/'scripts/route_application.py').write_text('# new companion\n')
    def write(self,name,data):
        path=owner.physical(self.root,name);path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(data);path.chmod(0o600);return path
    def install(self):
        owner.begin(self.root,self.source)
        self.write(owner.CODE,b'# new companion\n')
        self.write('/usr/local/etc/wsprrypi.ini',b'default')
        self.write('/usr/local/etc/wsprrypi.ini.wtp-devices.json',b'{"version":1,"profiles":[]}')
        owner.complete(self.root)
    def test_fresh_then_repeat_then_remove_retires_owned_companion_and_sidecars(self):
        self.install();owner.begin(self.root,self.source)
        self.write('/usr/local/etc/wsprrypi.ini.pre_migration.bak',b'default')
        self.write('/etc/apache2/sites-available/wsprrypi.conf.wsprrypi.bak',b'generated')
        owner.complete(self.root);owner.remove(self.root)
        for name in owner.FILES:self.assertFalse(owner.physical(self.root,name).exists(),name)
        self.assertFalse(owner.physical(self.root,owner.STATE).exists())
        owner.remove(self.root)
    def test_preexisting_backup_and_operator_configuration_are_preserved(self):
        backup=self.write('/usr/local/etc/wsprrypi.ini.pre_migration.bak',b'operator backup')
        self.write('/usr/local/etc/wsprrypi.ini',b'operator settings')
        self.install()
        owner.remove(self.root);self.assertEqual(backup.read_bytes(),b'operator backup')
    def test_user_fleet_and_modified_backup_are_preserved(self):
        self.install()
        fleet=self.write('/usr/local/etc/wsprrypi.ini.wtp-devices.json',b'{"operator":"data"}')
        owner.remove(self.root);self.assertTrue(fleet.exists())
    def test_modified_code_fails_before_other_deletions(self):
        self.install();code=self.write(owner.CODE,b'# administrator changed\n')
        with self.assertRaisesRegex(ValueError,'modified application code'):owner.remove(self.root)
        self.assertTrue(code.exists());self.assertTrue(owner.physical(self.root,owner.STATE).exists())
    def test_foreign_companion_fails_before_ownership_changes(self):
        self.write(owner.CODE,b'foreign')
        with self.assertRaisesRegex(ValueError,'unattributed'):owner.begin(self.root,self.source)
        self.assertFalse(owner.physical(self.root,owner.STATE).exists())
    def test_symlink_is_not_followed(self):
        outside=self.write('/outside',b'preserve')
        code=owner.physical(self.root,owner.CODE);code.parent.mkdir(parents=True);code.symlink_to(outside)
        with self.assertRaisesRegex(ValueError,'symlink'):owner.begin(self.root,self.source)
        self.assertEqual(outside.read_bytes(),b'preserve')
    def test_interrupted_installation_is_explicitly_recoverable(self):
        owner.begin(self.root,self.source);self.write(owner.CODE,b'# new companion\n')
        with self.assertRaisesRegex(ValueError,'incomplete'):owner.remove(self.root)
        owner.begin(self.root,self.source);owner.complete(self.root);owner.remove(self.root)
        self.assertFalse(owner.physical(self.root,owner.CODE).exists())
    def test_unrecognized_ownership_paths_are_rejected(self):
        self.install();path=owner.physical(self.root,owner.STATE);value=json.loads(path.read_text())
        value['files']['/etc/foreign']={'preserve':False,'sha256':None,'kind':'code'}
        path.write_text(json.dumps(value))
        with self.assertRaisesRegex(ValueError,'outside fixed scope'):owner.remove(self.root)

    def test_malformed_receipt_types_fail_before_deletion(self):
        self.install();path=owner.physical(self.root,owner.STATE);original=json.loads(path.read_text())
        import copy
        for field,bad in [('protectBackups',0),('iniSha256','bad'),('directories',{n:0 for n in owner.DIRECTORIES})]:
            value=copy.deepcopy(original);value[field]=bad;path.write_text(json.dumps(value))
            with self.assertRaises(ValueError):owner.remove(self.root)
            self.assertTrue(owner.physical(self.root,owner.CODE).exists())
        path.write_text('{"version":1,"version":1}')
        with self.assertRaisesRegex(ValueError,'duplicate'):owner.remove(self.root)


if __name__=='__main__':unittest.main(verbosity=2)
