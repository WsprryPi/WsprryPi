"""Bounded independent cleanup for the two explicitly selected browser targets."""
import json,sys,time,urllib.request
from pathlib import Path
stage=Path(sys.argv[1]);base='http://127.0.0.1:31415/api/v1/host/'
selected=['e2a4d273f7d8f24f6696f7c6fb95ff11','fd6127d11d6aca42a9905fa3fb1bf1d5']
def read():
 with urllib.request.urlopen(base+'fleet',timeout=10) as r:return json.load(r),r.headers['ETag']
records=[]
for identity in selected:
 for attempt in range(4):
  try:
   data,revision=read()
   row=next((r for r in data['assignments'] if r['device_id']==identity),None)
   if row is None:break
   request=urllib.request.Request(base+'fleet',data=json.dumps({'operation':'pause','device_id':identity}).encode(),method='POST',headers={'Origin':'http://127.0.0.1:31415','Content-Type':'application/json','X-WsprryPico-Request':'1','If-Match':revision})
   with urllib.request.urlopen(request,timeout=45) as r:json.load(r)
   records.append({'device_id':identity,'paused':True});break
  except Exception as e:
   records.append({'device_id':identity,'attempt':attempt,'error':repr(e)});time.sleep(1)
(stage/'guard-result.json').write_text(json.dumps(records,indent=2)+'\n')
print(json.dumps(records))
