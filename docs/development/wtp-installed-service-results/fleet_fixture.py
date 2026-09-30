import json,os
from pathlib import Path
p=Path('/usr/local/etc/wsprrypi.ini.wtp-assignments.json')
assert not p.exists(),'existing fleet state must be preserved'
id='b'*32
settings={'Transport':'network_plain','Hostname':'192.0.2.123','TCP Port':31417,'Device ID':id,'Allow Frequency Adjustment':True,'Start Uncertainty ns':500000000}
row={'device_id':id,'name':'Installed recovery fixture','settings':settings,'product':'WsprryPico','management_port':31415,'enabled':True,'last_start_ns':'1800000000000000000','in_flight':True,'revocation_generation':None,'schedule':{'mode':'tone','frequency_hz':14097100,'duration_ms':1000,'period_seconds':60,'phase_seconds':0}}
with p.open('x') as f:json.dump({'version':1,'assignments':[row],'removals':[]},f)
os.chmod(p,0o600)
print('private in-flight fixture saved; no target connection or RF expected')
