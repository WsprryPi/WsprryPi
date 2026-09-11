#!/usr/bin/env python3
"""Opt-in Linux isolated-loopback actual binary configuration regression; simulated backend only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import socket
import time
import urllib.request


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--ini',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--run',action='store_true');args=p.parse_args()
    if not args.run:print('Plan only; no executable or network accessed');return
    from phase115_production_load import validate_ini,DEVICE,BINARY_SHA
    assert os.geteuid()==0 and {name for index,name in socket.if_nameindex()}=={'lo'},'Isolated network namespace required'
    validate_ini(args.ini)
    binary=Path('/home/pi/phase11-5-closure-fb0a2eb/source/src/build/bin/phase115-pi-fb0a2eb')
    assert hashlib.sha256(binary.read_bytes()).hexdigest()==BINARY_SHA
    os.umask(0o077);args.output.mkdir(mode=0o700)
    subprocess.run(['ip','link','set','lo','up'],check=True,timeout=5)
    opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with (args.output/'process.log').open('x') as log:
        process=subprocess.Popen([str(binary),'--backend','simulated','-i',str(args.ini),
            '--socket-loopback-only','--socket-loopback-family','ipv4'],cwd=args.output,
            stdout=log,stderr=subprocess.STDOUT,env=dict(os.environ,WSPRRYPI_DISABLE_HARDWARE_ACCESS='1'))
        try:
            end=time.monotonic()+20;config=None
            while time.monotonic()<end:
                assert process.poll() is None,'Process exited before loopback config API'
                try:
                    with opener.open('http://127.0.0.1:31425/config',timeout=2) as response:config=json.load(response)
                    break
                except OSError:time.sleep(.1)
            assert config is not None,'Configured port unavailable'
            (args.output/'actual-config.json').write_text(json.dumps(config,indent=2)+'\n')
            wtp=config['WTP'];op=config['Operation']
            assert wtp['Device ID']==DEVICE and wtp['Transport']=='network' and wtp['TCP Port']==18443
            assert wtp['Hostname']=='wsprrypico-0a60df.local' and wtp['Start Uncertainty ns']==500000000
            assert wtp['Allow Frequency Adjustment'] is True
            assert op['Transmit Backend']=='wtp' and op['Transmit'] is False and op['Enable on Boot']=='Never'
            assert op['Use LED'] is False and op['Use Amp'] is False and op['Use Shutdown'] is False
            assert op['Web Port']==31425 and op['Socket Port']==31426
            print('Actual Linux executable canonical INI round-trip: PASS; simulated-only; loopback namespace')
        finally:
            if process.poll() is None:
                process.terminate()
                try:process.wait(timeout=15)
                except subprocess.TimeoutExpired:process.kill();process.wait();raise
            assert process.returncode==0,'Nonzero application exit'
    assert 'Transmit backend: simulated' in (args.output/'process.log').read_text()
    (args.output/'result.json').write_text(json.dumps(dict(result='PASS',binary_sha256=BINARY_SHA,
        ini_sha256=hashlib.sha256(args.ini.read_bytes()).hexdigest(),hardware='NONE; isolated loopback simulated backend'))+'\n')


if __name__=='__main__':main()
