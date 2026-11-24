# SPDX-License-Identifier: MIT
"""Bounded inactive reconciliation evidence, never loaded as settings or work.

Restore retains source records separately from destination authority. Existing
validated evidence groups are copied unchanged and deduplicated by content; no
receipt, key, profile, or job becomes active merely by appearing here.
"""
import hashlib
import os
from pathlib import Path
import re
import sys
from . import archive
from .durable import canonical, publish_record, read_record, sync_directory


def _make_directory(path, *, exist_ok=False):
    if sys.platform == 'win32':
        from . import native_files
        return native_files.make_directory(path, exist_ok=exist_ok)
    Path(path).mkdir(mode=0o700, exist_ok=exist_ok)
    return archive.private_directory(path)

STATE_NAMES = {'state-' + name + '.json' for name in ('configuration', 'managed-settings', 'operations', 'catalog',
               'recurrence-authority', 'recurrence-initialization', 'initialization', 'recurrence-recovery', 'selector')}
SMALL_NAMES = STATE_NAMES | {'pair-' + name + '.json' for name in ('selector', 'authority', 'operations', 'peers')}
SMALL_NAMES |= {'retirement-authority.json', 'retirement-established.json', 'fleet-assignments.json', 'fleet-contexts.json'}


def maximum(name):
    if name in SMALL_NAMES or re.fullmatch(r'c-[0-9a-f]{32}-(ca|crt|key)', name):
        return archive.MAX_MANIFEST
    if name == 'progress.json':
        return 16384
    if re.fullmatch(r'target-[0-9a-f]{32}\.json', name):
        return 16 * 1024 * 1024
    raise archive.RecoveryError('Unsupported inactive recovery evidence')


def content_digest(index):
    return hashlib.sha256(canonical({key: index[key] for key in ('schema', 'source', 'files')})).hexdigest()


def validate_index(value, group_id):
    archive.closed(value, ('schema', 'source', 'created_utc', 'archive_sha256', 'content_digest', 'files'))
    archive.require(value['schema'] == 'recovery-evidence/1' and type(value['source']) is dict)
    source = value['source']
    archive.closed(source, ('identity', 'role', 'application_version', 'storage', 'platform'))
    archive.require(type(source['identity']) is str and archive.ID.fullmatch(source['identity']) and source['role'] in ('pi', 'controller')
                    and source['storage'] == 'application-state/1' and (source['platform'] in ('linux', 'darwin') or source['platform'] == 'windows' and source['role'] == 'controller')
                    and type(source['application_version']) is str and 0 < len(source['application_version']) <= 128
                    and type(value['created_utc']) is int and 0 < value['created_utc'] < 2**63
                    and type(value['archive_sha256']) is str and archive.HASH.fullmatch(value['archive_sha256'])
                    and type(value['content_digest']) is str and archive.HASH.fullmatch(value['content_digest'])
                    and type(value['files']) is list and 0 < len(value['files']) < archive.MAX_FILES)
    names, total = [], 0
    for item in value['files']:
        archive.closed(item, ('name', 'size', 'sha256'))
        archive.require(type(item['name']) is str and type(item['size']) is int and 0 <= item['size'] <= maximum(item['name'])
                        and type(item['sha256']) is str and archive.HASH.fullmatch(item['sha256']))
        names.append(item['name'])
        total += item['size']
    archive.require(names == sorted(set(names)) and total <= archive.MAX_PLAINTEXT and value['content_digest'] == content_digest(value)
                    and type(group_id) is str and archive.ID.fullmatch(group_id) and group_id == value['content_digest'][:32])


def inspect(directory):
    """Validate a private optional evidence namespace with streaming payloads."""
    directory = Path(directory)
    if not directory.exists():
        return []
    archive.private_directory(directory)
    result, count, total = [], 0, 0
    for group in sorted(directory.iterdir()):
        archive.require(archive.ID.fullmatch(group.name))
        archive.private_directory(group)
        index, _ = read_record(group / 'index.json')
        validate_index(index, group.name)
        expected = {'index.json'} | {item['name'] for item in index['files']}
        archive.require({path.name for path in group.iterdir()} == expected)
        for item in index['files']:
            with archive.private_input(group / item['name'], maximum(item['name'])) as (stream, info):
                archive.require(info.st_size == item['size'] and archive.file_digest(stream) == item['sha256']
                                and archive.observation(archive.stat_descriptor(stream.fileno())) == archive.observation(info))
            total += item['size']
            archive.require(total <= archive.MAX_PLAINTEXT)
        count += len(expected)
        archive.require(count <= archive.MAX_FILES and total <= archive.MAX_PLAINTEXT)
        result.append((group, index))
    return result


def copy_file(source, destination, item):
    with archive.private_input(source, maximum(item['name'])) as (stream, info):
        archive.require(info.st_size == item['size'])
        digest = hashlib.sha256()
        total = 0
        created = False
        try:
            with archive.private_output(destination, item['size']) as output:
                created = True
                for data in iter(lambda: stream.read(archive.CHUNK), b''):
                    total += len(data)
                    archive.require(total <= item['size'])
                    digest.update(data)
                    output.write(data)
                output.flush()
                os.fsync(output.fileno())
            archive.require(total == item['size'] and digest.hexdigest() == item['sha256'] and archive.observation(archive.stat_descriptor(stream.fileno())) == archive.observation(info))
        except BaseException:
            if created:
                Path(destination).unlink(missing_ok=True)
            raise


def copy_groups(source, destination):
    groups = inspect(source)
    destination = Path(destination)
    if not groups:
        return
    _make_directory(destination, exist_ok=True)
    archive.private_directory(destination)
    for group, index in groups:
        target = destination / group.name
        if target.exists():
            existing = dict(inspect(destination))[target]
            # Creation/archive provenance may differ for identical records;
            # retain the first validated copy without dropping any payload.
            archive.require(existing['content_digest'] == index['content_digest'])
            continue
        _make_directory(target)
        for item in index['files']:
            copy_file(group / item['name'], target / item['name'], item)
        publish_record(target / 'index.json', index, None)
        sync_directory(target)
        sync_directory(destination)
    inspect(destination)


def retain_source(candidate, validated, records, manifest, archive_sha256):
    archive.validate_manifest(manifest)
    archive.require(type(archive_sha256) is str and archive.HASH.fullmatch(archive_sha256))
    candidate, validated, records = map(archive.private_directory, (candidate, validated, records))
    definition, _ = read_record(records / 'index.json')
    archive.closed(definition, ('schema', 'identity', 'role', 'files'))
    archive.require(definition['schema'] == 'recovery-source-records/1' and definition['identity'] == manifest['source']['identity']
                    and definition['role'] == manifest['source']['role'] and type(definition['files']) is list)
    sources, files = {}, []
    for item in definition['files']:
        archive.closed(item, ('name', 'size', 'sha256'))
        archive.require(item['name'] in STATE_NAMES and item['name'] != 'state-selector.json' and item['name'] not in sources)
        sources[item['name']] = records / item['name']
        files.append(item)
    archive.require({'state-configuration.json', 'state-catalog.json', 'state-initialization.json'} <= sources.keys())
    fixed = {'application/authority.json': 'state-selector.json', 'application/configuration.wtp-assignments.json': 'fleet-assignments.json',
             'application/configuration.wtp-fleet-contexts.json': 'fleet-contexts.json', 'application/configuration.recurrence-progress.json': 'progress.json'}
    for original in manifest['files']:
        name, retained = original['name'], None
        if name in fixed:
            retained = fixed[name]
        elif name.startswith('pairing/'):
            retained = 'pair-' + Path(name).name
        elif name.startswith('retirement/'):
            retained = 'retirement-' + Path(name).name
        elif name.startswith('application/configuration.management/'):
            retained = 'target-' + Path(name).name
        elif name.startswith('credentials/'):
            _, profile, key = name.split('/')
            retained = 'c-' + profile + '-' + {'ca.crt': 'ca', 'client.crt': 'crt', 'client.key': 'key'}[key]
        if retained is not None:
            maximum(retained)
            archive.require(retained not in sources)
            sources[retained] = validated / name
            files.append(dict(original, name=retained))
    index = {'schema': 'recovery-evidence/1', 'source': manifest['source'], 'created_utc': manifest['created_utc'],
             'archive_sha256': archive_sha256, 'content_digest': '', 'files': sorted(files, key=lambda item: item['name'])}
    index['content_digest'] = content_digest(index)
    group_id = index['content_digest'][:32]
    validate_index(index, group_id)
    destination = candidate / 'evidence'
    copy_groups(validated / 'application/evidence', destination)
    _make_directory(destination, exist_ok=True)
    archive.private_directory(destination)
    target = destination / group_id
    if not target.exists():
        _make_directory(target)
        for item in index['files']:
            copy_file(sources[item['name']], target / item['name'], item)
        publish_record(target / 'index.json', index, None)
        sync_directory(target)
        sync_directory(destination)
    inspect(destination)
    return {'group_id': group_id, 'source_identity': manifest['source']['identity'], 'files': len(files)}
