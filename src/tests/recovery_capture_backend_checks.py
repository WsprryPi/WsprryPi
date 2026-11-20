# SPDX-License-Identifier: MIT
"""Actual owner/IPC recovery capture checks, with synthetic private state."""
import json
from pathlib import Path
import sqlite3
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from application_recovery import archive


def exercise(Connection, runtime, ledger, state, identity, boot, helper):
    root = Path(str(ledger) + '.maintenance')
    root.mkdir(mode=0o700)
    jobs = root / 'jobs'
    jobs.mkdir(mode=0o700)

    def admitted(job_id, selected_boot=boot):
        job = jobs / job_id
        job.mkdir(mode=0o700)
        admission = job / 'capture-admission.json'
        admission.write_text(json.dumps({'schema': 'recovery-capture-admission/1', 'job_id': job_id,
                                         'backend_boot': selected_boot, 'identity': identity}))
        admission.chmod(0o600)
        return job

    def capture(job_id):
        connection = Connection(runtime, 'control')
        try:
            return connection.request('recovery.capture', {'job_id': job_id})
        finally:
            connection.close()

    assert capture('../not-a-job') == {'ok': False, 'error': 'invalid_request'}
    stale = admitted('b' * 32, '0' * 32)
    assert capture(stale.name) == {'ok': False, 'error': 'invalid_request'}
    assert not (stale / 'snapshot').exists()
    source = state / 'generation-1/state.sqlite3'
    with sqlite3.connect(source.as_uri() + '?mode=ro', uri=True) as database:
        before = database.execute('SELECT kind,payload FROM records ORDER BY kind').fetchall()
    # Real background owner checkpoints may briefly hold their try-locks.
    # Each explicit fixture retry uses a fresh admitted job; never replay a
    # capture with an uncertain or terminal result.
    for attempt in range(16):
        job = admitted(f'{attempt + 1:032x}')
        result = capture(job.name)
        if result == {'ok': True, 'value': {'job_id': job.name, 'state': 'ready'}}:
            break
        refused = json.loads((job / 'capture-result.json').read_text())
        assert refused['state'] == 'failed' and refused['error'] == 'busy', (result, refused)
        time.sleep(0.02)
    assert result == {'ok': True, 'value': {'job_id': job.name, 'state': 'ready'}}, (result, (job / 'capture-result.json').read_text())
    proof_file = job / 'capture-result.json'
    original = proof_file.read_bytes()
    proof = json.loads(original)
    inventory = proof['inventory']
    assert inventory['schema'] == 'recovery-snapshot/1' and inventory['source']['identity'] == identity
    names = set(inventory['files'])
    assert {'application/state.sqlite3', 'application/authority.json', 'pairing/selector.json',
            'pairing/authority.json', 'pairing/peers.json', 'pairing/operations.json',
            'retirement/authority.json', 'retirement/established.json'} <= names
    assert not any('source-' in name or '.ini' in name or 'wal' in name or 'shm' in name for name in names)
    assert str(root).encode() not in original
    assert capture(job.name) == result and proof_file.read_bytes() == original
    with sqlite3.connect((job / 'snapshot/application/state.sqlite3').as_uri() + '?mode=ro', uri=True) as database:
        assert database.execute('SELECT kind,payload FROM records ORDER BY kind').fetchall() == before
    manifest = archive.manifest_for_snapshot(job / 'snapshot', inventory['source'], inventory['files'])
    encrypted = job / 'backup.age'
    phrase = b'synthetic actual-owner recovery passphrase'
    archive.create_archive(helper, job / 'snapshot', encrypted, manifest, phrase)
    decoded = job / 'verified'
    decoded.mkdir(mode=0o700)
    assert archive.validate_archive(helper, encrypted, decoded, phrase) == manifest
    with sqlite3.connect(source.as_uri() + '?mode=ro', uri=True) as database:
        assert database.execute('SELECT kind,payload FROM records ORDER BY kind').fetchall() == before
    print('PASS actual owner capture: SQL, pairing/key history, retirement fences, bounded encrypted archive, stale boot refusal, no INI or source mutation')

    def busy():
        job = admitted('c' * 32)
        assert capture(job.name) == {'ok': True, 'value': {'job_id': job.name, 'state': 'failed'}}
        proof = json.loads((job / 'capture-result.json').read_text())
        assert proof['error'] == 'busy' and proof['inventory'] is None
        assert list((job / 'snapshot').iterdir()) == []
        print('PASS active Test Tone refuses capture without stopping or replacing its output reservation')
    return busy
