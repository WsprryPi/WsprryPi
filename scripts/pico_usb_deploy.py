#!/usr/bin/env python3
"""Install the protected contract adapter; shared installer releases are independent."""
import argparse
import ast
import errno
import importlib.util
import json
import os
from pathlib import Path
import shutil
import tempfile

SPEC = importlib.util.spec_from_file_location('pico_usb_bundle',Path(__file__).with_name('pico_usb_bundle.py'))
bundle = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bundle)
FILES = {'scripts/usb_installer.py','scripts/ci/release-manifest-1.schema.json'}


def remove_empty_legacy_directories(target):
    # Old deployments may contain Python caches or administrator files. Only
    # the two owned source files are removed; unrelated contents are retained.
    for relative in ('scripts/ci','scripts'):
        try:
            (target/relative).rmdir()
        except OSError as error:
            if error.errno not in (errno.ENOTEMPTY,errno.EEXIST):
                raise


def retained(prior,payload,channel,kind='retained-installation'):
    tree = ast.parse(payload['usb_installer.py'])
    versions = [ast.literal_eval(node.value) for node in tree.body if isinstance(node,ast.Assign)
                and any(isinstance(t,ast.Name) and t.id == 'INSTALLER_VERSION' for t in node.targets)]
    bundle.require(len(versions) == 1, 'Installer has no unambiguous version')
    return {'schema':bundle.MARKER,'repository':bundle.REPOSITORY,'contract':1,'channel':channel,
            'result_schema':'wsprrypico-usb-result/1','transaction_schema':'wsprrypico-usb-transaction/1',
            'firmware_manifest_schema':1,'python_min':[3,10],'installer_version':versions[0],
            'source':prior['revision'],'origin':{'kind':kind,'previous_deployment':prior},
            'files':{name:{'sha256':bundle.sha(data),'bytes':len(data)} for name,data in payload.items()}}


def provision(action,repository,root=Path('/'),fetch=None,development_source=None):
    prefix = lambda name:root/name.lstrip('/')
    target = prefix('/usr/local/lib/wsprrypi/pico-usb')
    policy = bundle.decode((repository/'config/pico-usb-installer.json').read_bytes())
    bundle.Store(target,policy)
    bundle.safe(target,owner=False)
    with bundle.lock(target,owner=root == Path('/')):
        prior = None
        if target.exists():
            prior = bundle.decode((target/'deployment.json').read_bytes())
            expected = (prior['adapter_files'] if prior.get('schema') == 'wsprrypi-pico-usb-deployment/2' else
                        {**prior['files'],'entry.py':prior['entry_sha256']})
            bundle.require((prior.get('removed') is True and expected == {}) or
                           set(expected) == ({'entry.py','bundle.py','policy.json'} if 'adapter_files' in prior else FILES | {'entry.py'}),
                           'Unknown deployed installer inventory')
            for name,digest in expected.items():
                path = target/name
                bundle.safe(path,owner=False)
                bundle.require(path.is_file() and bundle.sha(path.read_bytes()) == digest,
                               'Changed deployed installer retained for administrator review')
        if action == 'uninstall':
            if prior is None:
                return
            if 'adapter_files' not in prior:
                store = bundle.Store(target,policy,owner=root == Path('/'))
                payload = {Path(name).name:(target/name).read_bytes() for name in FILES}
                identity = store.save(retained(prior,payload,policy['channel']),payload)
                bundle.write_json(target/'legacy.json',{'identity':identity})
                bundle.write_json(target/'active.json',{'identity':identity,'check_after':0,'update':{'state':'retained'}})
            # Retain immutable installers and private data for recovery.
            for name in expected:
                (target/name).unlink()
            bundle.write_json(target/'deployment.json',{'schema':'wsprrypi-pico-usb-deployment/2','adapter_files':{},'removed':True})
            for name in ('/etc/udev/rules.d/70-wsprrypico-usb.rules','/usr/lib/tmpfiles.d/wsprrypico-usb.conf'):
                path = prefix(name)
                bundle.safe(path,owner=False)
                if path.exists() and path.read_text() == owned_text(name):
                    path.unlink()
            return
        holds = prefix('/var/lib/wsprrypi/pico-usb-control')
        bundle.require(not holds.exists() or not any(holds.iterdir()), 'Finish Pico USB maintenance before replacing its adapter')
        target.parent.mkdir(parents=True,exist_ok=True)
        stage = Path(tempfile.mkdtemp(prefix='.pico-usb-',dir=target.parent))
        try:
            if target.exists():
                shutil.copytree(target,stage,dirs_exist_ok=True)
            store = bundle.Store(stage,policy,owner=root == Path('/'))
            if prior is not None and 'adapter_files' not in prior:
                payload = {Path(name).name:(target/name).read_bytes() for name in FILES}
                identity = store.save(retained(prior,payload,policy['channel']),payload)
                bundle.write_json(stage/'legacy.json',{'identity':identity})
                bundle.write_json(stage/'active.json',{'identity':identity,'check_after':0,'update':{'state':'retained'}})
                for name in FILES:
                    (stage/name).unlink()
                remove_empty_legacy_directories(stage)
            if development_source is not None:
                source = development_source.resolve(strict=True)
                payload = {}
                for name in FILES:
                    bundle.safe(source/name,owner=False)
                    payload[Path(name).name] = (source/name).read_bytes()
                identity = store.save(retained({'revision':'0'*40},payload,policy['channel'],'unpublished-working-tree'),payload)
                bundle.write_json(stage/'active.json',{'identity':identity,'check_after':0,'update':{'state':'retained'}})
            elif not (stage/'active.json').exists():
                client = None
                if fetch is not None:
                    class Client(bundle.Network):
                        def get(self,url,limit=bundle.MAX_FILE):
                            data = fetch(url)
                            bundle.require(len(data) <= limit,'Oversized installer download')
                            return data
                    client = Client()
                store.refresh(client)
            else:
                store.load(store.active()['identity'])
            files = {'entry.py':(repository/'scripts/pico_usb_entry.py').read_bytes(),
                     'bundle.py':(repository/'scripts/pico_usb_bundle.py').read_bytes(),
                     'policy.json':(json.dumps(policy,indent=2,sort_keys=True)+'\n').encode()}
            for name,data in files.items():
                bundle.atomic(stage/name,data,0o644)
            descriptor = {'schema':'wsprrypi-pico-usb-deployment/2',
                          'adapter_files':{name:bundle.sha(data) for name,data in files.items()}}
            bundle.atomic(stage/'deployment.json',(json.dumps(descriptor,indent=2,sort_keys=True)+'\n').encode(),0o644)
            stage.chmod(0o755)
            if not target.exists():
                os.replace(stage,target)
            else:
                # The shared lock excludes entry processes. Metadata is last.
                for name in ('bundles','plans'):
                    if (stage/name).exists():
                        shutil.copytree(stage/name,target/name,dirs_exist_ok=True)
                for path in stage.iterdir():
                    if path.is_file() and path.name != 'deployment.json':
                        os.replace(path,target/path.name)
                os.replace(stage/'deployment.json',target/'deployment.json')
                if prior and 'adapter_files' not in prior:
                    for name in FILES:
                        (target/name).unlink()
                    remove_empty_legacy_directories(target)
        finally:
            if stage.exists():
                shutil.rmtree(stage)
        for name,mode in (('/var/lib/wsprrypi/pico-usb',0o700),('/var/lib/wsprrypi/pico-usb-control',0o700),
                          ('/var/cache/wsprrypi/pico-usb',0o700),('/run/lock/wsprrypico',0o3770)):
            path = prefix(name)
            bundle.safe(path,owner=False)
            path.mkdir(parents=True,exist_ok=True)
            path.chmod(mode)
            if root == Path('/'):
                import grp
                os.chown(path,0,grp.getgrnam('dialout').gr_gid if name.startswith('/run/') else 0)
        for name in ('/etc/udev/rules.d/70-wsprrypico-usb.rules','/usr/lib/tmpfiles.d/wsprrypico-usb.conf'):
            path = prefix(name)
            bundle.safe(path,owner=False)
            path.parent.mkdir(parents=True,exist_ok=True)
            bundle.require(not path.exists() or path.read_text() == owned_text(name),'Unrecognized USB permission file retained')
            path.write_text(owned_text(name))
            path.chmod(0o644)


def owned_text(name):
    if name.endswith('.conf'):
        return '# WsprryPi Pico USB installer\nd /run/lock/wsprrypico 3770 root dialout -\n'
    return ('# WsprryPi Pico USB installer\n'
            'SUBSYSTEM=="tty", ATTRS{idVendor}=="cafe", ATTRS{idProduct}=="4012", GROUP="dialout", MODE="0660"\n'
            'SUBSYSTEM=="usb", ATTR{idVendor}=="2e8a", ATTR{idProduct}=="000f", GROUP="dialout", MODE="0660"\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=('install','uninstall'))
    parser.add_argument('--repository',type=Path,required=True)
    parser.add_argument('--development-source',type=Path,help='Explicit unpublished core; never claims CI provenance')
    args = parser.parse_args()
    if os.geteuid() != 0:
        parser.error('Package-time provisioning requires root')
    provision(args.action,args.repository,development_source=args.development_source)
