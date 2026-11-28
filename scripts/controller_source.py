# SPDX-License-Identifier: MIT
"""Fingerprint the tracked and local controller build and web asset inputs."""

import hashlib
import os
from pathlib import Path
import subprocess


def fingerprint(root):
    scope = ('src', 'WsprryPi-UI/data', 'WsprryPi-UI/scripts/generate_ui_manifest.py',
             'scripts/controller_sources.mk', 'scripts/build_metadata_rules.mk',
             'scripts/generate_build_metadata.py', 'scripts/controller_source.py',
             'scripts/generate_backend_capabilities.py', 'scripts/pairing_provider.json',
             'scripts/build_recovery_age.py', 'scripts/recovery_age_provider.json')
    names = subprocess.check_output(['git', '-C', str(root), 'ls-files', '-z', '--cached',
                                     '--others', '--exclude-standard', '--', *scope], timeout=30)
    value = hashlib.sha256()
    for raw in sorted(set(names.split(b'\0')) - {b''}):
        path = root / Path(os.fsdecode(raw))
        if path.is_symlink():
            raise ValueError(f'Controller build input is a link: {path}')
        value.update(raw + b'\0')
        if path.exists():
            value.update(hashlib.sha256(path.read_bytes()).digest())
        value.update(b'\0')
    return value.hexdigest()
