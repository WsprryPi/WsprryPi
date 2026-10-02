"""Observe the real installed Fleet, Pi WTP STATUS and Pico A USB JobService.

No claims, jobs or schedule writes are issued. Pico INFO has no owner/job IDs.
"""
import json,sys,time,threading,urllib.request
from pathlib import Path
sys.path.insert(0,'/home/pi/wtp-fleet-reconnection-20261001')
from usb_observer import SerialStream
from probe import Wtp
stage=Path(sys.argv[1]);deadline=int(sys.argv[2]);stop=threading.Event()
def emit(stream,row):
 stream.write(json.dumps({'utc_ns':str(time.time_ns()),**row})+'\n');stream.flush()
def pi():
 wire=None
 with (stage/'wspr2-observations.jsonl').open('w') as f:
  while not stop.is_set() and time.time()<deadline:
   try:
    if wire is None:wire=Wtp('192.168.1.123',31417)
    assert wire.hello['body']['device_id']=='e2a4d273f7d8f24f6696f7c6fb95ff11'
    emit(f,{'hello':wire.hello['body'],'status':wire.request('STATUS')['body']});wire.records.clear()
   except Exception as e:
    emit(f,{'error':repr(e)})
    if wire:wire.close()
    wire=None
   stop.wait(.5)
  if wire:wire.close()
def pico():
 stream=None
 with (stage/'Pico-A-observations.jsonl').open('w') as f:
  try:
   stream=SerialStream('/dev/serial/by-id/usb-WsprryPi_WsprryPico_0BF4B4AEC9FFB344-if00')
   while not stop.is_set() and time.time()<deadline:
    stream.sendall(b'INFO\n');buf=b'';end=time.monotonic()+8;info=None
    while time.monotonic()<end and info is None:
     buf+=stream.recv(4096)
     for line in buf.split(b'\n')[:-1]:
      if line.startswith(b'{') and line.endswith(b'}'):info=json.loads(line);break
    assert info and info['device_id']=='fd6127d11d6aca42a9905fa3fb1bf1d5'
    s=info['status'];assert s['engine']=='pio-dma-gp2'
    emit(f,{'device_id':info['device_id'],'revision':info['revision'],'status':s,'owner_and_remote_job_ids_observed':False})
    stop.wait(.5)
  except Exception as e:emit(f,{'error':repr(e)});stop.set()
  finally:
   if stream:stream.close()
def controller():
 with (stage/'controller-observations.jsonl').open('w') as f:
  while not stop.is_set() and time.time()<deadline:
   try:
    data={}
    for path in ['fleet','wtp-endpoint']:
     with urllib.request.urlopen('http://127.0.0.1:31415/api/v1/host/'+path,timeout=8) as r:data[path]=json.load(r)
    assert not data['wtp-endpoint']['local_requested'] and not data['wtp-endpoint']['output_unknown']
    emit(f,data)
   except Exception as e:emit(f,{'error':repr(e)})
   stop.wait(1)
threads=[threading.Thread(target=target) for target in [pi,pico,controller]]
for thread in threads:thread.start()
try:
 while time.time()<deadline and not stop.is_set():time.sleep(.5)
finally:
 stop.set()
 for thread in threads:thread.join(timeout=12)
