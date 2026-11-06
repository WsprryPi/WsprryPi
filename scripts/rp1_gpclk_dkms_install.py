#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""WsprryPi selection, authentication, and client of the RP1-DKMS public facade."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import pathlib
import platform
import re
import shlex
import stat
import subprocess
import sys
import tempfile
import urllib.request
from dataclasses import dataclass
from typing import Any, Mapping, Sequence
sys.dont_write_bytecode = True

MIGRATION_REPOSITORY = REPOSITORY = 'WsprryPi/RP1-DKMS'
REPOSITORY_URL='https://github.com/'+REPOSITORY+'.git'
PUBLIC_CONTRACT='rp1-gpclk-provider-lifecycle-v1'
RECORD_SCHEMA='rp1-gpclk-dkms-provider-installation-v1'
RUNTIME_READINESS_CONTRACT='rp1-gpclk-runtime-readiness-v1'
RUNTIME_PROVIDER=pathlib.Path('/usr/lib/rp1-gpclk-dkms/runtime_provider.py')
PROVIDER_RECORD=pathlib.Path('/var/lib/rp1-gpclk-dkms/provider-installation.json')
ACTIVE_FACADE=pathlib.Path('/var/lib/rp1-gpclk-dkms/facade/active.json')
LEGACY_RECORD=pathlib.Path('/var/lib/wsprrypi/rp1-gpclk-dkms-installation.json')
SHA256=re.compile(r'^[0-9a-f]{64}$')
SHA40=re.compile(r'^[0-9a-f]{40}$')
MAX_API_BYTES=16*1024*1024
MAX_RECORD_BYTES=4*1024*1024

class ContractError(RuntimeError):
    """A fail-closed contract refusal."""


def canonical(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ContractError(message)


def json_result(result: CommandResult, operation: str) -> dict[str, Any]:
    try:
        value = strict_json(result.stdout)
    except json.JSONDecodeError as error:
        raise ContractError(f"RP1 runtime {operation} returned malformed JSON") from error
    require(isinstance(value, dict), f"RP1 runtime {operation} did not return an object")
    return value


@dataclass(frozen=True)
class CommandResult:
    stdout: str
    stderr: str
    returncode: int


class RuntimeOperationError(ContractError):
    """Structured failure returned by the RP1 runtime provider."""

    def __init__(
        self,
        operation: str,
        value: Mapping[str, Any],
        result: CommandResult,
        message: str,
    ) -> None:
        super().__init__(message)
        self.operation = operation
        self.value = dict(value)
        self.result = result


class Runner:
    def __init__(self, debug: bool = False):
        self.debug = debug

    def trace(self, argv: Sequence[str], cwd: pathlib.Path | None = None) -> None:
        if self.debug:
            location = f" (cwd={cwd})" if cwd is not None else ""
            print(f"[RP1 DEBUG] command{location}: {shlex.join(str(item) for item in argv)}", file=sys.stderr)

    def run(
        self,
        argv: Sequence[str],
        *,
        check: bool = True,
        cwd: pathlib.Path | None = None,
        passthrough: bool = False,
    ) -> CommandResult:
        self.trace(argv, cwd)
        try:
            if passthrough:
                process = subprocess.run(list(argv), cwd=cwd, text=True, check=False)
            else:
                process = subprocess.run(
                    list(argv), cwd=cwd, text=True, stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE, check=False,
                )
        except FileNotFoundError as error:
            result = CommandResult("", f"required command is unavailable: {argv[0]}", 127)
            if check:
                raise ContractError(result.stderr) from error
            return result
        result = CommandResult(process.stdout or "", process.stderr or "", process.returncode)
        if self.debug and not passthrough:
            if result.stdout:
                print(result.stdout, end="" if result.stdout.endswith("\n") else "\n")
            if result.stderr:
                print(result.stderr, end="" if result.stderr.endswith("\n") else "\n", file=sys.stderr)
        if check and process.returncode:
            if passthrough:
                detail = "see command output above"
            else:
                detail = result.stderr.strip() or result.stdout.strip() or "no diagnostic"
            raise ContractError(f"command failed ({argv[0]}): {detail}")
        return result


def secure_record_parent(path: pathlib.Path) -> None:
    parent = path.parent
    parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    absolute = pathlib.Path(os.path.abspath(parent))
    resolved = parent.resolve(strict=True)
    require(absolute == resolved, "installation record parent contains a symlink or path substitution")
    info = resolved.stat()
    require(info.st_uid == os.geteuid(), "installation record parent is not owned by the invoking user")
    require(stat.S_IMODE(info.st_mode) & 0o022 == 0, "installation record parent is group- or world-writable")


def atomic_json(path: pathlib.Path, value: Any, mode: int = 0o600) -> None:
    payload = canonical(value) + b"\n"
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        os.fchmod(descriptor, mode)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        parent_descriptor = os.open(path.parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            os.fsync(parent_descriptor)
        finally:
            os.close(parent_descriptor)
    finally:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass


def record_file_identity(path: pathlib.Path) -> os.stat_result:
    require(path.is_absolute(), "installation ownership record path must be absolute")
    secure_record_parent(path)
    try:
        info = path.lstat()
    except FileNotFoundError as error:
        raise ContractError("installation ownership record is absent") from error
    require(stat.S_ISREG(info.st_mode), "installation ownership record is not a regular file")
    require(info.st_uid == os.geteuid(), "installation ownership record is not owned by the invoking user")
    require(stat.S_IMODE(info.st_mode) & 0o077 == 0, "installation ownership record is accessible by group or others")
    require(info.st_size <= MAX_RECORD_BYTES, "installation ownership record exceeds the bounded size")
    return info


def runtime_call(
    runner: Runner,
    provider: pathlib.Path,
    operation: str,
    arguments: Sequence[str] = (),
    allowed_results: Iterable[str] = (),
) -> dict[str, Any]:
    result = runner.run(["python3", str(provider), operation, *arguments], check=False)
    value = json_result(result, operation)
    if result.returncode != 0:
        classification = value.get("result")
        if classification not in set(allowed_results):
            details = []
            error = value.get("error")
            if isinstance(error, dict) and isinstance(error.get("detail"), str):
                details.append(error["detail"])
            conflicts = value.get("conflicts")
            if (isinstance(conflicts, list) and conflicts
                    and all(isinstance(item, str) for item in conflicts)):
                details.append("conflicts: " + ", ".join(conflicts))
            remediation = value.get("remediation")
            if (isinstance(remediation, list) and remediation
                    and all(isinstance(item, str) for item in remediation)):
                details.append("remediation: " + ", ".join(remediation))
            detail = f" ({'; '.join(details)})" if details else ""
            raise RuntimeOperationError(
                operation, value, result,
                f"RP1 runtime {operation} failed with "
                f"{classification or 'unknown state'}{detail}",
            )
    return value


def validate_readiness(value: Mapping[str, Any], expected_result: str | None = None) -> dict[str, Any]:
    require(value.get("contract") == RUNTIME_READINESS_CONTRACT, "runtime readiness contract differs")
    if expected_result is not None:
        require(value.get("result") == expected_result and value.get("state") == expected_result, f"runtime readiness did not reach {expected_result}")
    return dict(value)



def strict_json(data):
    require(len(data)<=MAX_API_BYTES,'provider JSON exceeds bound')
    def unique(pairs):
        value={}
        for key,item in pairs:
            require(key not in value,'duplicate provider JSON field: '+key)
            value[key]=item
        return value
    return json.loads(data,object_pairs_hook=unique)


def requirements_path():
    installed=pathlib.Path(__file__).with_name('rp1-provider-requirements.json')
    return installed if installed.exists() else pathlib.Path(__file__).resolve().parents[1]/'config/rp1-provider-requirements.json'


def valid_product(value):
    if not isinstance(value,str) or len(value)>48:return False
    match=re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?',value)
    return bool(match) and not any(x.isdigit() and len(x)>1 and x.startswith('0') for x in (match[4] or '').split('.'))


def validate_consumer_provider(resolved, *, repository=None):
    require((repository or REPOSITORY)==REPOSITORY,'provider repository differs')
    value=resolved.get('identity')
    require(isinstance(value,dict),'provider public identity is missing')
    required=strict_json(requirements_path().read_bytes())
    require(type(value.get('schemaVersion')) is int and value['schemaVersion']==1
            and value.get('contract')=='rp1-gpclk-provider-identity-v1'
            and value.get('repository')==REPOSITORY and value.get('module')=='rp1-gpclk-dkms',
            'provider identity contract differs')
    require(value.get('interfaces')==required['interfaces'] and value.get('uapiSha256')==required['uapiSha256'],
            'provider UAPI/runtime interfaces are incompatible with this consumer')
    capabilities=value.get('capabilities')
    require(isinstance(capabilities,list) and all(isinstance(x,str) for x in capabilities)
            and len(capabilities)==len(set(capabilities))
            and set(required['capabilities'])<=set(capabilities),'provider capabilities are incompatible with this consumer')
    require(valid_product(value.get('productVersion')),
            'provider product identity is malformed')
    require(value.get('compatibilityIdentities')=={r:'v'+value['productVersion']+'-rp1-'+r for r in ('gpio4','gpio20')},
            'provider route identities contradict the product')
    require(isinstance(value.get('debianVersion'),str) and len(value['debianVersion'])<=128
            and re.fullmatch(r'[0-9][0-9A-Za-z.+:~\-]*',value['debianVersion']), 'provider native version is malformed')


def trusted(path):
    path=pathlib.Path(path)
    for parent in (path,*path.parents):
        info=parent.lstat()
        require(not stat.S_ISLNK(info.st_mode) and info.st_uid==0 and not info.st_mode&0o022,
                'unsafe provider facade path: '+str(parent))
    require(path.is_file(),'provider facade input is not a regular file')
    return path.read_bytes()


def retained_facade():
    value=strict_json(trusted(ACTIVE_FACADE))
    require(isinstance(value,dict) and type(value.get('schemaVersion')) is int and value['schemaVersion']==1 and value.get('contract')==PUBLIC_CONTRACT,
            'retained provider facade contract differs')
    root=pathlib.Path(value['root'])
    require(root.parent==ACTIVE_FACADE.parent and SHA256.fullmatch(root.name),'retained provider facade root differs')
    require(value['entrypoint']==str(root/'scripts/provider_lifecycle.py'),'retained provider entrypoint differs')
    expected={'scripts/provider_lifecycle.py','scripts/provider_identity.py','scripts/version_identity.py','scripts/runtime_layout.py','scripts/development_workflow.py',
              'include/rp1_gpclk/version.h','include/uapi/linux/rp1_gpclk.h','include/uapi/linux/rp1_route_admin.h'}
    require(isinstance(value.get('files'),dict) and set(value['files'])==expected,'retained provider closure differs')
    require(sha256_bytes(canonical(value['files']))==root.name,'retained provider closure identity differs')
    for name,digest in value['files'].items():
        require(isinstance(digest,str) and SHA256.fullmatch(digest) and sha256_bytes(trusted(root/name))==digest,
                'retained provider facade file changed')
    return root/'scripts/provider_lifecycle.py'


def invoke(entrypoint, operation, *arguments):
    process=subprocess.run(['python3','-E','-s','-B',str(entrypoint),'--client','WsprryPi/WsprryPi',operation,*map(str,arguments)],text=True,
                           stdout=subprocess.PIPE,stderr=sys.stderr,check=False)
    try: value=strict_json(process.stdout)
    except (ValueError,TypeError) as error: raise ContractError('provider returned malformed structured result') from error
    require(isinstance(value,dict) and type(value.get('schemaVersion')) is int and value['schemaVersion']==1
            and value.get('contract')==PUBLIC_CONTRACT and value.get('operation')==operation,
            'provider result contract/operation differs')
    require(process.returncode==0 and value.get('status')=='ok',
            'provider '+operation+' failed: '+str(value.get('detail','nonzero result')))
    return value


def provider_identity():
    value=invoke(retained_facade(),'identity')['providerIdentity']
    validate_consumer_provider({'identity':value})
    return value


def load_ownership_record(path, *, repository=None):
    try:
        require((repository or REPOSITORY)==REPOSITORY,'provider repository differs')
        if not path.exists() and not path.is_symlink(): return None,None,'provider receipt absent'
        before=record_file_identity(path)
        reply=invoke(retained_facade(),'status','--record',path)
        record=reply.get('receipt')
        require(isinstance(record,dict) and record.get('schema')==RECORD_SCHEMA
                and record.get('repository')==REPOSITORY and record.get('client')=='WsprryPi/WsprryPi','provider receipt contract/client differs')
        require(reply.get('recordSha256')==sha256_file(path),'provider receipt changed during inspection')
        after=record_file_identity(path)
        require((before.st_dev,before.st_ino,before.st_mtime_ns)==(after.st_dev,after.st_ino,after.st_mtime_ns),
                'provider receipt changed during inspection')
        return record,after,None
    except (OSError,ValueError,KeyError,ContractError) as error:
        return None,None,str(error)


def run_git(source,*args):
    return subprocess.check_output(['git','-c','safe.directory='+str(source),'-C',str(source),*args],text=True).strip()


def source_policy(source):
    require(source not in {'','HEAD','unknown','detached'},'ambiguous application source; select RP1 provider explicitly')
    return 'production' if source in {'main','master','release'} or re.fullmatch(r'v?\d+\.\d+\.\d+',source) else 'development'


def fetch_json(url):
    headers={'User-Agent':'WsprryPi-provider-bootstrap','Accept':'application/vnd.github+json'}
    if os.environ.get('GITHUB_TOKEN'): headers['Authorization']='Bearer '+os.environ['GITHUB_TOKEN']
    with urllib.request.urlopen(urllib.request.Request(url,headers=headers),timeout=60) as response:
        data=response.read(MAX_API_BYTES+1)
    return strict_json(data)


def canonical_origin(value):
    for prefix in ['https://github.com/','ssh://git@github.com/','git@github.com:']:
        if value.startswith(prefix):
            path=value[len(prefix):].removesuffix('/').removesuffix('.git')
            require(path==REPOSITORY,'provider checkout authority differs')
            return REPOSITORY_URL
    raise ContractError('provider checkout authority differs')


def bootstrap(state, selector, policy):
    if selector=='auto': selector='release' if policy=='production' else 'devel'
    selected=selector
    if selector=='release':
        releases=fetch_json('https://api.github.com/repos/'+REPOSITORY+'/releases?per_page=100')
        require(isinstance(releases,list),'release listing malformed')
        candidates=[]
        for value in releases:
            require(isinstance(value,dict),'release listing contains a malformed entry')
            tag=value.get('tag_name','')
            if (not value.get('draft',True) and not value.get('prerelease',True) and value.get('published_at')
                    and re.fullmatch(r'v?(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)',tag)):
                names={item.get('name') for item in value.get('assets',[]) if isinstance(item,dict)}
                if {'SHA256SUMS','rp1-gpclk-dkms-installation-manifest-v1.json'}<=names:
                    candidates.append((tuple(map(int,tag.lstrip('v').split('.'))),tag))
        require(bool(candidates),'no eligible published provider release')
        reference='refs/tags/'+max(candidates)[1]
        origin=REPOSITORY_URL
    elif selector=='devel': reference='origin/devel'; origin=REPOSITORY_URL
    elif selector.startswith('commit:'):
        reference=selector[7:]; require(SHA40.fullmatch(reference),'provider commit must be a full SHA'); origin=REPOSITORY_URL
    elif selector.startswith('checkout:'):
        source=pathlib.Path(selector[9:])
        require(source.is_absolute() and source.resolve()==source,'provider checkout must be canonical and absolute')
        canonical_origin(run_git(source,'remote','get-url','origin'))
        require(not run_git(source,'status','--porcelain=v1','--untracked-files=all'),'provider checkout is dirty')
        require(not any(line and line[0] not in {'H','?'} for line in run_git(source,'ls-files','-v').splitlines()),
                'provider checkout has hidden tracked-file state')
        reference=run_git(source,'rev-parse','HEAD');origin=str(source)
    else: raise ContractError('invalid provider source selector')
    checkout=state/'provider-source'
    subprocess.run(['git','clone','--no-local','--no-checkout',origin,str(checkout)],check=True,stdout=sys.stderr,stderr=sys.stderr)
    if origin!=REPOSITORY_URL:
        subprocess.run(['git','-C',str(checkout),'remote','set-url','origin',REPOSITORY_URL],check=True)
    subprocess.run(['git','-C',str(checkout),'checkout','--detach',reference],check=True,stdout=sys.stderr,stderr=sys.stderr)
    commit=run_git(checkout,'rev-parse','HEAD')
    if selector.startswith('checkout:'):require(commit==reference,'provider snapshot changed')
    entrypoint=checkout/'scripts/provider_lifecycle.py'
    require(entrypoint.is_file() and not entrypoint.is_symlink(),'selected provider lacks the public lifecycle interface')
    metadata=invoke(entrypoint,'identity')['providerIdentity']
    validate_consumer_provider({'identity':metadata})
    # Development resolution uses the authenticated frozen snapshot. Release
    # resolution remains release-channel verification inside the provider.
    return entrypoint, ('release' if selected=='release' else 'checkout:'+str(checkout))


def skipped_auto(args):
    if args.install=='false': return True
    if args.install=='true': return False
    try:
        model=pathlib.Path('/proc/device-tree/model').read_bytes().lower()
        compatible=pathlib.Path('/proc/device-tree/compatible').read_bytes().split(b'\0')
    except OSError:return True
    return not (any(c in {b'brcm,bcm2712',b'brcm,bcm2712d0'} for c in compatible) and (b'raspberry pi 5' in model or b'compute module 5' in model))


def prepare_client(args):
    state=args.state_dir
    if skipped_auto(args):
        atomic_json(state/'client.json',{'skip':True})
        print('RP1-DKMS installation skipped by platform/operator selection.')
        return
    policy=source_policy(args.wsprry_source) if args.source=='auto' else 'explicit-source'
    if args.dry_run:
        print('Dry run: would authenticate selected provider and invoke its public lifecycle plan; no provider effects.')
        atomic_json(state/'client.json',{'skip':True})
        return
    entrypoint,source=bootstrap(state,args.source,policy)
    # Export an existing attributable client receipt to the provider's explicit
    # adoption adapter; the provider never discovers/reads application state.
    if LEGACY_RECORD.exists():
        require(not PROVIDER_RECORD.exists(),'both legacy and provider receipts exist')
        old_identity=record_file_identity(LEGACY_RECORD)
        old_bytes=LEGACY_RECORD.read_bytes()
        receipt=state/'predecessor-receipt.json'
        atomic_json(receipt,strict_json(old_bytes))
        invoke(entrypoint,'adopt','--receipt',receipt)
        require(record_file_identity(LEGACY_RECORD)==old_identity and LEGACY_RECORD.read_bytes()==old_bytes,
                'legacy receipt changed during provider adoption; retained for recovery')
        LEGACY_RECORD.unlink()
    req=state/'consumer-requirements.json'
    atomic_json(req,strict_json(requirements_path().read_bytes()))
    response=invoke(entrypoint,'prepare','--state-dir',state,'--install',args.install,'--source',source,
                    '--source-policy',policy,'--requirements',req)
    require(response.get('decision',{}).get('install') is True,'provider did not plan installation')
    require(isinstance(response.get('planSha256'),str) and SHA256.fullmatch(response['planSha256']), 'provider preparation omitted its plan digest')
    atomic_json(state/'client.json',{'skip':False,'entrypoint':str(entrypoint),'planSha256':response['planSha256']})
    print('RP1-DKMS public plan: '+str(response.get('transition')))


def removal_result(args,outcome):
    path=getattr(args,'result_file',None)
    if path is not None:
        require(path.is_absolute(),'application removal result path must be absolute')
        secure_record_parent(path)
        require(not path.exists() and not path.is_symlink(),'application removal result already exists')
        atomic_json(path,{'schemaVersion':1,'contract':'wsprrypi-rp1-provider-removal-v1','outcome':outcome})


def execute_client(args):
    if args.command=='remove' and args.remove=='false':
        removal_result(args,'preserved')
        print('Provider preserved by explicit operator opt-out.')
        return
    state=args.state_dir
    if state is not None:
        record=strict_json((state/'client.json').read_bytes())
        if record['skip']: return
        entrypoint=pathlib.Path(record['entrypoint'])
        require(entrypoint==state/'provider-source/scripts/provider_lifecycle.py','provider staging reference differs')
        plan=invoke(entrypoint,'plan-status','--state-dir',state)
        require(plan.get('planSha256')==record.get('planSha256'),'provider plan changed outside the acknowledged transaction')
        arguments=['--state-dir',state]
    else:
        if not ACTIVE_FACADE.exists():
            require(not PROVIDER_RECORD.exists() and not LEGACY_RECORD.exists(),'provider receipt has no authenticated facade')
            require(getattr(args,'remove','auto')!='true','explicit provider removal has no attributable installation')
            removal_result(args,'unmanaged')
            print('No application-managed provider facade exists; no provider removal requested.')
            return
        entrypoint=retained_facade()
        plan=invoke(entrypoint,'removal-plan')
        arguments=[]
    require(isinstance(plan.get('planSha256'),str) and SHA256.fullmatch(plan['planSha256']), 'provider plan digest missing')
    arguments+=['--plan-sha256',plan['planSha256']]
    if args.command=='remove': arguments+=['--remove',args.remove]
    response=invoke(entrypoint,args.command,*arguments)
    if state is not None:
        require(isinstance(response.get('planSha256'),str) and SHA256.fullmatch(response['planSha256']), 'provider effect omitted its successor plan digest')
        record['planSha256']=response['planSha256']
        atomic_json(state/'client.json',record)
    if args.command=='remove' and args.remove!='false':
        require(response.get('postconditions')=={'providerAbsent':True,'transmissionAuthorized':False},
                'provider removal did not prove absence')
        removal_result(args,'absent')
    print('RP1-DKMS '+args.command+' completed through its public lifecycle interface.')


def parser():
    result=argparse.ArgumentParser(description=__doc__)
    result.add_argument('--provider-repository',default=REPOSITORY,choices=[REPOSITORY])
    sub=result.add_subparsers(dest='command',required=True)
    prepare=sub.add_parser('prepare')
    prepare.add_argument('--state-dir',type=pathlib.Path,required=True)
    prepare.add_argument('--install',choices=['auto','true','false'],default='auto')
    prepare.add_argument('--source',default='auto')
    prepare.add_argument('--wsprry-source',required=True)
    prepare.add_argument('--dry-run',action='store_true')
    prepare.add_argument('--debug',action='store_true')
    for name in ['apply','prepare-runtime-update','activate-runtime','prepare-runtime-removal','remove']:
        item=sub.add_parser(name)
        item.add_argument('--state-dir',type=pathlib.Path,required=name in {'apply','prepare-runtime-update','activate-runtime'})
        item.add_argument('--debug',action='store_true')
        if name=='remove':
            item.add_argument('--remove',choices=['auto','true','false'],default='auto')
            item.add_argument('--result-file',type=pathlib.Path)
    return result


def main(argv=None):
    args=parser().parse_args(argv)
    try:
        if args.command=='prepare': prepare_client(args)
        else: execute_client(args)
        return 0
    except (ContractError,OSError,ValueError,KeyError,TypeError,subprocess.SubprocessError) as error:
        print('RP1-DKMS operation refused: '+str(error),file=sys.stderr)
        return 1


if __name__=='__main__':raise SystemExit(main())
