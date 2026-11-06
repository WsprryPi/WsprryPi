#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise the actual application/provider public boundary without hardware."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

APP=Path(__file__).resolve().parents[2]
if len(sys.argv)!=2:raise SystemExit('usage: rp1_gpclk_update_contract_test.py /path/to/RP1-DKMS')
PROVIDER=Path(sys.argv.pop()).resolve(strict=True)
sys.path.insert(0,str(APP/'scripts'))
import rp1_gpclk_dkms_install as client


class Tests(unittest.TestCase):
    def test_actual_descriptor_satisfies_application_requirements(self):
        p=subprocess.run([sys.executable,'-B',str(PROVIDER/'scripts/provider_lifecycle.py'),'identity'],
                         capture_output=True,text=True,check=True)
        value=client.strict_json(p.stdout)
        self.assertEqual(value['contract'],client.PUBLIC_CONTRACT)
        self.assertEqual(value['operation'],'identity')
        client.validate_consumer_provider({'identity':value['providerIdentity']})
    def test_real_read_only_prepare_and_plan_digest_have_no_host_effects(self):
        with tempfile.TemporaryDirectory() as temp:
            state=Path(temp).resolve();state.chmod(0o700)
            model=state/'model';model.write_bytes(b'unknown\0')
            compatible=state/'compatible';compatible.write_bytes(b'unknown\0')
            p=subprocess.run([sys.executable,'-B',str(PROVIDER/'scripts/provider_lifecycle.py'),'prepare',
                '--state-dir',str(state),'--install','false','--source-policy','production',
                '--model-file',str(model),'--compatible-file',str(compatible)],capture_output=True,text=True,check=True)
            value=client.strict_json(p.stdout)
            self.assertEqual(value['decision']['install'],False)
            plan=subprocess.run([sys.executable,'-B',str(PROVIDER/'scripts/provider_lifecycle.py'),'plan-status',
                                 '--state-dir',str(state)],capture_output=True,text=True,check=True)
            checked=client.strict_json(plan.stdout)
            self.assertEqual(checked['planSha256'],value['planSha256'])
            raw=client.strict_json((state/'plan.json').read_bytes())
            raw['requestedSource']='changed'
            (state/'plan.json').write_text(json.dumps(raw))
            changed=subprocess.run([sys.executable,'-B',str(PROVIDER/'scripts/provider_lifecycle.py'),'plan-status',
                                   '--state-dir',str(state)],capture_output=True,text=True,check=True)
            self.assertNotEqual(client.strict_json(changed.stdout)['planSha256'],value['planSha256'])
            self.assertEqual({p.name for p in state.iterdir()},{'model','compatible','plan.json'})


if __name__=='__main__':unittest.main(verbosity=2)
