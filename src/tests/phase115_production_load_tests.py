#!/usr/bin/env python3
import configparser
import contextlib
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

import phase115_production_load as load


def settings():
    config=configparser.ConfigParser()
    config.optionxform=str
    config.read_dict({'Operation':{'Transmit':'false','Use LED':'false','Use Amp':'false',
        'Use Shutdown':'false','Enable on Boot':'Never','Transmit Backend':'wtp',
        'Web Port':'31425','Socket Port':'31426'},'WTP':{'Transport':'network','Hostname':load.NAME,
        'TLS Server Identity':load.NAME,'Device ID':load.DEVICE,'TCP Port':'18443','Endpoint':'',
        'Allow Frequency Adjustment':'true','Start Uncertainty ns':'500000000'},
        'Band GPIO':{'Transmit Pin':'','Active High':'false'},'Calibration':{'PPM':'0'},
        'GPIO':{'Use System Clock Frequency Estimate':'false','Frequency Residual PPM':'0','Manual PPM':'0'},
        'Experimental':{'Allow Unqualified Frequency':'true','Allow Non-Amateur Frequency':'true'}})
    return config


class LoadTests(unittest.TestCase):
    def test_reject_scope_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'test.ini'
            def write(config):
                with path.open('w') as stream: config.write(stream)
            write(settings());load.validate_ini(path)
            for section,key,value in (
                ('Operation','Transmit','true'),('Operation','Use Amp','true'),
                ('Operation','Enable on Boot','Always'),('Operation','Transmit Backend','simulated'),
                ('Operation','Socket Port','1234'),('WTP','Hostname','other.local'),
                ('WTP','Endpoint','/dev/ttyACM0'),('WTP','Transport','serial'),
                ('WTP','Allow Frequency Adjustment','false'),('WTP','Start Uncertainty ns','1'),
                ('Band GPIO','Transmit Pin','4'),('Calibration','PPM','1'),
                ('GPIO','Use System Clock Frequency Estimate','true'),
                ('Experimental','Allow Non-Amateur Frequency','false')):
                config=settings();config[section][key]=value;write(config)
                with self.subTest(section=section,key=key),self.assertRaises(ValueError):
                    load.validate_ini(path)
            write(settings())
            with path.open('a') as stream: stream.write('\n[Operation]\nTransmit = true\n')
            with self.assertRaises(configparser.Error):load.validate_ini(path)

    def test_lowercased_or_shadowed_keys_fail_before_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'test.ini'
            original=settings()
            for section in ('Operation','WTP','GPIO','Calibration','Experimental'):
                changed=settings()
                for key in list(changed[section]):
                    value=changed[section].pop(key);changed[section][key.lower()]=value
                with path.open('w') as stream: changed.write(stream)
                with self.subTest(section=section),self.assertRaises((ValueError,configparser.Error)):
                    load.validate_ini(path)
            with path.open('w') as stream: original.write(stream)
            load.validate_ini(path)

    def test_no_run_does_not_resolve_or_spawn(self):
        with patch.object(sys,'argv',['load','--root','/absent','--seconds','180','--boot','a'*32]), \
             patch.object(load.subprocess,'Popen',side_effect=AssertionError('spawned')), \
             patch.object(Path,'resolve',side_effect=AssertionError('file accessed')), \
             contextlib.redirect_stdout(io.StringIO()):
            load.main()

    def test_wrong_peer_after_clean_process_exit_still_records_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);binary=root/'binary';binary.write_bytes(b'test-only')
            observer=root/'observer';observer.write_bytes(b'test-only-observer')
            ini=root/'test.ini'
            with ini.open('w') as stream: settings().write(stream)
            plan=dict(boot_id='a'*32,seconds=1,browser=False,device_id=load.DEVICE,
                address='10.77.15.10',netns='net-test',mountns='mount-test',binary=str(binary),
                observer=str(observer),ini=str(ini),ini_sha256=load.digest(ini),
                observer_sha256=load.digest(observer),ca='test',browser_cert='test',browser_key='test')
            (root/'load.json').write_text(json.dumps(plan))
            status={'host':{'identity':{'device_id':load.DEVICE,'boot_id':'b'*32},
                'network':{'resolved_address':'10.77.15.10','authenticated_identity':load.NAME}}}
            class Response:
                def __enter__(self):return self
                def __exit__(self,*args):pass
                def read(self,*args):return json.dumps(status).encode()
            class Process:
                returncode=None
                def poll(self):return self.returncode
                def terminate(self):self.returncode=0
                def wait(self,**kwargs):return self.returncode
            real_readlink=load.os.readlink
            def readlink(path,*args,**kwargs):
                if str(path)=='/proc/self/ns/net': return 'net-test'
                if str(path)=='/proc/self/ns/mnt': return 'mount-test'
                return real_readlink(path,*args,**kwargs)
            context=types.SimpleNamespace(load_cert_chain=lambda *args:None,
                                          set_alpn_protocols=lambda *args:None)
            opener=types.SimpleNamespace(open=lambda *args,**kwargs:Response())
            with patch.object(sys,'argv',['load','--root',str(root),'--seconds','1','--boot','a'*32,'--run']), \
                 patch.object(load,'BINARY_SHA',load.digest(binary)), \
                 patch.object(load.os,'readlink',side_effect=readlink), \
                 patch.object(load.os,'umask'),patch.object(load.signal,'signal'), \
                 patch.object(load.ssl,'create_default_context',return_value=context), \
                 patch.object(load.urllib.request,'build_opener',return_value=opener), \
                 patch.object(load.subprocess,'Popen',return_value=Process()):
                with self.assertRaisesRegex(ValueError,'peer identity'):load.main()
            rows=[json.loads(line) for line in (root/'load-events.jsonl').read_text().splitlines()]
            self.assertEqual(rows[-1]['kind'],'finish')
            self.assertEqual(rows[-1]['value']['result'],'FAILED')
            self.assertTrue(rows[-1]['value']['failures'])
            self.assertEqual(rows[-1]['value']['exit'],0)


if __name__=='__main__':unittest.main()
