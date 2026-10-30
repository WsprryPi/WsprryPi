#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Deterministic latency, starvation, interruption and finite-load regressions."""
import unittest
import production_browser_load as load


class BrowserLoadTests(unittest.TestCase):
    def test_normal_browser_records_every_action_without_stress_reads(self):
        for seconds in (180,300):
            now=[0.0];requests=[];events=[]
            class Stop:
                def is_set(self):return False
                def wait(self,duration):now[0]+=duration
            def get(path):requests.append(path);now[0]+=.2
            load.normal_browser_schedule(0,seconds,Stop(),get,
                lambda kind,value:events.append((kind,value)),clock=lambda:now[0])
            self.assertEqual(len(requests),14)
            self.assertEqual(requests.count('/api/wtp'),8)
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

    def test_browser_reload_does_not_starve_five_second_status(self):
        now=[0.0];requests=[]
        class Stop:
            def is_set(self):return False
            def wait(self,seconds):now[0]+=seconds
        def get(path):
            requests.append((path,now[0]))
            now[0]+=2.022 if path=='/' else 1.498
        load.browser_schedule(0,180,Stop(),get,clock=lambda:now[0])
        status=[t for path,t in requests if path=='/api/wtp']
        self.assertEqual(len(status),36)
        self.assertLess(max(abs(t-i*5) for i,t in enumerate(status)),.101)
        for path in ('/','/version','/config'):
            self.assertEqual(sum(p==path for p,t in requests),6)
        now[0]=0
        def too_slow(path):now[0]+=4
        with self.assertRaisesRegex(ValueError,'fell behind'):
            load.browser_schedule(0,180,Stop(),too_slow,clock=lambda:now[0])

    def test_due_status_precedes_reads_after_one_slow_response(self):
        now=[0.0];requests=[]
        class Stop:
            def is_set(self):return False
            def wait(self,seconds):now[0]+=seconds
        def get(path):
            requests.append((path,now[0]))
            now[0]+=5.5 if len(requests)==1 else 1
        load.browser_schedule(0,180,Stop(),get,clock=lambda:now[0])
        self.assertEqual(requests[:2],[('/api/wtp',0.0),('/api/wtp',5.5)])
        self.assertEqual(sum(path=='/api/wtp' for path,t in requests),36)
        for begin in range(0,180,30):
            self.assertEqual(sorted(path for path,t in requests
                if begin<=t<begin+30 and path!='/api/wtp'),['/','/config','/version'])
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
        self.assertEqual(sum(p=='/api/wtp' for p,t in requests),36)
        for begin in range(0,180,30):
            self.assertEqual(sorted(p for p,t in requests if begin<=t<begin+30 and p!='/api/wtp'),
                             ['/','/config','/version'])

    def test_slow_response_latency_defers_reads_without_dropping_work(self):
        now=[0.0];requests=[];controls=[]
        status_times=iter([1.701,1.589,1.623,1.673,1.979,1.645,1.738,2.893,2.285,3.331])
        asset_times=iter([2.163,2.172,1.629,2.401,2.934,2.508])
        class Stop:
            def is_set(self):return False
            def wait(self,seconds):now[0]+=seconds
        def get(path):
            requests.append((path,now[0]))
            now[0]+=next(status_times,1.6) if path=='/api/wtp' else next(asset_times,1.6)
        def grant(deadline):
            if len(controls)>=4 or (len(controls)==3 and now[0]<30):return False
            controls.append(now[0]);now[0]+=1.6
            self.assertLess(now[0],deadline)
            return True
        load.browser_schedule(0,180,Stop(),get,clock=lambda:now[0],grant_control=grant)
        self.assertEqual(len(controls),4)
        status=[t for path,t in requests if path=='/api/wtp']
        self.assertEqual(len(status),36)
        self.assertLessEqual(max(t-i*5 for i,t in enumerate(status)),1)
        for begin in range(0,180,30):
            self.assertEqual(sorted(p for p,t in requests if begin<=t<begin+30 and p!='/api/wtp'),
                             ['/','/config','/version'])


if __name__ == "__main__":
    unittest.main()
