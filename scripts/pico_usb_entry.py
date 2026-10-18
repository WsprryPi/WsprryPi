#!/usr/bin/env python3
"""Installed adapter for one pinned, checksum-verified WsprryPico core."""
import hashlib
import json
import os
from pathlib import Path
import stat
import sys

def main():
    root = Path(__file__).resolve().parent
    descriptor = json.loads((root / 'deployment.json').read_text())
    if descriptor['schema'] != 'wsprrypi-pico-usb-deployment/1':
        raise ValueError('Unsupported shared installer deployment')
    verified = {}
    if set(descriptor['files']) != {'scripts/usb_installer.py', 'scripts/ci/release-manifest-1.schema.json'}:
        raise ValueError('Incomplete shared installer inventory')
    for relative, digest in descriptor['files'].items():
        path = root / relative
        if relative not in ('scripts/usb_installer.py', 'scripts/ci/release-manifest-1.schema.json'):
            raise ValueError('Unexpected installer member')
        for ancestor in (path, *path.parents):
            info = ancestor.lstat()
            if stat.S_ISLNK(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
                raise ValueError('Shared installer must remain root-owned and protected')
        verified[relative] = path.read_bytes()
        if hashlib.sha256(verified[relative]).hexdigest() != digest:
            raise ValueError('Shared installer checksum changed; reinstall the Pi package')
    # Execute the verified source bytes directly; never reuse a timestamp-based
    # Python bytecode cache left by a prior package version.
    import types
    usb_installer = types.ModuleType('wsprrypico_shared_installer')
    usb_installer.__file__ = str(root / 'scripts/usb_installer.py')
    exec(compile(verified['scripts/usb_installer.py'], usb_installer.__file__, 'exec'), usb_installer.__dict__)
    if usb_installer.INSTALLER_VERSION != descriptor['installer_version']:
        raise ValueError('Shared installer version mismatch')
    # A click cannot accumulate indefinite anonymous download caches.
    if len(sys.argv) > 1 and sys.argv[1] == 'plan':
        cache = Path('/var/cache/wsprrypi/pico-usb')
        import shutil
        entries = sorted((p for p in cache.iterdir() if p.is_dir() and not p.is_symlink()),
                         key=lambda p: p.stat().st_mtime, reverse=True)
        for path in entries[3:]:
            if len(path.name) == 32 and all(c in '0123456789abcdef' for c in path.name):
                shutil.rmtree(path)
    return usb_installer.main(schema_bytes=verified['scripts/ci/release-manifest-1.schema.json'])

if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as error:
        print(json.dumps({'schema':'wsprrypico-usb-result/1','installer_version':'0.1.0',
                          'ok':False,'error':str(error),'recovery_required':False}))
        sys.exit(2)
