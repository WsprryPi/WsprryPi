#!/usr/bin/env python3
"""Contract-1 installer distribution. No USB, firmware or RF authority."""
import ast
import base64
import contextlib
from datetime import datetime, timezone
from email.utils import parsedate_to_datetime
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

REPOSITORY = 'WsprryPi/WsprryPico'
API = 'https://api.github.com/repos/' + REPOSITORY + '/'
MARKER = 'wsprrypico-usb-installer/1'
FILES = {'usb_installer.py', 'release-manifest-1.schema.json'}
HEX = r'[0-9a-f]{64}'
PLAN = r'[0-9a-f]{32}'
MAX_FILE = 1024 * 1024


class Unavailable(ValueError):
    def __init__(self, message, **details):
        super().__init__(message)
        self.details = details


class Incompatible(Unavailable):
    """Verified source that cannot be used by this adapter or Python host."""


def require(ok, message):
    if not ok:
        raise Unavailable(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def unique(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'Duplicate installer metadata')
        result[key] = value
    return result


def decode(data):
    return json.loads(data, object_pairs_hook=unique)


def safe(path, owner=True):
    for item in (path, *path.parents):
        if not item.exists() and not item.is_symlink():
            continue
        info = item.lstat()
        require(not stat.S_ISLNK(info.st_mode), 'Symlink in installer storage')
        if owner:
            require(info.st_uid == 0 and not info.st_mode & 0o022, 'Installer storage must be root-owned and protected')


def atomic(path, data, mode=0o600):
    safe(path, owner=False)
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    fd, name = tempfile.mkstemp(prefix='.installer-', dir=path.parent)
    try:
        os.fchmod(fd, mode)
        with os.fdopen(fd, 'wb') as target:
            target.write(data)
            target.flush()
            os.fsync(target.fileno())
        os.replace(name, path)
        directory = os.open(path.parent, os.O_RDONLY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        Path(name).unlink(missing_ok=True)


def write_json(path, value):
    atomic(path, (json.dumps(value, indent=2, sort_keys=True)+'\n').encode())


@contextlib.contextmanager
def lock(root, owner=True):
    root.parent.mkdir(parents=True, exist_ok=True)
    path = root.parent / '.pico-usb.lock'
    safe(path,owner)
    fd = os.open(path, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX)
        yield
    finally:
        os.close(fd)


class Network:
    def get(self, url, limit=MAX_FILE):
        require(url.startswith(API), 'Unexpected installer download origin')
        request = urllib.request.Request(url, headers={'User-Agent':'WsprryPi-USB-Installer/1', 'Accept':'application/vnd.github+json'})
        try:
            with urllib.request.urlopen(request, timeout=20) as response:
                require(urllib.parse.urlsplit(response.url).scheme == 'https', 'Insecure installer download')
                data = response.read(limit+1)
                require(len(data) <= limit, 'Oversized installer download')
                length = response.headers.get('Content-Length')
                require(length is None or int(length) == len(data), 'Incomplete installer download')
                return data
        except urllib.error.HTTPError as error:
            with error:
                headers = error.headers or {}
                message = error.read(8192).lower()
                if error.code in (403, 429) and (error.code == 429 or headers.get('X-RateLimit-Remaining') == '0'
                        or headers.get('Retry-After') is not None or b'rate limit' in message):
                    now = int(time.time())
                    deadlines = []
                    reset = headers.get('X-RateLimit-Reset', '')
                    if headers.get('X-RateLimit-Remaining') == '0' and re.fullmatch(r'[0-9]{1,12}', reset):
                        if now <= int(reset) <= now+604800:
                            deadlines.append(int(reset)+1)
                    retry = headers.get('Retry-After', '')
                    if re.fullmatch(r'[0-9]{1,6}', retry):
                        if int(retry) <= 604800:
                            deadlines.append(now+int(retry)+1)
                    elif retry:
                        try:
                            date = parsedate_to_datetime(retry)
                            if date.tzinfo and now <= date.timestamp() <= now+604800:
                                deadlines.append(int(date.timestamp())+1)
                        except (ValueError, OverflowError, TypeError):
                            pass
                    deadline = max(deadlines) if deadlines else now+60
                    when = datetime.fromtimestamp(deadline, timezone.utc).strftime('%Y-%m-%d %H:%M:%S UTC')
                    guidance = '' if deadlines else ' GitHub provided no usable retry time; wait at least one minute.'
                    raise Unavailable(f'GitHub installer update request rate-limited (HTTP {error.code}). Retry after {when}.{guidance}',
                                      error_code='github_rate_limited', retry_at=deadline) from error
                if error.code == 404:
                    raise Unavailable('A verified compatible installer source is unavailable.', http_status=404) from error
                raise Unavailable(f'Installer update request unavailable (HTTP {error.code}).', http_status=error.code) from error
        except (OSError, urllib.error.URLError) as error:
            raise Unavailable('Installer update check could not connect to GitHub.') from error

    def api(self, path):
        return decode(self.get(API+path, 256*1024))


def firmware_contract(source):
    try:
        tree=ast.parse(source) if isinstance(source,(bytes,str)) else source
        declared=[ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and
                  any(isinstance(t,ast.Name) and t.id=='FIRMWARE_MANIFEST_SCHEMA' for t in n.targets)]
    except (SyntaxError,ValueError,TypeError) as error:
        raise Incompatible('Unknown firmware manifest contract') from error
    require(not declared or (len(declared)==1 and type(declared[0]) is int and declared[0] in (1,2)),
            'Unknown firmware manifest contract')
    return declared[0] if declared else 1

def validate(manifest, channel):
    require(isinstance(manifest, dict) and manifest.get('schema') == {1:MARKER,2:'wsprrypico-usb-installer/2'}.get(manifest.get('contract')) and type(manifest.get('contract')) is int and manifest['contract'] in (1, 2) and
            manifest.get('repository') == REPOSITORY and manifest.get('channel') == channel and
            channel in ('main', 'devel') and re.fullmatch(r'[0-9a-f]{40}', manifest.get('source','')) and
            re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', manifest.get('installer_version','')) and
            manifest.get('result_schema') == 'wsprrypico-usb-result/1' and
            manifest.get('transaction_schema') == 'wsprrypico-usb-transaction/1' and
            type(manifest.get('firmware_manifest_schema')) is int and manifest['firmware_manifest_schema'] == manifest['contract'],
            'Installer update has an incompatible contract')
    minimum = manifest.get('python_min')
    require(isinstance(minimum, list) and len(minimum) == 2 and all(type(n) is int and 0 <= n < 100 for n in minimum)
            and tuple(minimum) >= (3,10) and tuple(minimum) <= sys.version_info[:2], 'Installer needs an unsupported Python version')
    require(set(manifest.get('files',{})) == {'usb_installer.py', f"release-manifest-{manifest['contract']}.schema.json"}, 'Installer file inventory differs from the supported contract')
    for item in manifest['files'].values():
        require(set(item) == {'sha256','bytes'} and re.fullmatch(HEX,item['sha256']) and
                type(item['bytes']) is int and 0 < item['bytes'] <= MAX_FILE, 'Invalid installer checksum inventory')
    return manifest


def source_file(client, path, source):
    record = decode(client.get(API+'contents/'+path+'?ref='+source, 2*MAX_FILE))
    require(isinstance(record,dict) and isinstance(record.get('content'),str) and
            len(record['content']) <= 2*MAX_FILE and record.get('type') == 'file' and record.get('path') == path and
            record.get('encoding') == 'base64' and type(record.get('size')) is int and
            0 < record['size'] <= MAX_FILE, 'Invalid installer source file')
    try:
        data = base64.b64decode(''.join(record['content'].split()), validate=True)
    except (ValueError, TypeError) as error:
        raise Unavailable('Invalid installer source encoding') from error
    blob = hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
    require(len(data) == record['size'] and blob == record.get('sha'), 'Installer Git blob checksum mismatch')
    return data


def metadata(client, path):
    value = client.api(path)
    require(isinstance(value,dict), 'Invalid installer verification metadata')
    return value


def branch_head(client, channel):
    # The commits endpoint includes file diffs and can exceed the metadata
    # budget on large commits. A Git ref binds the same head without the diff.
    reference = metadata(client, 'git/ref/heads/'+channel)
    obj = reference.get('object')
    require(reference.get('ref') == 'refs/heads/'+channel and isinstance(obj,dict) and
            obj.get('type') == 'commit', 'Invalid installer branch reference')
    source = obj.get('sha')
    require(isinstance(source,str) and re.fullmatch(r'[0-9a-f]{40}',source), 'Invalid installer source identity')
    return source


def published(client, channel):
    # Bootstrap only from successful canonical CI, newest first. A pending or
    # incompatible branch tip must not hide an older verified compatible core.
    require(channel in ('main', 'devel'), 'Unsupported installer channel')
    head = branch_head(client,channel)
    workflow = metadata(client,'actions/workflows/software-build.yml')
    require(type(workflow.get('id')) is int and workflow['id'] > 0 and
            workflow.get('path') == '.github/workflows/software-build.yml', 'Unexpected installer verification workflow')
    seen = set()
    # Bound metadata requests and anonymous API use to thirty recent runs.
    for page in range(1,4):
        runs = metadata(client,'actions/workflows/software-build.yml/runs?branch='+channel+
                        '&status=success&per_page=10&page='+str(page))['workflow_runs']
        require(isinstance(runs,list) and len(runs) <= 10 and all(isinstance(r,dict) for r in runs),
                'Invalid installer verification runs')
        candidates = [r for r in runs if r.get('head_branch') == channel and
                      isinstance(r.get('head_sha'),str) and re.fullmatch(r'[0-9a-f]{40}',r['head_sha']) and
                      type(r.get('id')) is int and r['id'] > 0 and type(r.get('run_attempt')) is int and r['run_attempt'] > 0 and
                      r.get('event') in ('push','workflow_dispatch')]
        for selected in sorted(candidates,key=lambda r:(r['id'],r['run_attempt']),reverse=True):
            if selected['head_sha'] in seen:
                continue
            seen.add(selected['head_sha'])
            try:
                return verified_source(client,channel,workflow,selected,head)
            except Incompatible:
                continue
        if len(runs) < 10:
            break
    raise Unavailable('No verified compatible Pico USB installer is available in recent '+channel+
                      ' CI runs; retry after compatible software verification completes')


def verified_source(client, channel, workflow, selected, head):
    source = selected['head_sha']
    run, attempt = selected['id'], selected['run_attempt']
    info = metadata(client,f'actions/runs/{run}/attempts/{attempt}')
    require(info.get('id') == run and info.get('head_sha') == source and info.get('head_branch') == channel and
            info.get('path') == workflow['path'] and info.get('workflow_id') == workflow['id'] and
            isinstance(info.get('repository'),dict) and info['repository'].get('full_name') == REPOSITORY and
            isinstance(info.get('head_repository'),dict) and info['head_repository'].get('full_name') == REPOSITORY and
            info.get('run_attempt') == attempt and info.get('event') in ('push','workflow_dispatch') and
            info.get('status') == 'completed' and info.get('conclusion') == 'success',
            'Installer CI has not completed successfully on canonical source')
    result = metadata(client,f'actions/runs/{run}/attempts/{attempt}/jobs?per_page=100')
    jobs = result['jobs']
    require(isinstance(jobs,list) and all(isinstance(j,dict) for j in jobs) and result.get('total_count') == len(jobs) and len(jobs) == 2 and
            {j.get('name') for j in jobs} == {'verify','publish'}, 'Unexpected installer verification jobs')
    job = next(j for j in jobs if j['name'] == 'verify')
    require(all(j.get('run_id') == run and j.get('head_sha') == source and j.get('run_attempt') == attempt and
                j.get('status') == 'completed' and j.get('conclusion') in ('success','skipped') for j in jobs) and
            job.get('conclusion') == 'success' and type(job.get('id')) is int and job['id'] > 0,
            'Installer source has no successful verification job')
    if source != head:
        # GitHub omits changed files after the first comparison page. Keep
        # ancestry metadata bounded even when pending commits have large diffs.
        history = metadata(client,f'compare/{source}...{head}?per_page=1&page=2')
        require(isinstance(history.get('base_commit'),dict) and history['base_commit'].get('sha') == source and
                history.get('status') in ('ahead','behind','identical','diverged') and
                isinstance(history.get('merge_base_commit'),dict) and
                isinstance(history['merge_base_commit'].get('sha'),str) and
                re.fullmatch(r'[0-9a-f]{40}',history['merge_base_commit']['sha']), 'Invalid installer branch ancestry')
        if history['status'] not in ('ahead','identical') or history['merge_base_commit'].get('sha') != source:
            raise Incompatible('Installer source is outside the selected branch history')
    payload = {'usb_installer.py':source_file(client,'scripts/usb_installer.py',source)}
    try:
        tree = ast.parse(payload['usb_installer.py'])
        compile(tree, '<verified-pico-installer>', 'exec')
    except SyntaxError as error:
        raise Incompatible('Installer source needs an unsupported Python version') from error
    try:
        versions = [ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and
                    any(isinstance(t,ast.Name) and t.id == 'INSTALLER_VERSION' for t in n.targets)]
    except (ValueError,TypeError) as error:
        raise Incompatible('Installer source has an incompatible version contract') from error
    if len(versions) != 1 or not isinstance(versions[0],str) or not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+',versions[0]):
        raise Incompatible('Installer source has an incompatible version contract')
    minimum = (3,10)
    for node in tree.body:
        test = node.test if isinstance(node,ast.If) else None
        if (isinstance(test,ast.Compare) and isinstance(test.left,ast.Attribute) and
                isinstance(test.left.value,ast.Name) and test.left.value.id == 'sys' and test.left.attr == 'version_info' and
                len(test.ops) == 1 and isinstance(test.ops[0],ast.Lt) and
                any(isinstance(statement,ast.Raise) for statement in node.body)):
            required = ast.literal_eval(test.comparators[0])
            if isinstance(required,tuple) and len(required) == 2 and all(type(n) is int for n in required):
                minimum = max(minimum,required)
    if minimum > sys.version_info[:2]:
        raise Incompatible('Installer source needs an unsupported Python version')
    constants = {n.value for n in ast.walk(tree) if isinstance(n,ast.Constant) and isinstance(n.value,str)}
    manifest_version = firmware_contract(tree)
    schema_name = f'release-manifest-{manifest_version}.schema.json'
    payload[schema_name] = source_file(client,'scripts/ci/'+schema_name,source)
    schema = decode(payload[schema_name])
    mains = [n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name == 'main']
    if not (isinstance(schema,dict) and {'wsprrypico-usb-result/1','wsprrypico-usb-transaction/1'} <= constants and
            schema.get('$id') == 'https://github.com/'+REPOSITORY+'/firmware-release-manifest/'+str(manifest_version) and
            len(mains) == 1 and 'schema_bytes' in {a.arg for a in (*mains[0].args.args,*mains[0].args.kwonlyargs)}):
        raise Incompatible('Installer source has an incompatible contract')
    manifest = validate({'schema':MARKER if manifest_version==1 else 'wsprrypico-usb-installer/2','repository':REPOSITORY,'contract':manifest_version,'channel':channel,
        'source':source,'run_id':run,'attempt':attempt,'workflow':workflow['path'],
        'delivery':'verified-source','verify_job':{k:job[k] for k in ('id','name','status','conclusion')},
        'installer_version':versions[0],'python_min':list(minimum),
        'result_schema':'wsprrypico-usb-result/1','transaction_schema':'wsprrypico-usb-transaction/1',
        'firmware_manifest_schema':manifest_version,
        'files':{n:{'sha256':sha(d),'bytes':len(d)} for n,d in payload.items()}},channel)
    return manifest, payload


class Store:
    def __init__(self, root, policy, owner=True):
        require(type(policy.get('contract')) is int and policy == {'schema':'wsprrypi-pico-usb-policy/1','contract':1,'channel':policy.get('channel')} and
                policy['channel'] in ('main','devel'), 'Unsupported installer update policy')
        self.root, self.channel, self.owner = root, policy['channel'], owner

    def read(self, path, limit=MAX_FILE):
        safe(path, self.owner)
        require(path.is_file() and path.stat().st_size <= limit, 'Installer snapshot is missing or oversized')
        return path.read_bytes()

    def bundle(self, directory):
        receipt = self.read(directory/'usb-installer.json')
        manifest = decode(receipt)
        validate(manifest, manifest.get('channel'))
        payload = {}
        for name, item in manifest['files'].items():
            data = self.read(directory/name)
            require(len(data) == item['bytes'] and sha(data) == item['sha256'], 'Retained installer checksum changed')
            payload[name] = data
        return manifest, payload

    def save(self, manifest, payload):
        validate(manifest, manifest.get('channel'))
        receipt = (json.dumps(manifest,indent=2,sort_keys=True)+'\n').encode()
        identity = sha(receipt)
        directory = self.root/'bundles'/identity
        if not directory.exists():
            for name, item in manifest['files'].items():
                require(len(payload[name]) == item['bytes'] and sha(payload[name]) == item['sha256'], 'Installer payload checksum mismatch')
            directory.mkdir(parents=True, mode=0o700)
            for name,data in {**payload,'usb-installer.json':receipt}.items():
                atomic(directory/name, data)
        self.bundle(directory)
        return identity

    def load(self, identity):
        require(isinstance(identity,str) and re.fullmatch(HEX,identity), 'Invalid retained installer identity')
        manifest,payload = self.bundle(self.root/'bundles'/identity)
        require(sha(self.read(self.root/'bundles'/identity/'usb-installer.json')) == identity, 'Installer receipt identity changed')
        return manifest,payload

    def active(self):
        path = self.root/'active.json'
        return decode(self.read(path,65536)) if path.exists() else None

    def refresh(self, client=None, force=False):
        current = self.active()
        if current:
            self.load(current['identity'])
            previous,_ = self.load(current['identity'])
            if not force and previous['channel'] == self.channel and time.time() < current['check_after']:
                return current
        try:
            manifest,payload = published(client or Network(),self.channel)
            if current:
                previous,_ = self.load(current['identity'])
                # Different workflows can verify identical source in a different
                # run order. Migrating its receipt must preserve old plans.
                same_content = manifest['source'] == previous['source'] and manifest['files'] == previous['files']
                require(previous['channel'] != self.channel or same_content or
                        (manifest['run_id'],manifest['attempt']) >= (previous.get('run_id',0),previous.get('attempt',0)),
                        'Installer channel attempted to replace a newer verified version')
            identity = self.save(manifest,payload)
            result = {'identity':identity,'check_after':int(time.time())+600,'update':{'state':'current'}}
        except (ValueError, KeyError, TypeError, OSError, SyntaxError) as error:
            if not current:
                raise
            details = getattr(error,'details',{})
            reason = str(error) if isinstance(error,Unavailable) else 'Installer update metadata could not be verified.'
            result = {'identity':current['identity'],'check_after':details.get('retry_at',int(time.time())+60),
                      'update':{'state':'retained','reason':reason,**{k:v for k,v in details.items() if k in ('error_code','retry_at')}}}
        write_json(self.root/'active.json',result)
        return result

    def bind(self, plan, identity):
        require(re.fullmatch(PLAN,plan), 'Invalid firmware plan identity')
        self.load(identity)
        write_json(self.root/'plans'/(plan+'.json'),{'identity':identity})

    def planned(self, plan):
        require(re.fullmatch(PLAN,plan), 'Invalid firmware plan identity')
        identity = decode(self.read(self.root/'plans'/(plan+'.json'),4096))['identity']
        self.load(identity)
        return identity

    def snapshot(self, state, identity):
        manifest,payload = self.load(identity)
        directory = state/'managed'/identity
        if not directory.exists():
            directory.mkdir(parents=True, mode=0o700)
            for name,data in payload.items():
                atomic(directory/name,data)
            atomic(directory/'usb-installer.json',self.read(self.root/'bundles'/identity/'usb-installer.json'))
        actual,_ = self.bundle(directory)
        require(actual == manifest and sha(self.read(directory/'usb-installer.json')) == identity, 'Transaction installer snapshot changed')
        # Preserve the existing same-board local-build references when the
        # installer version changes. The core still validates their identity.
        self.local_references(state,directory)
        for parent in (directory,directory.parent,state):
            fd = os.open(parent,os.O_RDONLY)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
        return directory

    def local_references(self, source, destination):
        refs = list(source.glob('.local-reference-*.json'))
        require(len(refs) <= 32, 'Local image reference limit reached')
        for reference in refs:
            match = re.fullmatch(r'\.local-reference-([0-9A-F]{16})\.json',reference.name)
            require(match is not None, 'Unknown local image reference')
            data = self.read(reference,65536)
            value = decode(data)
            require(value.get('schema') == 'wsprrypico-local-reference/1' and value.get('serial') == match[1],
                    'Local image reference identity changed')
            image = source/('.local-image-'+match[1]+'.uf2')
            content = self.read(image,4*1024*1024)
            require(value.get('uf2_sha256') == sha(content), 'Local image reference checksum changed')
            existing = list(destination.glob('.local-reference-*.json'))
            require((destination/reference.name).exists() or len(existing) < 32, 'Local image reference limit reached')
            if (destination/reference.name).exists() and (destination/reference.name).stat().st_mtime > reference.stat().st_mtime:
                continue
            atomic(destination/image.name,content)
            atomic(destination/reference.name,data)

    def recovery(self, transaction, state):
        require(transaction.name == 'transaction.json' and transaction.parent.parent.is_dir(), 'Select a retained firmware transaction')
        self.read(transaction,65536)
        parent = transaction.parent.parent
        if parent == state:
            identity = decode(self.read(self.root/'legacy.json',4096))['identity']
            return self.load(identity)
        require(parent.parent == state/'managed' and re.fullmatch(HEX,parent.name), 'Recovery is outside managed private storage')
        require(sha(self.read(parent/'usb-installer.json')) == parent.name, 'Recovery installer receipt changed')
        return self.bundle(parent)

    def prune(self, cache, state, *, retain_history=False):
        # One global firmware budget across all installer versions, not one
        # independent allowance for each release. Recovery is never evicted.
        safe(cache,self.owner)
        plans = sorted((p for p in cache.iterdir() if re.fullmatch(PLAN,p.name)),key=lambda p:p.stat().st_mtime,reverse=True)
        retained = {p.name for p in plans[:3]}
        for path in ([] if retain_history else plans[3:]):
            safe(path,self.owner)
            require(path.is_dir(), 'Unexpected firmware cache member')
            shutil.rmtree(path)
        bindings = self.root/'plans'
        if bindings.exists():
            for path in bindings.iterdir():
                require(re.fullmatch(PLAN+r'\.json',path.name), 'Unknown installer plan binding')
                if not retain_history and path.stem not in retained:
                    safe(path,self.owner)
                    path.unlink()
        records = []
        for directory in [state, *sorted((state/'managed').glob('*'))]:
            safe(directory,self.owner)
            if not directory.exists():
                continue
            for child in directory.iterdir():
                if directory == state and child.name == 'managed':
                    continue
                if child.is_dir() or child.is_symlink():
                    safe(child,self.owner)
                    require((child/'transaction.json').is_file(), 'Unknown private transaction directory; inspect it before another update')
            if directory != state and not retain_history:
                self.local_references(directory,state)
                for reference in directory.glob('.local-reference-*.json'):
                    (directory/reference.name.replace('.local-reference-','.local-image-').replace('.json','.uf2')).unlink()
                    reference.unlink()
            for path in directory.glob('*/transaction.json'):
                value = decode(self.read(path,65536))
                require(value.get('schema') == 'wsprrypico-usb-transaction/1' and
                        re.fullmatch(r'[0-9A-F]{16}',value.get('serial','')) and
                        path.parent.name.startswith(value['serial']+'-') and type(value.get('recovery_required')) is bool,
                        'Unknown private firmware transaction')
                records.append((path.parent,value))
        complete, unchanged, pending = [],[],[]
        for path,record in records:
            (pending if record['recovery_required'] else complete if record.get('stage') == 'complete' else unchanged).append(path)
        if retain_history:
            return len(pending)
        for history,limit in ((complete,2),(unchanged,8)):
            for i,path in enumerate(sorted(history,key=lambda p:p.stat().st_mtime,reverse=True)):
                if i >= limit or time.time()-path.stat().st_mtime > 7*86400:
                    shutil.rmtree(path)
        keep = {self.active()['identity']} if self.active() else set()
        if (self.root/'legacy.json').exists():
            keep.add(decode(self.read(self.root/'legacy.json',4096))['identity'])
        for path in bindings.glob('*.json'):
            keep.add(decode(self.read(path,4096))['identity'])
        bundles = self.root/'bundles'
        safe(bundles,self.owner)
        for path in (bundles.iterdir() if bundles.exists() else ()):
            safe(path,self.owner)
            require(re.fullmatch(HEX,path.name) and path.is_dir(), 'Unknown installer cache member')
            if path.name not in keep:
                shutil.rmtree(path)
        for directory in (state/'managed').glob('*'):
            if not any(directory.glob('*/transaction.json')):
                safe(directory,self.owner)
                # Only the three recognized immutable snapshot members remain.
                snapshot=validate(decode((directory/'usb-installer.json').read_bytes()),self.channel)
                require({p.name for p in directory.iterdir()} == set(snapshot['files']) | {'usb-installer.json'}, 'Unknown retained installer data')
                shutil.rmtree(directory)
        return len(pending)
