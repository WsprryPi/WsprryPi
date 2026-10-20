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
    def test_production_qrss_requires_exact_opt_in_and_finite_native_shape(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'production.ini'
            config=settings()
            config['Operation'].update({'Transmit':'true','Enable on Boot':'Follow','Mode':'QRSS'})
            config['CW']={'Message':'ETE','Base Frequency':'135500','Dot Seconds':'3',
                'Inter Character Gap':'3','Fade Shape':'none','Repeat Minutes':'60'}
            def write():
                with path.open('w') as out:config.write(out)
            write();load.validate_ini(path,True)
            with self.assertRaises(ValueError):load.validate_ini(path)
            for key,bad in [('Message','ETET'),('Base Frequency','14097100'),('Dot Seconds','4'),
                            ('Inter Character Gap','1'),('Repeat Minutes','1'),('Fade Shape','linear')]:
                old=config['CW'][key];config['CW'][key]=bad;write()
                with self.assertRaises(ValueError):load.validate_ini(path,True)
                config['CW'][key]=old

    def test_normal_browser_records_every_action_without_stress_assets(self):
        for seconds in (180,300):
            now=[0.0];requests=[];events=[]
            class Stop:
                def is_set(self):return False
                def wait(self,duration):now[0]+=duration
            def get(path):requests.append(path);now[0]+=.2
            load.normal_browser_schedule(0,seconds,Stop(),get,
                lambda kind,value:events.append((kind,value)),clock=lambda:now[0])
            self.assertEqual(len(requests),14)
            self.assertEqual(requests.count('/api/v1/status'),8)
            self.assertNotIn('/style.css',requests)
            self.assertNotIn('/app.js',requests)
            self.assertEqual(len(events),16)
            self.assertEqual(sum(v['action']=='refresh' for k,v in events if k=='browser_action_start'),6)

    def test_normal_mutation_lane_preserves_every_due_action(self):
        now=[0.0];requests=[];events=[];grants=[]
        class Stop:
            def is_set(self):return False
            def wait(self,duration):now[0]+=duration
        def get(path):requests.append(path);now[0]+=.2
        # Model requests admitted only once every eight seconds. Consuming a
        # full five-second request must never cross the reserved due boundary.
        next_request=[5.0]
        def grant(until):
            if now[0]<next_request[0]:return False
            self.assertGreaterEqual(until-now[0],5.99)
            grants.append((now[0],until));now[0]+=5;next_request[0]=now[0]+8
            self.assertLessEqual(now[0],until-1+.001)
            return True
        load.normal_browser_schedule(0,300,Stop(),get,
            lambda kind,value:events.append((kind,value,now[0])),clock=lambda:now[0],grant_control=grant)
        self.assertEqual(len(requests),14);self.assertEqual(len(events),16)
        self.assertTrue(grants);self.assertGreaterEqual(now[0],300)
        for kind,value,when in events:
            if kind=='browser_action_start':
                self.assertLessEqual(when-value['scheduled_monotonic_ns']/1e9,15)

    def test_normal_browser_does_not_drop_late_or_interrupted_action(self):
        class Stop:
            def is_set(self):return False
            def wait(self,duration):pass
        with self.assertRaisesRegex(ValueError,'late/stopped'):
            load.normal_browser_schedule(0,300,Stop(),lambda path:None,
                lambda k,v:None,clock=lambda:16)
        with self.assertRaises(ValueError):load.normal_actions(299)

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

    def test_browser_reload_does_not_starve_five_second_status(self):
        now=[0.0];requests=[]
        class Stop:
            def is_set(self):return False
            def wait(self,seconds):now[0]+=seconds
        def get(path):
            requests.append((path,now[0]))
            now[0]+=2.022 if path=='/' else 1.498
        load.browser_schedule(0,180,Stop(),get,clock=lambda:now[0])
        status=[t for path,t in requests if path=='/api/v1/status']
        self.assertEqual(len(status),36)
        self.assertLess(max(abs(t-i*5) for i,t in enumerate(status)),.101)
        for path in ('/','/style.css','/app.js'):
            self.assertEqual(sum(p==path for p,t in requests),6)
        now[0]=0
        def too_slow(path):now[0]+=4
        with self.assertRaisesRegex(ValueError,'fell behind'):
            load.browser_schedule(0,180,Stop(),too_slow,clock=lambda:now[0])

    def test_due_status_precedes_assets_after_one_slow_response(self):
        now=[0.0];requests=[]
        class Stop:
            def is_set(self):return False
            def wait(self,seconds):now[0]+=seconds
        def get(path):
            requests.append((path,now[0]))
            now[0]+=5.5 if len(requests)==1 else 1
        load.browser_schedule(0,180,Stop(),get,clock=lambda:now[0])
        self.assertEqual(requests[:2],[('/api/v1/status',0.0),('/api/v1/status',5.5)])
        self.assertEqual(sum(path=='/api/v1/status' for path,t in requests),36)
        for begin in range(0,180,30):
            self.assertEqual(sorted(path for path,t in requests
                if begin<=t<begin+30 and path!='/api/v1/status'),['/','/app.js','/style.css'])
        now[0]=0
        def failed_measurement(path):now[0]+=6.013358513
        with self.assertRaisesRegex(ValueError,'fell behind'):
            load.browser_schedule(0,180,Stop(),failed_measurement,clock=lambda:now[0])

    def test_control_slots_preserve_all_status_and_reload_requests(self):
        now=[0.0];requests=[];controls=[]
        class Stop:
            def is_set(self):return False
            def wait(self,seconds):now[0]+=seconds
        def get(path):
            requests.append((path,now[0]));now[0]+=2.022 if path=='/' else 1.498
        def grant(deadline):
            if len(controls)==13:return False
            controls.append(now[0]);now[0]+=2
            self.assertLess(now[0],deadline)
            return True
        load.browser_schedule(0,180,Stop(),get,clock=lambda:now[0],grant_control=grant)
        self.assertEqual(len(controls),13)
        self.assertEqual(sum(p=='/api/v1/status' for p,t in requests),36)
        for begin in range(0,180,30):
            self.assertEqual(sorted(p for p,t in requests if begin<=t<begin+30 and p!='/api/v1/status'),
                             ['/','/app.js','/style.css'])

    def test_measured_rf_latency_defers_assets_without_dropping_work(self):
        now=[0.0];requests=[];controls=[]
        status_times=iter([1.701,1.589,1.623,1.673,1.979,1.645,1.738,2.893,2.285,3.331])
        asset_times=iter([2.163,2.172,1.629,2.401,2.934,2.508])
        class Stop:
            def is_set(self):return False
            def wait(self,seconds):now[0]+=seconds
        def get(path):
            requests.append((path,now[0]))
            now[0]+=next(status_times,1.6) if path=='/api/v1/status' else next(asset_times,1.6)
        def grant(deadline):
            if len(controls)>=4 or (len(controls)==3 and now[0]<30):return False
            controls.append(now[0]);now[0]+=1.6
            self.assertLess(now[0],deadline)
            return True
        load.browser_schedule(0,180,Stop(),get,clock=lambda:now[0],grant_control=grant)
        self.assertEqual(len(controls),4)
        status=[t for path,t in requests if path=='/api/v1/status']
        self.assertEqual(len(status),36)
        self.assertLessEqual(max(t-i*5 for i,t in enumerate(status)),1)
        for begin in range(0,180,30):
            self.assertEqual(sorted(p for p,t in requests if begin<=t<begin+30 and p!='/api/v1/status'),
                             ['/','/app.js','/style.css'])

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
                observer=str(observer),binary_sha256=load.digest(binary),ini=str(ini),ini_sha256=load.digest(ini),
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
