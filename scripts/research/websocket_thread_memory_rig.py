#!/usr/bin/env python3
"""RF-inert diagnostic for issue #349; never sends a state-changing command.

This is deliberately a conservative research harness.  It launches an isolated
copy of the installed daemon with a rewritten *temporary* INI, connects only to
localhost, and leaves the production INI and binary untouched.
"""
import argparse, base64, configparser, csv, errno, hashlib, json, math, os
import platform, re, secrets, shutil, signal, socket, struct, subprocess, sys
import tempfile, threading, time
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path

VERSION = "issue-349-rig-1"
STD_PORTS = {31415, 31416}
COMPONENT_PATHS = ("WsprryPi-UI", "src/INI-Handler", "src/LCBLog",
                   "src/Mailbox", "src/MonitorFile", "src/PPM-Manager",
                   "src/Signal-Handler", "src/Singleton",
                   "src/WSPR-Transmitter", "src/WSPR-Reference")
STATUS_FIELDS = ("VmPeak", "VmSize", "VmRSS", "RssAnon", "RssFile", "VmData",
                 "VmStk", "VmSwap", "Threads")
SMAPS_FIELDS = ("Rss", "Pss", "Pss_Anon", "Pss_File", "Private_Clean",
                "Private_Dirty", "Anonymous", "Swap")
GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

class RigError(RuntimeError): pass
class Abort(RigError): pass

def now(): return datetime.now(timezone.utc).isoformat()
def mib(v): return None if v is None else v / 1024.0
def read(path, default=None):
    try: return Path(path).read_text()
    except (OSError, PermissionError): return default
def write(path, data):
    Path(path).write_text(data)
def command(args, timeout=15):
    try:
        p = subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=timeout, check=False)
        return p.returncode, p.stdout, p.stderr
    except (OSError, subprocess.TimeoutExpired) as e: return 127, "", str(e)
def available_port(port):
    if not (1024 <= port <= 49151) or port in STD_PORTS: return False
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        s.bind(("127.0.0.1", port)); return True
    except OSError: return False
    finally: s.close()
def choose_ports():
    for _ in range(200):
        a, b = secrets.randbelow(40000)+1024, secrets.randbelow(40000)+1024
        if a != b and available_port(a) and available_port(b): return a, b
    raise RigError("could not reserve two valid loopback ports")

def parse_kib(text, wanted):
    out = {x: None for x in wanted}
    for line in (text or "").splitlines():
        m = re.match(r"^([A-Za-z_]+):\s+(\d+)\s+kB", line)
        if m and m.group(1) in out: out[m.group(1)] = int(m.group(2))
        m = re.match(r"^([A-Za-z_]+):\s+(\d+)$", line)
        if m and m.group(1) in out: out[m.group(1)] = int(m.group(2))
    return out
def parse_range(line):
    m = re.match(r"^([0-9A-Fa-f]+)-([0-9A-Fa-f]+)\s+([rwxps-]{4})\s+", line)
    if not m: return None
    return int(m.group(1), 16), int(m.group(2), 16), m.group(3)
def map_summary(pid):
    text = read(f"/proc/{pid}/maps", "")
    large=[]; total=0; count=0
    for line in text.splitlines():
        x=parse_range(line)
        if x and x[2].startswith("rw") and ("/" not in line and "[" not in line):
            count += 1; total += x[1]-x[0]
            if x[1]-x[0] >= 1024*1024: large.append({"range":line.split()[0],"bytes":x[1]-x[0]})
    return {"anonymous_writable_count":count, "anonymous_writable_bytes":total,
            "large_anonymous_writable":large[:128]}
def tcp_count(port):
    n=0
    for table in ("/proc/net/tcp", "/proc/net/tcp6"):
        for line in (read(table, "") or "").splitlines()[1:]:
            f=line.split()
            if len(f)>=4 and int(f[1].split(":")[1],16)==port and f[3]=="01": n+=1
    return n
def psi():
    x=read("/proc/pressure/memory")
    return x.strip() if x is not None else None
def system_mem():
    d=parse_kib(read("/proc/meminfo", ""), ("MemAvailable","SwapFree"))
    d["memory_psi"]=psi(); return d
def sample(pid, port, tag):
    if not Path(f"/proc/{pid}").exists(): raise Abort("isolated daemon PID exited")
    d={"utc":now(),"epoch":time.time(),"tag":tag,"pid":pid}
    d.update(parse_kib(read(f"/proc/{pid}/status", ""), STATUS_FIELDS))
    d.update({"smaps_"+k:v for k,v in parse_kib(read(f"/proc/{pid}/smaps_rollup", ""),SMAPS_FIELDS).items()})
    try:d["tasks"]=len(list(Path(f"/proc/{pid}/task").iterdir()))
    except OSError:d["tasks"]=None
    try:d["fds"]=len(list(Path(f"/proc/{pid}/fd").iterdir()))
    except OSError:d["fds"]=None
    x=read(f"/proc/{pid}/oom_score"); d["oom_score"]=int(x.strip()) if x and x.strip().isdigit() else None
    d["tcp_active"]=tcp_count(port); d.update(system_mem()); d.update(map_summary(pid)); return d

def frame(opcode, payload=b"", mask=True):
    if isinstance(payload,str): payload=payload.encode()
    if len(payload)>125: raise RigError("rig control frame unexpectedly long")
    key=secrets.token_bytes(4) if mask else b""; b2=(0x80 if mask else 0)|len(payload)
    body=bytes(a^key[i%4] for i,a in enumerate(payload)) if mask else payload
    return bytes([0x80|opcode,b2])+key+body
def recv_exact(s,n):
    b=b""
    while len(b)<n:
        x=s.recv(n-len(b))
        if not x: raise EOFError("peer closed")
        b+=x
    return b
def recv_frame(s):
    a,b=recv_exact(s,2); opcode=a&15; size=b&127; masked=bool(b&128)
    if size==126: size=struct.unpack("!H",recv_exact(s,2))[0]
    elif size==127: size=struct.unpack("!Q",recv_exact(s,8))[0]
    if size>65536: raise RigError("oversize server frame")
    key=recv_exact(s,4) if masked else b""; payload=recv_exact(s,size)
    if masked: payload=bytes(x^key[i%4] for i,x in enumerate(payload))
    return opcode,payload
def handshake(port, timeout):
    s=socket.create_connection(("127.0.0.1",port),timeout); s.settimeout(timeout)
    key=base64.b64encode(secrets.token_bytes(16)).decode()
    req=("GET / HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
         "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: %s\r\n\r\n")%(port,key)
    s.sendall(req.encode()); got=b""
    while b"\r\n\r\n" not in got and len(got)<16384: got+=s.recv(1024)
    lines=got.decode("iso-8859-1").split("\r\n"); headers={}
    if not lines or " 101 " not in lines[0]: raise RigError("upgrade did not return HTTP 101")
    for line in lines[1:]:
        if ":" in line: k,v=line.split(":",1); headers[k.lower()]=v.strip()
    expected=base64.b64encode(hashlib.sha1((key+GUID).encode()).digest()).decode()
    if headers.get("sec-websocket-accept")!=expected: raise RigError("invalid Sec-WebSocket-Accept")
    return s
def close_ws(s):
    try:
        s.sendall(frame(8));
        try:
            end=time.monotonic()+1
            while time.monotonic()<end:
                o,p=recv_frame(s)
                if o==9:s.sendall(frame(10,p))
                if o==8:break
        except (OSError, EOFError): pass
    finally: s.close()
def response_is_state(text):
    try: return isinstance(json.loads(text),dict) and "tx_state" in json.loads(text)
    except (ValueError,TypeError): return False
def one_connection(port, kind, timeout):
    started=time.monotonic(); r={"utc":now(),"pattern":kind,"success":False,"response":"none"}
    s=None
    try:
        s=handshake(port,timeout); r["handshake"]="ok"
        if kind=="get-state":
            s.sendall(frame(1,'{"command":"get_tx_state"}')); deadline=time.monotonic()+timeout
            while time.monotonic()<deadline:
                o,p=recv_frame(s)
                if o==9: s.sendall(frame(10,p)); continue
                if o==8: raise RigError("server closed before state response")
                if o==1:
                    text=p.decode("utf-8","replace")
                    if response_is_state(text): r["response"]="valid_tx_state"; break
                    r["response"]="event_or_nonstate_text"
            if r["response"]!="valid_tx_state": raise RigError("timed out awaiting valid tx-state response")
        if kind=="abrupt": s.close(); s=None
        else: close_ws(s); s=None
        r["success"]=True
    except Exception as e: r["error"]=str(e)
    finally:
        if s:
            try:s.close()
            except OSError:pass
        r["duration_s"]=round(time.monotonic()-started,6)
    return r

def load_ini(path):
    c=configparser.ConfigParser(interpolation=None); c.optionxform=str
    if not c.read(path): raise RigError("cannot read installed INI")
    return c
def rewrite_ini(src,dst,http,ws):
    c=load_ini(src); required={"Operation":("Transmit","Enable on Boot","Use LED","Use Amp","Amp Pin","Use Shutdown","Web Port","Socket Port"),
      "Band GPIO":()}
    for sec,keys in required.items():
        if not c.has_section(sec): raise RigError("required canonical section missing: "+sec)
        for key in keys:
            if not c.has_option(sec,key): raise RigError("required canonical key missing: %s.%s"%(sec,key))
    c.set("Operation","Transmit","false"); c.set("Operation","Enable on Boot","Never")
    c.set("Operation","Use LED","false"); c.set("Operation","Use Amp","false"); c.set("Operation","Amp Pin","")
    c.set("Operation","Use Shutdown","false"); c.set("Operation","Web Port",str(http)); c.set("Operation","Socket Port",str(ws))
    for key in list(c["Band GPIO"]):
        if not key.lower().endswith("active high"): c.set("Band GPIO",key,"")
    with open(dst,"w") as f:c.write(f)
def redact_ini(src,dst):
    text=read(src,"")
    text=re.sub(r"(?im)^(.*(?:password|token|secret|apikey).*?=).*?$",r"\1 <redacted>",text)
    write(dst,text)
def proc_children(pid):
    out=[]
    for p in Path("/proc").iterdir():
        if not p.name.isdigit():continue
        st=read(p/"stat","") or ""; x=st.rsplit(") ",1)
        if len(x)==2 and len(x[1].split())>2 and x[1].split()[1]==str(pid): out.append(int(p.name))
    return out
def select_descendant(launcher):
    # sudo normally forks, but some configurations exec the target in place.
    # In the latter case the launcher PID is now demonstrably the daemon, not
    # the sudo launcher, and is safe to sample.
    if (read(f"/proc/{launcher}/comm", "") or "").strip().startswith("wsprrypi"):
        return launcher
    children=proc_children(launcher)
    candidates=[]
    while children:
        p=children.pop()
        # /proc/<root-pid>/exe can be unreadable to the invoking user even when
        # status/smaps are readable.  comm is the kernel executable name and is
        # sufficient here because this searches only the isolated launch tree.
        if (read(f"/proc/{p}/comm", "") or "").strip().startswith("wsprrypi"): candidates.append(p)
        children.extend(proc_children(p))
    if len(candidates)!=1: raise RigError("expected one wsprrypi descendant, found %r"%candidates)
    return candidates[0]
def service_active(): return command(["systemctl","is-active","--quiet","wsprrypi.service"])[0]==0
def wspr_pids():
    ans=[]
    for p in Path("/proc").iterdir():
        if p.name.isdigit() and (read(p/"comm", "") or "").strip() == "wsprrypi": ans.append(int(p.name))
    return ans
def live(pid):
    x=read(f"/proc/{pid}/stat", "") or ""
    return bool(x) and x.rsplit(") ",1)[-1].split()[0] != "Z"
def terminate_group(pgid, launcher, daemon, timeout=15):
    outcome={"sigkill":False,"term_sent":False,"launcher_returncode":None,"daemon_live_after_term":None}
    try: os.killpg(pgid,signal.SIGTERM); outcome["term_sent"]=True
    except ProcessLookupError:return outcome
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        if not live(daemon): return outcome
        time.sleep(.1)
    outcome["daemon_live_after_term"]=live(daemon)
    try: os.killpg(pgid,signal.SIGKILL); outcome["sigkill"]=live(daemon)
    except ProcessLookupError:pass
    return outcome
def slope(points,key):
    xy=[(p[0],p[1].get(key)) for p in points if p[1].get(key) is not None]
    if len(xy)<2:return None
    mx=sum(x for x,y in xy)/len(xy); my=sum(y for x,y in xy)/len(xy); den=sum((x-mx)**2 for x,y in xy)
    return None if not den else sum((x-mx)*(y-my) for x,y in xy)/den

class Artifacts:
 def __init__(self,root):
    self.path=Path(root)/("issue-349-"+datetime.now().strftime("%Y%m%dT%H%M%SZ")); self.path.mkdir(parents=True); os.chmod(self.path,0o700)
    self.conn=open(self.path/"connections.jsonl","w", buffering=1); self.samples=open(self.path/"samples.csv","w",newline="",buffering=1); self.writer=None; self.rows=[]; self.completed_connections=0
 def connection(self,x): self.conn.write(json.dumps(x,sort_keys=True)+"\n"); self.conn.flush()
 def sample(self,x):
    if not self.writer:self.writer=csv.DictWriter(self.samples,fieldnames=list(x)); self.writer.writeheader()
    self.writer.writerow({k:x.get(k) for k in self.writer.fieldnames}); self.samples.flush(); self.rows.append(x)
 def text(self,name,value):write(self.path/name,str(value))
 def close(self):self.conn.close();self.samples.close()

def safety(s,base,args):
 if s.get("VmSize") is not None and base.get("VmSize") is not None and s["VmSize"]-base["VmSize"]>args.vmsize_limit*1024:
    args.vmsize_reached=True
 for key,limit in (("VmRSS",args.rss_limit*1024),("smaps_Pss",args.pss_limit*1024)):
    if s.get(key) is not None and base.get(key) is not None and s[key]-base[key]>limit: raise Abort("%s threshold exceeded"%key)
 if s.get("MemAvailable") is not None and s["MemAvailable"]<args.memavailable_limit*1024:raise Abort("MemAvailable threshold exceeded")
def snapshot(a,pid,port,tag,base,args):
 s=sample(pid,port,tag); s["completed_connections"]=a.completed_connections; a.sample(s); safety(s,base,args) if base else None; return s

def self_test():
 assert parse_kib("VmSize: 12 kB\nThreads: 3\n",STATUS_FIELDS)["VmSize"]==12
 assert parse_kib("Pss_Anon: 9 kB",SMAPS_FIELDS)["Pss_Anon"]==9 and parse_range("ffff-1ffff rw-p 0 00:00 0")[1]==0x1ffff
 assert not available_port(31415) and not available_port(1023)
 f=frame(8); assert f[0:2]==b"\x88\x80" and len(f)==6 # masked close construction
 key=base64.b64encode(b"0123456789abcdef").decode(); accept=base64.b64encode(hashlib.sha1((key+GUID).encode()).digest()).decode(); assert accept=="BACScCJPNqyz+UBoqMH89VmURoA="
 assert response_is_state('{"tx_state":"idle"}') and not response_is_state('{"event":"x"}')
 assert abs(slope([(0,{"x":1}),(1,{"x":3})],"x")-2)<.001
 with tempfile.TemporaryDirectory() as d:
  p=Path(d)/"a.ini"; p.write_text("[Operation]\nTransmit=true\nEnable on Boot=Always\nUse LED=true\nUse Amp=true\nAmp Pin=5\nUse Shutdown=true\nWeb Port=1\nSocket Port=2\n[Band GPIO]\n20m=5\n")
  rewrite_ini(p,Path(d)/"b.ini",20000,20001); q=load_ini(Path(d)/"b.ini"); assert q["Operation"]["Transmit"]=="false" and q["Band GPIO"]["20m"]==""
  Path(d,"missing.ini").write_text("[Operation]\nTransmit=false\n[Band GPIO]\n")
  try: rewrite_ini(Path(d)/"missing.ini",Path(d)/"c.ini",20000,20001)
  except RigError: pass
  else: raise AssertionError("missing canonical keys accepted")
 print("self-test passed")

def main(args):
 if args.self_test: self_test(); return 0
 if not args.dry_run and os.geteuid()!=0: raise RigError("live rig must be run with sudo so smaps, FD, and map evidence is authoritative")
 args.vmsize_reached=False
 a=Artifacts(args.artifact_root); initial_active=service_active(); launch=None; isolated=None; pgid=None; abort=None; cleanup={}; completed=[]; base=None
 try:
  # Read-only preflight evidence.
  for name,cmd in (("parent_git.txt",["git","rev-parse","HEAD"]),("parent_status.txt",["git","status","--short","--branch"]),("component_trees.txt",["git","ls-tree","HEAD","--",*COMPONENT_PATHS]),("component_maintenance.md",["git","show","HEAD:docs/components/README.md"]),("installed_version.txt",["/usr/local/bin/wsprrypi","--version"]),("service_definition.txt",["systemctl","cat","wsprrypi.service"]),("service_execstart.txt",["systemctl","show","-p","ExecStart","wsprrypi.service"])):
   rc,o,e=command(cmd); a.text(name,o+e)
  binary=Path(args.binary); ini=Path(args.installed_ini)
  if not binary.is_file() or not ini.is_file():raise RigError("installed binary or INI not found")
  a.text("installed_binary_sha256.txt",hashlib.sha256(binary.read_bytes()).hexdigest()+"\n"); a.text("binary_architecture.txt",command(["file",str(binary)])[1])
  a.text("system.txt",platform.platform()+"\n"+read("/proc/device-tree/model","")+"\n")
  redact_ini(ini,a.path/"installed.ini.redacted"); original=ini.read_bytes(); binary_hash=hashlib.sha256(binary.read_bytes()).digest()
  c=load_ini(ini)
  if not c.has_section("Operation") or c.get("Operation","Transmit",fallback=None).lower()!="false" or c.get("Operation","Enable on Boot",fallback=None)!="Never":raise RigError("installed INI fails required RF-inert Operation preflight")
  existing=wspr_pids();
  if initial_active and len(existing)!=1:raise RigError("unexpected WsprryPi process count: %r"%existing)
  if not initial_active and existing:raise RigError("WsprryPi exists while service is inactive")
  if existing: a.text("installed_process_initial.json",json.dumps(sample(existing[0],0,"installed-preflight"),indent=2))
  a.text("free.initial.txt",command(["free","-k"])[1]);a.text("meminfo.initial.txt",read("/proc/meminfo","") or "");a.text("swaps.initial.txt",read("/proc/swaps","") or "")
  http,ws=choose_ports(); a.text("selected_ports.txt","http=%d\nwebsocket=%d\n"%(http,ws))
  td=tempfile.TemporaryDirectory(prefix="issue349-rig-"); tmpini=Path(td.name)/"wsprrypi.ini";rewrite_ini(ini,tmpini,http,ws);redact_ini(tmpini,a.path/"temporary.ini.redacted")
  a.text("launch_command.txt","sudo -n %s -i %s\n"%(binary,tmpini))
  a.text("service_state_before.txt","active=%s\npids=%s\n"%(initial_active,wspr_pids()))
  if args.dry_run:
   a.text("summary.txt","DRY RUN: preflight completed; service was not stopped and daemon was not launched.\n"); td.cleanup();a.close();print(a.path);return 0
  if initial_active:
   rc,o,e=command(["sudo","systemctl","stop","wsprrypi.service"],30)
   if rc:raise RigError("could not stop pre-existing service: "+e)
  if service_active() or wspr_pids():raise RigError("service stop did not leave zero WsprryPi processes")
  launch=subprocess.Popen(["sudo","-n",str(binary),"-i",str(tmpini)],stdout=open(a.path/"daemon.stdout","w"),stderr=open(a.path/"daemon.stderr","w"),start_new_session=True,text=True)
  pgid=launch.pid; a.text("launch_interval.txt","start=%s\nlauncher_pid=%s\npgid=%s\n"%(now(),launch.pid,pgid))
  end=time.monotonic()+args.startup_timeout
  while time.monotonic()<end:
   try:
    isolated=select_descendant(launch.pid); handshake(ws,args.timeout).close();break
   except (RigError,OSError):time.sleep(.25)
  if not isolated:raise RigError("isolated daemon readiness/descendant selection timed out")
  a.text("process_ids.txt","launcher=%s\nresolved_wsprrypi=%s\n"%(launch.pid,isolated)); base=snapshot(a,isolated,ws,"startup",None,args)
  for i in range(args.baseline): snapshot(a,isolated,ws,"baseline",base,args);time.sleep(1)
  def phase(kind,count):
   nonlocal base
   for i in range(count):
    if args.vmsize_reached: completed.append(kind+" (stopped at VmSize threshold)"); return
    snapshot(a,isolated,ws,"before-%s-%d"%(kind,i+1),base,args); r=one_connection(ws,kind,args.timeout); a.connection(r)
    if not r["success"]: raise Abort("connection failure: "+r.get("error","unknown"))
    a.completed_connections+=1; snapshot(a,isolated,ws,"after-%s-%d"%(kind,i+1),base,args);time.sleep(args.pace)
    if (i+1)%10==0:time.sleep(args.batch_quiescence);snapshot(a,isolated,ws,"boundary-%s-%d"%(kind,i+1),base,args)
   completed.append(kind)
  phase("get-state-warmup",args.warmup);base=snapshot(a,isolated,ws,"post-warmup-baseline",None,args)
  phase("graceful",args.graceful);phase("get-state",args.get_state);phase("abrupt",args.abrupt)
  def burst(kind):
   snapshot(a,isolated,ws,"before-burst-"+kind,base,args); results=[]
   threads=[threading.Thread(target=lambda:results.append(one_connection(ws,kind,args.timeout))) for _ in range(args.burst)]
   [x.start() for x in threads];[x.join(args.timeout+2) for x in threads]
   if len(results)!=args.burst or not all(x["success"] for x in results):raise Abort("burst failure")
   for x in results:a.connection(x)
   snapshot(a,isolated,ws,"after-burst-"+kind,base,args);completed.append("burst-"+kind)
  if args.burst:
   burst("graceful");burst("abrupt")
  for _ in range(args.recovery):snapshot(a,isolated,ws,"recovery",base,args);time.sleep(1)
  completed.append("recovery")
 except (RigError,Abort,KeyboardInterrupt) as e:abort=str(e)
 finally:
  if isolated and Path(f"/proc/{isolated}").exists():
   try:snapshot(a,isolated,ws,"before-cleanup",base,args)
   except RigError:pass
  if pgid:
   cleanup=terminate_group(pgid,launch.pid,isolated)
   if launch:
    try: cleanup["launcher_returncode"]=launch.wait(timeout=2)
    except subprocess.TimeoutExpired: cleanup["launcher_returncode"]="unreaped"
   a.text("cleanup_outcome.json",json.dumps(cleanup,indent=2))
  if initial_active:
   rc,o,e=command(["sudo","systemctl","start","wsprrypi.service"],30)
   if rc:abort=(abort or "")+"; service restore failed: "+e
  deadline=time.monotonic()+15
  while time.monotonic()<deadline and not (service_active() and len(wspr_pids())==1): time.sleep(.2)
  a.text("service_state_after.txt","active=%s\npids=%s\n"%(service_active(),wspr_pids()))
  a.text("free.final.txt",command(["free","-k"])[1]);a.text("meminfo.final.txt",read("/proc/meminfo","") or "");a.text("swaps.final.txt",read("/proc/swaps","") or "")
  if pgid: a.text("journald.isolated.txt",command(["journalctl","--since",read(a.path/"launch_interval.txt","").splitlines()[0].replace("start=","") if (a.path/"launch_interval.txt").exists() else "-10min","-u","wsprrypi.service","--no-pager"])[1])
  if ini.read_bytes()!=original or hashlib.sha256(binary.read_bytes()).digest()!=binary_hash:abort=(abort or "")+"; installed binary or INI changed unexpectedly"
  con=[]
  try:con=[json.loads(x) for x in (a.path/"connections.jsonl").read_text().splitlines()]
  except OSError:pass
  lines=["Issue #349 WebSocket thread memory rig", "completed="+", ".join(completed), "abort="+str(abort), "connections="+str(Counter((x["pattern"],x["success"]) for x in con)), "VmStk generally describes the main process stack, not all pthread stacks."]
  if a.rows:
   for k in ("VmSize","VmRSS","smaps_Pss","smaps_Anonymous","VmData","VmSwap","tasks","fds","tcp_active","MemAvailable","anonymous_writable_count","anonymous_writable_bytes"):
    vals=[r.get(k) for r in a.rows if r.get(k)is not None]
    if vals:lines.append("%s baseline=%s peak=%s ending=%s delta=%s slope_per_connection=%s"%(k,vals[0],max(vals),vals[-1],vals[-1]-vals[0],slope([(r.get("completed_connections",0),r) for r in a.rows],k)))
   for pat in ("graceful","get-state","abrupt"):
    pr=[r for r in a.rows if r.get("tag","").startswith("after-"+pat+"-") and "warmup" not in r.get("tag","")]
    if len(pr)>1: lines.append("%s VmData_delta=%s VmData_slope=%s VmSize_delta=%s VmSize_slope=%s"%(pat,pr[-1].get("VmData")-pr[0].get("VmData"),slope(list(enumerate(pr,1)),"VmData"),pr[-1].get("VmSize")-pr[0].get("VmSize"),slope(list(enumerate(pr,1)),"VmSize")))
  lines.append("result=supports theory only when retained post-recovery VmData/VmSize growth is approximately linear while tasks, FDs, and TCP return to baseline; otherwise inconclusive.")
  a.text("launch_interval.txt",(read(a.path/"launch_interval.txt","") or "")+"finish=%s\n"%now())
  a.text("summary.txt","\n".join(lines)+"\n");a.close()
  if os.geteuid()==0 and os.environ.get("SUDO_UID"):
   uid,gid=int(os.environ["SUDO_UID"]),int(os.environ.get("SUDO_GID",os.environ["SUDO_UID"]))
   for root,dirs,files in os.walk(a.path):
    os.chown(root,uid,gid)
    for name in files: os.chown(os.path.join(root,name),uid,gid)
 print(a.path); return 1 if abort else 0

if __name__=="__main__":
 p=argparse.ArgumentParser(description=__doc__);p.add_argument("--artifact-root",default=os.path.expanduser("~/wsprrypi-websocket-memory-results"));p.add_argument("--binary",default="/usr/local/bin/wsprrypi");p.add_argument("--installed-ini",default="/usr/local/etc/wsprrypi.ini");p.add_argument("--dry-run",action="store_true");p.add_argument("--self-test",action="store_true");p.add_argument("--baseline",type=int,default=30);p.add_argument("--warmup",type=int,default=10);p.add_argument("--graceful",type=int,default=30);p.add_argument("--get-state",type=int,default=30);p.add_argument("--abrupt",type=int,default=30);p.add_argument("--burst",type=int,default=8);p.add_argument("--recovery",type=int,default=70);p.add_argument("--pace",type=float,default=.15);p.add_argument("--batch-quiescence",type=float,default=1);p.add_argument("--timeout",type=float,default=4);p.add_argument("--startup-timeout",type=float,default=20);p.add_argument("--vmsize-limit",type=int,default=512);p.add_argument("--rss-limit",type=int,default=128);p.add_argument("--pss-limit",type=int,default=128);p.add_argument("--memavailable-limit",type=int,default=1024);ns=p.parse_args();
 if ns.burst>8 or min(ns.baseline,ns.warmup,ns.graceful,ns.get_state,ns.abrupt,ns.recovery)<0 or ns.recovery<70 and not ns.self_test: p.error("counts must be nonnegative and recovery default/minimum is 70 seconds")
 try:sys.exit(main(ns))
 except Exception as e:print("fatal: "+str(e),file=sys.stderr);sys.exit(2)
