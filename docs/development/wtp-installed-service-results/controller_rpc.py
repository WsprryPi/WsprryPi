import json,sys,time
from pathlib import Path
import probe
wires={}
for line in sys.stdin:
 try:
  c=json.loads(line);op=c['op']
  if op=='open':
   w=probe.Wtp(c['host'],c.get('port',31417));wires[c['name']]=w;value={'hello':w.hello,'owner':w.owner,'session':w.session}
  elif op=='request':value=wires[c['name']].request(c['command'],c.get('body'),okay=c.get('okay',True))
  elif op=='close':
   w=wires.pop(c['name']);w.close();value=w.records
  elif op=='http':value=probe.http(c['host'],c.get('port',31415),c.get('method','GET'),c['path'],c.get('body'),c.get('revision'))
  elif op=='exit':break
  else:raise ValueError(op)
  print(json.dumps({'ok':True,'at_utc_ns':str(time.time_ns()),'value':value}),flush=True)
 except Exception as e:print(json.dumps({'ok':False,'error':repr(e)}),flush=True)
