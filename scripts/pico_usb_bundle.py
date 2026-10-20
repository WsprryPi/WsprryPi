#!/usr/bin/env python3
"""Contract-1 installer distribution. No USB, firmware or RF authority."""
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
DOWNLOAD = 'https://github.com/' + REPOSITORY + '/releases/download/'
MARKER = 'wsprrypico-usb-installer/1'
FILES = {'usb_installer.py', 'release-manifest-1.schema.json'}
HEX = r'[0-9a-f]{64}'
PLAN = r'[0-9a-f]{32}'
MAX_FILE = 1024 * 1024


class Unavailable(ValueError):
    def __init__(self, message, **details):
        super().__init__(message)
        self.details = details


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
        require(url.startswith((API, DOWNLOAD)), 'Unexpected installer download origin')
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
                    raise Unavailable('A tested compatible installer channel is not published yet.', http_status=404) from error
                raise Unavailable(f'Installer update request unavailable (HTTP {error.code}).', http_status=error.code) from error
        except (OSError, urllib.error.URLError) as error:
            raise Unavailable('Installer update check could not connect to GitHub.') from error

    def api(self, path):
        return decode(self.get(API+path, 256*1024))


def validate(manifest, channel):
    require(isinstance(manifest, dict) and manifest.get('schema') == MARKER and type(manifest.get('contract')) is int and manifest['contract'] == 1 and
            manifest.get('repository') == REPOSITORY and manifest.get('channel') == channel and
            channel in ('main', 'devel') and re.fullmatch(r'[0-9a-f]{40}', manifest.get('source','')) and
            re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', manifest.get('installer_version','')) and
            manifest.get('result_schema') == 'wsprrypico-usb-result/1' and
            manifest.get('transaction_schema') == 'wsprrypico-usb-transaction/1' and
            type(manifest.get('firmware_manifest_schema')) is int and manifest['firmware_manifest_schema'] == 1,
            'Installer update has an incompatible contract')
    minimum = manifest.get('python_min')
    require(isinstance(minimum, list) and len(minimum) == 2 and all(type(n) is int and 0 <= n < 100 for n in minimum)
            and tuple(minimum) >= (3,10) and tuple(minimum) <= sys.version_info[:2], 'Installer needs an unsupported Python version')
    require(set(manifest.get('files',{})) == FILES, 'Installer file inventory differs from the supported contract')
    for item in manifest['files'].values():
        require(set(item) == {'sha256','bytes'} and re.fullmatch(HEX,item['sha256']) and
                type(item['bytes']) is int and 0 < item['bytes'] <= MAX_FILE, 'Invalid installer checksum inventory')
    return manifest


def published(client, channel):
    release = client.api('releases/tags/usb-installer-v1-'+channel)
    require(release['draft'] is False and release['prerelease'] is True and not release['assets'], 'Invalid installer channel release')
    body = release['body']
    require(isinstance(body,str) and len(body) <= 16000, 'Oversized installer channel metadata')
    records = re.findall(r'<!-- '+re.escape(MARKER)+r'\n(.*?)\n-->', body, re.S)
    require(len(records) == 1, 'Missing or ambiguous installer channel')
    index = decode(records[0])
    run, attempt = index.get('run_id'), index.get('attempt')
    require(index.get('schema') == MARKER and index.get('repository') == REPOSITORY and index.get('contract') == 1 and
            index.get('channel') == channel and type(run) is int and run > 0 and type(attempt) is int and attempt > 0 and
            re.fullmatch(r'[0-9a-f]{40}',index.get('source','')) and re.fullmatch(HEX,index.get('manifest_sha256','')) and
            index.get('tag') == f'usb-installer-v1-{channel}-{run}.{attempt}', 'Invalid installer publication identity')
    version = client.api('releases/tags/'+index['tag'])
    assets = version['assets']
    require(version['draft'] is False and version['prerelease'] is True and version['tag_name'] == index['tag'] and
            len(assets) == 3 and {a['name'] for a in assets} == FILES | {'usb-installer.json'}, 'Incomplete installer publication')
    base = DOWNLOAD+index['tag']+'/'
    payload = {}
    for asset in assets:
        name = asset['name']
        require(asset['state'] == 'uploaded' and asset['browser_download_url'] == base+name and
                type(asset['size']) is int and 0 < asset['size'] <= MAX_FILE, 'Invalid installer asset identity')
        data = client.get(base+name)
        require(len(data) == asset['size'] and asset['digest'] == 'sha256:'+sha(data), 'Installer release checksum mismatch')
        payload[name] = data
    require(sha(payload['usb-installer.json']) == index['manifest_sha256'], 'Installer manifest checksum mismatch')
    manifest = validate(decode(payload.pop('usb-installer.json')), channel)
    for name,item in manifest['files'].items():
        require(len(payload[name]) == item['bytes'] and sha(payload[name]) == item['sha256'],
                'Installer content differs from its verified manifest')
    require(all(manifest.get(key) == index[key] for key in ('source','run_id','attempt','tag')) and
            manifest.get('workflow') == '.github/workflows/usb-installer.yml', 'Installer provenance differs from its channel')
    job = manifest.get('verify_job',{})
    require(job.get('name') == 'verify' and job.get('status') == 'completed' and job.get('conclusion') == 'success' and
            type(job.get('id')) is int and job['id'] > 0, 'Installer publication has no successful verification receipt')
    ref = client.api('git/ref/tags/'+index['tag'])
    require(ref['object'].get('type') == 'commit' and ref['object'].get('sha') == manifest['source'],
            'Installer tag points to different source')
    try:
        run_info = client.api(f'actions/runs/{run}/attempts/{attempt}')
    except Unavailable as error:
        if error.details.get('http_status') not in (404,410):
            raise
        # The immutable public receipt remains available after Actions expiry.
    else:
        require(run_info['head_sha'] == manifest['source'] and run_info['head_branch'] == channel and
                run_info['path'] == manifest['workflow'] and run_info['run_attempt'] == attempt and
                run_info['event'] in ('push','workflow_dispatch') and run_info['status'] == 'completed' and
                run_info['conclusion'] == 'success', 'Installer CI has not completed successfully')
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
                require(previous['channel'] != self.channel or (manifest['run_id'],manifest['attempt']) >= (previous.get('run_id',0),previous.get('attempt',0)),
                        'Installer channel attempted to replace a newer verified version')
            identity = self.save(manifest,payload)
            result = {'identity':identity,'check_after':int(time.time())+600,'update':{'state':'current'}}
        except (ValueError, KeyError, TypeError, OSError) as error:
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
        for path in bundles.iterdir():
            safe(path,self.owner)
            require(re.fullmatch(HEX,path.name) and path.is_dir(), 'Unknown installer cache member')
            if path.name not in keep:
                shutil.rmtree(path)
        for directory in (state/'managed').glob('*'):
            if not any(directory.glob('*/transaction.json')):
                safe(directory,self.owner)
                # Only the three recognized immutable snapshot members remain.
                require({p.name for p in directory.iterdir()} == FILES | {'usb-installer.json'}, 'Unknown retained installer data')
                shutil.rmtree(directory)
        return len(pending)
