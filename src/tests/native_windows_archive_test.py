#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Actual shared archive engine, native file DLL, CPython, and Go helper."""
import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from application_recovery import archive, native_files


def refuses(action):
    try:
        action()
    except (OSError, ValueError, archive.RecoveryError):
        return
    raise AssertionError('Unsafe archive operation was accepted')


def main():
    assert sys.platform == 'win32' and sys.version_info[:2] == (3, 15)
    assert len(sys.argv) == 4
    native_files.configure(Path(sys.argv[1]))
    root = native_files.private_directory(Path(sys.argv[2]))
    helper = Path(sys.argv[3])
    snapshot = native_files.make_directory(root / 'snapshot-\u03a9')
    native_files.make_directory(snapshot / 'application')
    contents = {'application/state.sqlite3': bytes(range(256)) * 1024,
                'application/authority.json': b'{"synthetic":"private initialization"}'}
    for name, value in contents.items():
        with native_files.private_output(snapshot / name, len(value)) as (stream, _):
            stream.write(value)
    # Exercise native Windows controller provenance through the shared format.
    manifest = archive.manifest_for_snapshot(snapshot, {'identity': 'a' * 32, 'role': 'controller',
        'application_version': '4.0.0-test', 'storage': 'application-state/1', 'platform': 'windows'}, list(contents), 1800000000)
    phrase = b'synthetic native archive test passphrase'
    output = root / 'backup.age'
    archive.create_archive(helper, snapshot, output, manifest, phrase)
    with archive.private_input(output, archive.MAX_ARCHIVE) as (stream, _):
        encrypted = stream.read()
    assert contents['application/authority.json'] not in encrypted
    destination = native_files.make_directory(root / 'decoded')
    assert archive.validate_archive(helper, output, destination, phrase) == manifest
    for name, value in contents.items():
        with archive.private_input(destination / name, len(value)) as (stream, _):
            assert stream.read() == value
    retry_receipt = list(archive.observation(native_files.path_info(output)))
    wrong_first = native_files.make_directory(root / 'wrong-first')
    refuses(lambda: archive.validate_archive(helper, output, wrong_first, b'wrong phrase', retry_receipt))
    assert list(archive.observation(native_files.path_info(output))) == retry_receipt
    retry = native_files.make_directory(root / 'correct-retry')
    assert archive.validate_archive(helper, output, retry, phrase, retry_receipt) == manifest
    refuses(lambda: archive.create_archive(helper, snapshot, output, manifest, phrase))
    with archive.private_input(output, archive.MAX_ARCHIVE) as (stream, _):
        assert stream.read() == encrypted
    for index, (secret, data) in enumerate(((b'wrong phrase', encrypted), (phrase, encrypted[:-1]),
                                           (phrase, encrypted[:-1] + bytes([encrypted[-1] ^ 1])))):
        upload = root / f'invalid-{index}.age'
        with native_files.private_output(upload, len(data)) as (stream, _):
            stream.write(data)
        staging = native_files.make_directory(root / f'refused-{index}')
        refuses(lambda: archive.validate_archive(helper, upload, staging, secret))
    receipt = list(archive.observation(native_files.path_info(output)))
    replacement = root / 'replacement.age'
    archive.create_archive(helper, snapshot, replacement, manifest, phrase)
    os.replace(replacement, output)
    stale = native_files.make_directory(root / 'stale-receipt')
    refuses(lambda: archive.validate_archive(helper, output, stale, phrase, receipt))
    assert not list(stale.iterdir())
    authority = snapshot / 'application/authority.json'
    with native_files.NativeFile(authority, 3, archive.MAX_MANIFEST) as native:
        os.write(native.fd, b'changed after snapshot')
        native.sync()
    failed = root / 'changed.age'
    refuses(lambda: archive.create_archive(helper, snapshot, failed, manifest, phrase))
    assert not failed.exists()
    print('Shared archive/native DLL/CPython/Go round trip, binary streams, collision preservation, hostile ciphertext, stale upload receipt, and stale snapshot refusals passed; no owner relay, service, or full recovery qualification')


if __name__ == '__main__':
    main()
