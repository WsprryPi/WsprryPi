import hashlib,json,os,re,sys,time
from pathlib import Path
p=Path(sys.argv[1]);value=sys.argv[2]
assert value in ('true','false')
before=p.read_text()
section=re.search(r'(?ms)^\[Operation\]\s*\n(.*?)(?=^\[|\Z)',before)
assert section
body,n=re.subn(r'(?mi)^Transmit\s*=\s*(?:true|false)\s*$',f'Transmit = {value}',section.group(1))
assert n==1
after=before[:section.start(1)]+body+before[section.end(1):]
assert after!=before
started=time.time_ns()
with p.open('w') as f:
    f.write(after);f.flush();os.fsync(f.fileno())
print(json.dumps({'path':str(p),'transmit':value,'write_started_utc_ns':str(started),
    'write_finished_utc_ns':str(time.time_ns()),'before_sha256':hashlib.sha256(before.encode()).hexdigest(),
    'after_sha256':hashlib.sha256(after.encode()).hexdigest(),'changed_fields':['Operation.Transmit']}))
