#!/usr/bin/env python3
"""Protected adapter: refresh on image check, retain exact code for recovery."""
import argparse
import contextlib
import hashlib
import io
import json
from pathlib import Path
import stat
import sys
import types

ROOT = Path('/usr/local/lib/wsprrypi/pico-usb')
CACHE = Path('/var/cache/wsprrypi/pico-usb')
STATE = Path('/var/lib/wsprrypi/pico-usb')


def protected(path):
    for item in (path,*path.parents):
        info = item.lstat()
        if stat.S_ISLNK(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
            raise ValueError('Shared installer must remain root-owned and protected')


def manager(root):
    protected(root/'deployment.json')
    descriptor = json.loads((root/'deployment.json').read_bytes())
    if descriptor.get('schema') != 'wsprrypi-pico-usb-deployment/2':
        raise ValueError('Unsupported shared installer deployment')
    verified = {}
    if set(descriptor['adapter_files']) != {'entry.py','bundle.py','policy.json'}:
        raise ValueError('Incomplete shared installer adapter')
    for name,digest in descriptor['adapter_files'].items():
        path = root/name
        protected(path)
        verified[name] = path.read_bytes()
        if hashlib.sha256(verified[name]).hexdigest() != digest:
            raise ValueError('Shared installer adapter changed; reinstall the Pi package')
    module = types.ModuleType('pico_usb_bundle')
    module.__file__ = str(root/'bundle.py')
    exec(compile(verified['bundle.py'],module.__file__,'exec'),module.__dict__)
    return module,module.Store(root,module.decode(verified['policy.json']))


def invoke(manifest,payload,arguments):
    module = types.ModuleType('wsprrypico_shared_installer')
    module.__file__ = str(ROOT/'retained-usb-installer.py')
    exec(compile(payload['usb_installer.py'],module.__file__,'exec'),module.__dict__)
    if module.INSTALLER_VERSION != manifest['installer_version']:
        raise ValueError('Shared installer version differs from its receipt')
    with contextlib.redirect_stdout(io.StringIO()) as output:
        code = module.main(arguments,schema_bytes=payload['release-manifest-1.schema.json'])
    result = json.loads(output.getvalue())
    if result.get('schema') != 'wsprrypico-usb-result/1' or result.get('installer_version') != manifest['installer_version']:
        raise ValueError('Incompatible shared installer result')
    result['installer_contract'] = 1
    result['installer_source'] = manifest['source']
    return code,result


def run(arguments,distribution,store,cache=CACHE,state=STATE):
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('operation',choices=('plan','upgrade','recover','discover'))
    parser.add_argument('--output',type=Path)
    parser.add_argument('--bundle',type=Path)
    parser.add_argument('--transaction',type=Path)
    parser.add_argument('--state-dir',type=Path,default=state)
    args,_ = parser.parse_known_args(arguments)
    if args.state_dir != state:
        raise ValueError('The Pi adapter uses its protected private state directory')
    state.mkdir(parents=True,exist_ok=True,mode=0o700)
    if args.operation != 'recover':
        cache.mkdir(parents=True,exist_ok=True,mode=0o700)
    with distribution.lock(store.root,owner=store.owner):
        pending = 0 if args.operation == 'recover' else store.prune(cache,state)
        update = None
        if args.operation == 'recover':
            if args.transaction is None:
                raise ValueError('Recovery requires its retained transaction')
            manifest,payload = store.recovery(args.transaction.absolute(),state)
        else:
            if args.operation == 'plan':
                if args.output is None or args.output.parent != cache:
                    raise ValueError('Image checks require a protected firmware cache destination')
                selected = store.refresh()
                identity,update = selected['identity'],selected['update']
                store.bind(args.output.name,identity)
            elif args.operation == 'upgrade':
                if args.bundle is None or args.bundle.parent != cache:
                    raise ValueError('Installation requires its checked firmware bundle')
                identity = store.planned(args.bundle.name)
                if pending >= 8:
                    raise ValueError('Eight unresolved firmware transactions retained; finish recovery before another update')
                bucket = store.snapshot(state,identity)
                arguments = list(arguments)
                if '--state-dir' in arguments:
                    arguments[arguments.index('--state-dir')+1] = str(bucket)
                else:
                    arguments += ['--state-dir',str(bucket)]
            else:
                identity = store.active()['identity']
            manifest,payload = store.load(identity)
        try:
            code,result = invoke(manifest,payload,arguments)
            if update:
                result['installer_update'] = update
            return code,result
        finally:
            if args.operation != 'recover':
                store.prune(cache,state)


def main():
    root = Path(__file__).resolve().parent
    distribution,store = manager(root)
    code,result = run(sys.argv[1:],distribution,store)
    print(json.dumps(result,sort_keys=True))
    return code


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as error:
        print(json.dumps({'schema':'wsprrypico-usb-result/1','installer_contract':1,
                          'installer_version':'adapter','ok':False,'error':str(error),
                          'recovery_required':len(sys.argv)>1 and sys.argv[1] in ('upgrade','recover'),
                          **getattr(error,'details',{})}))
        sys.exit(2)
