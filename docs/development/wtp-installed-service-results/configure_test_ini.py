import configparser,sys,os,time,json
from pathlib import Path
p=Path('/usr/local/etc/wsprrypi.ini');c=configparser.ConfigParser(strict=False);c.optionxform=str;c.read(p)
c['Operation'].update({'Transmit':'true' if sys.argv[1]=='on' else 'false','Mode':'QRSS','Enable on Boot':'Follow','Use LED':'false','Use Amp':'false','Use Shutdown':'false'})
c['CW'].update({'Message':'E','Base Frequency':'14097100','Dot Seconds':'3','Start Minute':str((time.gmtime().tm_min+30)%60),'Start Second':'0','Repeat Minutes':'60'})
c['WTP Server']={'Enabled':'true','Port':'31417','Interface':'wlan0'}
tmp=p.with_suffix('.installed-test.tmp')
with tmp.open('w') as f:c.write(f);f.flush();os.fsync(f.fileno())
os.chmod(tmp,0o644);os.replace(tmp,p)
print(json.dumps({'enabled':sys.argv[1]=='on','at_utc_ns':str(time.time_ns())}))
