#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Prove edited caller-selected production sources are built, with fail-closed errors."""
import argparse
from pathlib import Path
import shutil
import select
import subprocess
import tempfile


def run(args, *, cwd=None, succeeds=True):
    result = subprocess.run(args, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
    if (result.returncode == 0) != succeeds:
        raise AssertionError(f"Unexpected result ({result.returncode}): {args}\n{result.stdout[-12000:]}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('pico_source', type=Path)
    parser.add_argument('mbedtls_source', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    selected = args.pico_source.resolve()
    with tempfile.TemporaryDirectory(prefix='wsprrypi-selected-source-') as tmp:
        base = Path(tmp)
        source, build = base / 'pico', base / 'network'
        run(['git', 'clone', '--local', '--no-hardlinks', str(selected), str(source)])
        # Overlay tracked and untracked current inputs, including uncommitted edits
        # and deletions; the selected source is never changed.
        paths = run(['git', '-C', str(selected), 'ls-files', '-z', '--cached', '--others', '--exclude-standard']).split('\0')
        for relative in filter(None, paths):
            original, target = selected / relative, source / relative
            if original.is_file():
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(original, target)
            elif target.is_file():
                target.unlink()
        endpoint = source / 'src/wtp/endpoint.cpp'
        original = endpoint.read_text()
        marker = 'STAGE1_EDITED_PICO_SOURCE_CONSUMED'
        endpoint.write_text(original + '\n#include <cstdio>\nnamespace { const bool source_probe = [] { std::fputs("' + marker + '\\n", stderr); return true; }(); }\n')
        assert 'src/wtp/endpoint.cpp' in run(['git', '-C', str(source), 'status', '--short'])
        reference = ['make', '-C', str(root / 'src/WTP-Client'), 'interop-test', f'PICO_SOURCE={source}', f'BUILD_DIR={base / "client"}']
        assert marker in run(reference)
        configure = ['cmake', '-S', str(root / 'src/tests/network'), '-B', str(build), f'-DPICO_SOURCE={source}', f'-DMBEDTLS_SOURCE={args.mbedtls_source.resolve()}']
        output = run(configure)
        assert str(source) in output
        run(['cmake', '--build', str(build), '--target', 'pico_tls_server', '-j4'])
        with tempfile.TemporaryFile(mode='w+') as errors:
            process = subprocess.Popen([str(build / 'pico_tls_server'), '0', 'a', 'settings'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=errors, text=True)
            try:
                assert select.select([process.stdout], [], [], 15)[0], 'fixture readiness deadline'
                assert process.stdout.readline().startswith('READY ')
            finally:
                process.terminate()
                process.wait(timeout=10)
            errors.seek(0)
            assert marker in errors.read(), 'network fixture must execute the selected edited source'
        # Neither a missing source nor an actual compiler failure is admitted.
        endpoint.unlink()
        run(configure, succeeds=False)
        endpoint.write_text(original + '\n#error STAGE1_INTENTIONAL_COMPILE_FAILURE\n')
        run(configure)
        failed = run(['cmake', '--build', str(build), '--target', 'pico_tls_server', '-j4'], succeeds=False)
        assert 'STAGE1_INTENTIONAL_COMPILE_FAILURE' in failed
        run(reference, succeeds=False)
    print('Edited Pico production source accepted and executed by client and network fixtures; missing source and compiler errors rejected')


if __name__ == '__main__':
    main()
