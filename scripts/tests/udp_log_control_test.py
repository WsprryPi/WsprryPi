#!/usr/bin/env python3
"""Filesystem-only service lifecycle checks; no systemd, users, sockets or RF."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('udp_control',Path(__file__).parents[1]/'udp_log_control.py')
control=importlib.util.module_from_spec(spec);spec.loader.exec_module(control)

def defaults():
    return {'schema':'host-udp-logging/1','collector_id':'a'*32,'revision':'revision',
      'sender':{'enabled':False,'destination_ipv4':None,'destination_port':5514,'maximum_severity':6,'event_codes':[],'rate':16,'burst':32,'interface':None,'native_enabled':False,'usb_sources':[]},
      'collector':{'enabled':False,'bind_ipv4':None,'interface':None,'port':5514,'peers':[]}}
class Lifecycle(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name);self.ini=self.root/'settings.ini';self.ini.write_text('unchanged INI')
        self.application=self.root/'wsprrypi';header=b'\x7fELF'+b'\x02\x01'+bytes(12)+b'\xb7\x00';self.image=header+b'bounded worker fixture';self.application.write_bytes(header+b'--export-udp-logging-worker');self.calls=[];self.export_status=0
    def tearDown(self):self.temp.cleanup()
    def run_control(self,action):
        with patch.object(control.subprocess,'run',return_value=subprocess.CompletedProcess([],self.export_status,self.image,b'')):
            control.provision(action,self.ini,self.application,self.root,lambda *args:self.calls.append(args))
    def test_install_default_off_and_exact_image(self):
        self.run_control('install');self.assertEqual((self.root/'usr/local/bin/wsprrypi-log').read_bytes(),self.image)
        self.assertFalse(any('enable' in call for call in self.calls));self.assertEqual(self.ini.read_text(),'unchanged INI')
        unit=(self.root/'etc/systemd/system/wsprrypi-log.service').read_text()
        for value in ['User=wsprrylog','CapabilityBoundingSet=','PrivateDevices=true','MemoryMax=32M','TasksMax=2','ConditionControlGroupController=memory pids']:self.assertIn(value,unit)
    def test_explicit_514_socket_and_removal_preserve_policy(self):
        self.run_control('install');value=defaults();value['collector'].update(enabled=True,bind_ipv4='192.0.2.20',interface='eth0',port=514)
        policy=Path(str(self.ini)+'.udp-logs.json');policy.write_text(json.dumps(value));self.calls=[]
        with patch.object(control.socket,'if_nametoindex',return_value=1), patch.object(control,'validate_network'):self.run_control('configure')
        self.assertIn(('systemctl','enable','--now','wsprrypi-log.socket'),self.calls)
        self.assertIn('BindToDevice=eth0',(self.root/'etc/systemd/system/wsprrypi-log.socket').read_text())
        self.run_control('remove');self.assertEqual(json.loads(policy.read_text()),value);self.assertFalse((self.root/'usr/local/bin/wsprrypi-log').exists())
    def test_worker_upgrade_restarts_only_for_changed_image_or_unit(self):
        self.run_control('install');value=defaults();value['sender'].update(enabled=True,destination_ipv4='192.0.2.30',interface='eth0')
        Path(str(self.ini)+'.udp-logs.json').write_text(json.dumps(value))
        with patch.object(control.socket,'if_nametoindex',return_value=1),patch.object(control,'validate_network'):
            self.calls=[];self.run_control('install')
            self.assertNotIn(('systemctl','stop','wsprrypi-log.service'),self.calls)
            self.image+=b'new candidate';self.calls=[];self.run_control('install')
            self.assertIn(('systemctl','stop','wsprrypi-log.service'),self.calls)
            self.assertIn(('systemctl','enable','--now','wsprrypi-log.service'),self.calls)
    def test_unowned_executable_is_preserved(self):
        binary=self.root/'usr/local/bin/wsprrypi-log';binary.parent.mkdir(parents=True);binary.write_bytes(b'unknown executable')
        with self.assertRaises(ValueError):self.run_control('install')
        with self.assertRaises(ValueError):self.run_control('remove')
        self.assertEqual(binary.read_bytes(),b'unknown executable');self.assertEqual(self.calls,[])

    def test_invalid_policy_refuses_before_service_mutation(self):
        self.run_control('install');policy=Path(str(self.ini)+'.udp-logs.json');value=defaults();value['collector']['port']=True;policy.write_text(json.dumps(value));self.calls=[]
        with self.assertRaises(ValueError):self.run_control('configure')
        self.assertEqual(self.calls,[])
        policy.unlink();policy.symlink_to(self.ini)
        with self.assertRaises(ValueError):self.run_control('configure')
        self.assertEqual(self.ini.read_text(),'unchanged INI')
    def test_network_refusal_before_service_changes(self):
        value=defaults();value['sender'].update(enabled=True,destination_ipv4='192.0.2.255',interface='eth0')
        with patch.object(control,'ipv4_interfaces',return_value=[('eth0',int(control.ipaddress.IPv4Address('192.0.2.20')),0xffffff00)]):
            with self.assertRaises(ValueError):control.validate_network(value)
            value['sender']['destination_ipv4']='192.0.2.20'
            with self.assertRaises(ValueError):control.validate_network(value)
            value['sender']['destination_ipv4']='192.0.2.30';control.validate_network(value)
            value['collector'].update(enabled=True,interface='eth0',bind_ipv4='192.0.2.99')
            with self.assertRaises(ValueError):control.validate_network(value)

    def test_older_verified_executable_keeps_default_off(self):
        self.application.write_bytes(b'older executable without a worker')
        self.run_control('install');self.assertEqual(self.calls,[])
        self.assertFalse((self.root/'etc/systemd/system/wsprrypi-log.service').exists())
        value=defaults();value['sender'].update(enabled=True,interface='eth0',destination_ipv4='192.0.2.30')
        Path(str(self.ini)+'.udp-logs.json').write_text(json.dumps(value))
        with patch.object(control.socket,'if_nametoindex',return_value=1),patch.object(control,'validate_network'):
            with self.assertRaises(ValueError):self.run_control('install')
        self.assertEqual(self.calls,[])

    def test_missing_resource_controllers_are_unavailable(self):
        directory=self.root/'sys/fs/cgroup';directory.mkdir(parents=True)
        (directory/'cgroup.controllers').write_text('cpu io pids\n')
        self.assertFalse(control.resource_controllers_available(self.root))
        (directory/'cgroup.controllers').write_text('cpu io memory pids\n')
        self.assertTrue(control.resource_controllers_available(self.root))

    def test_build_without_journald_keeps_default_off_but_refuses_enabled_policy(self):
        self.export_status=3;self.image=b'';self.run_control('install')
        self.assertEqual(self.calls,[]);self.assertFalse((self.root/'etc/systemd/system/wsprrypi-log.service').exists())
        value=defaults();value['sender'].update(enabled=True,interface='eth0',destination_ipv4='192.0.2.30')
        Path(str(self.ini)+'.udp-logs.json').write_text(json.dumps(value))
        with patch.object(control.socket,'if_nametoindex',return_value=1),patch.object(control,'validate_network'):
            with self.assertRaises(ValueError):self.run_control('install')
        self.assertEqual(self.calls,[])
    def test_failed_export_is_not_an_unavailable_profile_fallback(self):
        self.export_status=1;self.image=b''
        with self.assertRaises(ValueError):self.run_control('install')
        self.assertEqual(self.calls,[])

    def test_systemd_argument_escaping(self):
        self.assertEqual(control.quoted('/a $b %c "d"'),r'"/a $$b %%c \"d\""')
if __name__=='__main__':unittest.main()
