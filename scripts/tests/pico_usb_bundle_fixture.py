"""Synthetic verified installer source and a hardware-free executable core."""
import base64
import hashlib
import json

REPOSITORY = 'WsprryPi/WsprryPico'
API = 'https://api.github.com/repos/'+REPOSITORY+'/'
SERIAL = '1545B6EFBF78028C'
POLICY = {'schema':'wsprrypi-pico-usb-policy/1','contract':1,'channel':'devel'}


def encoded(value):
    return (json.dumps(value,indent=2,sort_keys=True)+'\n').encode()


def core(version):
    return ("INSTALLER_VERSION="+repr(version)+"\n"+'''
import argparse,json,tempfile
from pathlib import Path
def erase_transaction(args): pass
def main(argv=None,schema_bytes=None):
    p=argparse.ArgumentParser();p.add_argument('op');p.add_argument('--output');p.add_argument('--bundle');p.add_argument('--transaction');p.add_argument('--state-dir');p.add_argument('--device');p.add_argument('--branch')
    args,_=p.parse_known_args(argv)
    result=dict(schema='wsprrypico-usb-result/1',installer_version=INSTALLER_VERSION,ok=True,serial='1545B6EFBF78028C',recovery_required=False,marker=INSTALLER_VERSION)
    if args.op=='plan':
        out=Path(args.output);out.mkdir();(out/'firmware.uf2').write_bytes(b'firmware fixture')
    if args.op in ('upgrade','erase'):
        directory=Path(tempfile.mkdtemp(prefix='1545B6EFBF78028C-',dir=args.state_dir));path=directory/'transaction.json'
        record=dict(schema='wsprrypico-usb-transaction/1',serial='1545B6EFBF78028C',installer_version=INSTALLER_VERSION,stage='awaiting_bootsel',recovery_required=True,saved='untouched',operation=args.op)
        path.write_text(json.dumps(record));path.chmod(0o600)
        result.update(transaction=str(path),stage='awaiting_bootsel',recovery_required=True)
        if args.op=='erase':result.update(operation='erase',manual_bootsel_required=True)
    if args.op=='recover':
        path=Path(args.transaction);record=json.loads(path.read_text())
        assert record['installer_version']==INSTALLER_VERSION
        assert record['saved']=='untouched'
        record.update(stage='complete',recovery_required=False);path.write_text(json.dumps(record))
        result.update(stage='complete',saved=record['saved'])
        if record.get('operation')=='erase':result.update(operation='erase',erase_verified=True,erased_bytes=4194304)
    print(json.dumps(result));return 0
''').encode()


def fixture(run=100,version='0.1.1'):
    payload = {'usb_installer.py':core(version),'release-manifest-1.schema.json':encoded({'$id':'https://github.com/'+REPOSITORY+'/firmware-release-manifest/1'})}
    manifest = {'schema':'wsprrypico-usb-installer/1','repository':REPOSITORY,'contract':1,
        'result_schema':'wsprrypico-usb-result/1','transaction_schema':'wsprrypico-usb-transaction/1',
        'firmware_manifest_schema':1,'python_min':[3,10],'installer_version':version,'channel':'devel',
        'source':('a' if run==100 else 'b')*40,'run_id':run,'attempt':1,
        'workflow':'.github/workflows/software-build.yml','verify_job':{'id':42,'name':'verify','status':'completed','conclusion':'success'},
        'files':{name:{'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)} for name,data in payload.items()}}
    return manifest,payload


class Network:
    def __init__(self,manifest,payload):
        self.manifest,self.payload,self.requests = manifest,payload,[]
        self.original = dict(payload)
        self.failure = None
        self.mutate = lambda value:value
    def api(self,path):
        self.requests.append(path)
        if self.failure:
            raise self.failure
        m=self.manifest
        if path=='commits/devel':return self.mutate({'sha':m['source']})
        if path=='actions/workflows/software-build.yml':return self.mutate({'id':123,'path':m['workflow']})
        run={'id':m['run_id'],'head_sha':m['source'],'head_branch':'devel','path':m['workflow'],'workflow_id':123,
             'repository':{'full_name':REPOSITORY},'head_repository':{'full_name':REPOSITORY},
             'run_attempt':m['attempt'],'event':'push','status':'completed','conclusion':'success'}
        if path.startswith('actions/workflows/software-build.yml/runs?'):return self.mutate({'workflow_runs':[run]})
        if path==f"actions/runs/{m['run_id']}/attempts/1":return self.mutate(run)
        if path==f"actions/runs/{m['run_id']}/attempts/1/jobs?per_page=100":
            jobs=[{**m['verify_job'],'run_id':m['run_id'],'head_sha':m['source'],'run_attempt':1},
                  {'id':43,'name':'publish','run_id':m['run_id'],'head_sha':m['source'],'run_attempt':1,'status':'completed','conclusion':'skipped'}]
            return self.mutate({'total_count':2,'jobs':jobs})
        raise AssertionError(path)
    def get(self,url,limit=1024*1024):
        self.requests.append(url)
        name=url.split('?')[0].rsplit('/',1)[1]
        data=self.payload[name];original=self.original[name]
        return encoded(self.mutate({'type':'file','path':'scripts/'+('ci/' if name.endswith('.json') else '')+name,
            'encoding':'base64','size':len(original),'content':base64.b64encode(data).decode(),
            'sha':hashlib.sha1(b'blob '+str(len(original)).encode()+b'\0'+original).hexdigest()}))
