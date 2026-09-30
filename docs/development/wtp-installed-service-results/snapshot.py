import sys,os,json,hashlib,stat,tarfile,subprocess,glob,time
from pathlib import Path
stage=Path('/home/pi/wtp-installed-validation.yTbN3G'); mode=sys.argv[1]
roots=['/usr/local/bin/wsprrypi','/usr/bin/wsprrypi','/usr/local/lib/wsprrypi','/var/lib/wsprrypi','/var/www/html','/etc/apache2','/etc/systemd/system/wsprrypi.service','/etc/systemd/system/wsprrypi.service.d','/etc/systemd/system/wsprrypi-rp1-reconcile.service','/etc/systemd/system/multi-user.target.wants/wsprrypi.service','/boot/firmware/config.txt','/etc/modules','/etc/modules-load.d','/etc/modprobe.d','/usr/local/bin/wspr','/usr/local/bin/shutdown-button.py','/usr/local/bin/shutdown-watch.py','/usr/local/bin/shutdown_watch.py','/usr/local/bin/wspr_watch.py','/etc/logrotate.d/wspr','/etc/logrotate.d/wsprrypi']
roots+=glob.glob('/usr/local/etc/wspr*')
for name in ['wspr','shutdown-button','shutdown-watch','shutdown_watch','wspr_watch']:
 for d in ['/etc/systemd/system','/lib/systemd/system']:
  p=f'{d}/{name}.service'
  if os.path.lexists(p):roots.append(p)
def inventory(paths):
 out={}
 def walk(p):
  if not os.path.lexists(p):out[p]={'missing':True};return
  s=os.lstat(p); x={'mode':oct(stat.S_IMODE(s.st_mode)),'uid':s.st_uid,'gid':s.st_gid}
  if stat.S_ISLNK(s.st_mode):x['link']=os.readlink(p)
  elif stat.S_ISREG(s.st_mode):x['sha256']=hashlib.file_digest(open(p,'rb'),'sha256').hexdigest()
  elif stat.S_ISDIR(s.st_mode):
   x['directory']=True
   for q in sorted(Path(p).iterdir()):walk(str(q))
  out[p]=x
 for p in paths:walk(p)
 return out
def savejson(name,obj):(stage/name).write_text(json.dumps(obj,indent=2)+'\n')
if mode=='backup':
 assert not (stage/'baseline.tar').exists()
 savejson('roots.json',roots);savejson('baseline-files.json',inventory(roots))
 with tarfile.open(stage/'baseline.tar','w') as archive:
  for p in roots:
   if os.path.lexists(p):archive.add(p,arcname=p.lstrip('/'),recursive=True)
 os.chmod(stage/'baseline.tar',0o600)
 for service in ['wsprrypi','apache2']:
  for action in ['is-active','is-enabled','cat','show']:
   proc=subprocess.run(['systemctl',action,service],capture_output=True,text=True)
   (stage/f'baseline-{service}-{action}.txt').write_text(proc.stdout+proc.stderr)
 (stage/'baseline-packages.txt').write_bytes(subprocess.check_output(['dpkg-query','-W','-f=${binary:Package}\t${Version}\n']))
 p=Path('/root/.wsprrypi-wtp/revocation-v1');(stage/'baseline-revocation-v1').write_bytes(p.read_bytes());os.chmod(stage/'baseline-revocation-v1',0o600)
 print(json.dumps({'backup':str(stage/'baseline.tar'),'files':len(inventory(roots)),'sha256':hashlib.file_digest(open(stage/'baseline.tar','rb'),'sha256').hexdigest()}))
elif mode=='compare':
 roots=json.loads((stage/'roots.json').read_text());before=json.loads((stage/'baseline-files.json').read_text());after=inventory(roots)
 changes={k:{'before':before.get(k),'after':after.get(k)} for k in sorted(before.keys()|after.keys()) if before.get(k)!=after.get(k)}
 savejson(sys.argv[2],changes);print(json.dumps({'differences':len(changes)}))
elif mode=='restore':
 roots=json.loads((stage/'roots.json').read_text())
 subprocess.run(['systemctl','stop','wsprrypi'],check=True)
 subprocess.run(['systemctl','stop','apache2'],check=True)
 dest=stage/'post-test-files';dest.mkdir(exist_ok=False)
 # Retain every replaced path for review; no destructive cleanup of test state.
 for p in roots:
  if os.path.lexists(p):
   q=dest/p.lstrip('/');q.parent.mkdir(parents=True,exist_ok=True)
   if os.stat(Path(p).parent).st_dev==os.stat(dest).st_dev:os.rename(p,q)
   else:
    import shutil
    if os.path.islink(p):q.symlink_to(os.readlink(p));os.unlink(p)
    elif os.path.isfile(p):shutil.copy2(p,q);os.unlink(p)
    else:raise RuntimeError('cross-filesystem directory '+p)
 subprocess.run(['tar','-xpf',str(stage/'baseline.tar'),'-C','/'],check=True)
 subprocess.run(['systemctl','daemon-reload'],check=True)
 for service in ['apache2','wsprrypi']:
  wanted=(stage/f'baseline-{service}-is-enabled.txt').read_text().strip()
  subprocess.run(['systemctl','enable' if wanted=='enabled' else 'disable',service],check=True)
  if (stage/f'baseline-{service}-is-active.txt').read_text().strip()=='active':subprocess.run(['systemctl','start',service],check=True)
 print('RESTORED; persistent revocation journal intentionally retained at current generation')
