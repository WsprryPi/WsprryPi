#!/usr/bin/env python3
"""Hardware-free regression for the live acceptance rollback harness."""
import importlib.util
import io
from pathlib import Path
import sys
import tarfile
import tempfile

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('routing_live', Path(__file__).with_name('live_target.py'))
live = importlib.util.module_from_spec(spec)
spec.loader.exec_module(live)

with tempfile.TemporaryDirectory(prefix='wtp-rollback-regression-') as directory:
    root = Path(directory)
    live.ROOT = root
    live.BINARY = root/'wsprrypi'
    live.BINARY.write_bytes(b'failed-candidate')
    (root/'candidate-installed').touch()
    with tarfile.open(root/'private-original.tar.gz', 'w:gz') as archive:
        member = tarfile.TarInfo('usr/local/bin/wsprrypi')
        original = b'original-qualified-binary'
        member.size = len(original)
        member.mode = 0o755
        archive.addfile(member, io.BytesIO(original))
    calls = []
    live.command = lambda *args, **kwargs: calls.append(args) or ''

    def unavailable_network():
        assert live.BINARY.read_bytes() == original
        assert not (root/'candidate-installed').exists()
        assert calls[-1] == ('systemctl', 'restart', 'wsprrypi')
        raise RuntimeError('listener still unavailable')

    live.restore_network = unavailable_network
    # An open handle keeps the former inode alive, as a running executable does.
    with live.BINARY.open('rb') as running_inode:
        try:
            live.rollback()
            raise AssertionError('injected recovery failure must remain visible')
        except RuntimeError as error:
            assert str(error) == 'listener still unavailable'
        assert running_inode.read() == b'failed-candidate'
    assert live.BINARY.read_bytes() == original
    assert live.BINARY.stat().st_mode & 0o777 == 0o755
    assert not live.BINARY.with_name(live.BINARY.name+'.routing-rollback').exists()

    # A later network-only retry must preserve the now-restored binary/service.
    calls.clear()
    live.restore_network = lambda: None
    assert live.rollback() == {'rolled_back': True}
    assert calls == [('systemctl', 'stop', 'wtp-routing-capture.service')]

print('Atomic binary rollback before listener recovery and network-only retry passed')
