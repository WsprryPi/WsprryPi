"""Synthetic public installer releases and a hardware-free executable core."""
import hashlib
import json

REPOSITORY = 'WsprryPi/WsprryPico'
API = 'https://api.github.com/repos/'+REPOSITORY+'/'
DOWNLOAD = 'https://github.com/'+REPOSITORY+'/releases/download/'
SERIAL = '1545B6EFBF78028C'
POLICY = {'schema':'wsprrypi-pico-usb-policy/1','contract':1,'channel':'devel'}


def encoded(value):
    return (json.dumps(value,indent=2,sort_keys=True)+'\n').encode()


def core(version):
    return ("INSTALLER_VERSION="+repr(version)+"\n"+'''
import argparse,json,tempfile
from pathlib import Path
def main(argv=None,schema_bytes=None):
    p=argparse.ArgumentParser();p.add_argument('op');p.add_argument('--output');p.add_argument('--bundle');p.add_argument('--transaction');p.add_argument('--state-dir');p.add_argument('--device');p.add_argument('--branch')
    args,_=p.parse_known_args(argv)
    result=dict(schema='wsprrypico-usb-result/1',installer_version=INSTALLER_VERSION,ok=True,serial='1545B6EFBF78028C',recovery_required=False,marker=INSTALLER_VERSION)
    if args.op=='plan':
        out=Path(args.output);out.mkdir();(out/'firmware.uf2').write_bytes(b'firmware fixture')
    if args.op=='upgrade':
        directory=Path(tempfile.mkdtemp(prefix='1545B6EFBF78028C-',dir=args.state_dir));path=directory/'transaction.json'
        record=dict(schema='wsprrypico-usb-transaction/1',serial='1545B6EFBF78028C',installer_version=INSTALLER_VERSION,stage='awaiting_bootsel',recovery_required=True,saved='untouched')
        path.write_text(json.dumps(record));path.chmod(0o600)
        result.update(transaction=str(path),stage='awaiting_bootsel',recovery_required=True)
    if args.op=='recover':
        path=Path(args.transaction);record=json.loads(path.read_text())
        assert record['installer_version']==INSTALLER_VERSION
        assert record['saved']=='untouched'
        record.update(stage='complete',recovery_required=False);path.write_text(json.dumps(record))
        result.update(stage='complete',saved=record['saved'])
    print(json.dumps(result));return 0
''').encode()


def fixture(run=100,version='0.1.1'):
    payload = {'usb_installer.py':core(version),'release-manifest-1.schema.json':b'{}\n'}
    manifest = {'schema':'wsprrypico-usb-installer/1','repository':REPOSITORY,'contract':1,
        'result_schema':'wsprrypico-usb-result/1','transaction_schema':'wsprrypico-usb-transaction/1',
        'firmware_manifest_schema':1,'python_min':[3,10],'installer_version':version,'channel':'devel',
        'source':('a' if run==100 else 'b')*40,'run_id':run,'attempt':1,
        'workflow':'.github/workflows/usb-installer.yml','verify_job':{'id':42,'name':'verify','status':'completed','conclusion':'success'},
        'tag':f'usb-installer-v1-devel-{run}.1',
        'files':{name:{'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)} for name,data in payload.items()}}
    return manifest,payload


class Network:
    def __init__(self,manifest,payload):
        self.manifest,self.payload,self.requests = manifest,payload,[]
        self.failure = None
        self.mutate = lambda value:value
    def api(self,path):
        self.requests.append(path)
        if self.failure:
            raise self.failure
        m=self.manifest;receipt=encoded(m)
        if path=='releases/tags/usb-installer-v1-devel':
            index={key:m[key] for key in ('schema','repository','contract','channel','source','run_id','attempt','tag')}
            index['manifest_sha256']=hashlib.sha256(receipt).hexdigest()
            return self.mutate({'draft':False,'prerelease':True,'assets':[],
                'body':'<!-- wsprrypico-usb-installer/1\n'+json.dumps(index)+'\n-->'})
        if path=='releases/tags/'+m['tag']:
            return self.mutate({'draft':False,'prerelease':True,'tag_name':m['tag'],
                'assets':[{'name':name,'state':'uploaded','size':len(data),'digest':'sha256:'+hashlib.sha256(data).hexdigest(),
                           'browser_download_url':DOWNLOAD+m['tag']+'/'+name} for name,data in {**self.payload,'usb-installer.json':receipt}.items()]})
        if path=='git/ref/tags/'+m['tag']:
            return self.mutate({'object':{'type':'commit','sha':m['source'],'url':API+'git/commits/'+m['source']}})
        if path==f"actions/runs/{m['run_id']}/attempts/1":
            return self.mutate({'head_sha':m['source'],'head_branch':'devel','path':m['workflow'],'run_attempt':1,
                               'event':'push','status':'completed','conclusion':'success'})
        raise AssertionError(path)
    def get(self,url,limit=1024*1024):
        self.requests.append(url)
        name=url.rsplit('/',1)[1]
        return encoded(self.manifest) if name=='usb-installer.json' else self.payload[name]
