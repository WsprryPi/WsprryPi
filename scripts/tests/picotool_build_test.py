#!/usr/bin/env python3
"""Hardware-free release staging and build/export boundary regressions."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_picotool as builder
import picotool_install as tool


class BuildTest(unittest.TestCase):
    def test_existing_outputs_are_refused_before_docker(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)/'occupied'
            output.mkdir()
            (output/'keep').write_text('user export')
            with patch.object(builder, 'run') as run:
                with self.assertRaises(ValueError):
                    builder.build('armv6-bookworm', output)
                run.assert_not_called()
            self.assertEqual((output/'keep').read_text(), 'user export')

    def test_source_revision_mismatch_stops_build(self):
        with patch.object(builder, 'run') as run:
            run.return_value.stdout = '0' * 40
            with self.assertRaises(ValueError):
                builder.checkout('/unused', 'picotool', builder.PICOTOOL_REVISION)

    def test_packages_are_repeatable_and_contain_support_data(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            payload = root/'payload'
            payload.mkdir()
            (payload/'picotool').write_bytes(b'binary')
            (payload/'xip_ram_perms.elf').write_bytes(b'support')
            records = []
            for index in range(2):
                output = root/str(index)
                output.mkdir()
                records.append(builder.package(payload, output, 'armv6-bookworm'))
            self.assertEqual(records[0], records[1])
            self.assertIn('xip_ram_perms.elf', records[0]['files'])
            (payload/'bad-link').symlink_to(payload/'picotool')
            with self.assertRaises(ValueError):
                builder.package(payload, root/'0', 'armv6-bookworm')

    def test_staging_requires_all_four_unchanged_exports(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            exports, output = root/'exports', root/'release'
            payload = root/'payload'
            payload.mkdir()
            for name in ('picotool', 'enc_bootloader.elf', 'xip_ram_perms.elf', 'rp2350_otp_contents.json'):
                (payload/name).write_text(name)
            for target in tool.TARGETS:
                directory = exports/target
                directory.mkdir(parents=True)
                builder.package(payload, directory, target)
            builder.stage_release(exports, output, 'v4.0.0-beta.1')
            self.assertEqual(len(list(output.iterdir())), 6)
            policy = json.loads((output/'picotool-manifest.json').read_text())
            self.assertEqual(set(policy['targets']), set(tool.TARGETS))
            for target in tool.TARGETS:
                tool.policy_record(policy, target)
            with self.assertRaises(ValueError):
                builder.stage_release(exports, output, 'v4.0.0-beta.1')
            corrupted = exports/'armv6-bookworm/picotool-armv6-bookworm.tar.gz'
            corrupted.write_bytes(b'changed')
            with self.assertRaises(ValueError):
                builder.stage_release(exports, root/'second-release', 'v4.0.0-beta.1')
            self.assertFalse((root/'second-release').exists())

    def test_unprivileged_no_network_validation_and_cleanup(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)/'export'
            calls = []
            def docker(*args, **kwargs):
                calls.append(args)
                if args[1] == 'buildx':
                    Path(args[args.index('--iidfile')+1]).write_text('sha256:' + 'a'*64)
                    self.assertEqual(args[args.index('--platform')+1], 'linux/arm64')
                    self.assertTrue(any(str(arg).startswith('BASE_IMAGE=debian:bookworm-slim@sha256:') for arg in args))
                elif args[1] == 'cp':
                    stage = Path(args[-1])
                    (stage/'elf-dynamic.txt').write_text('(NEEDED) Shared library: [libusb-1.0.so.0]\n')
                elif args[1] == 'run':
                    self.assertEqual(args[args.index('--network')+1], 'none')
                    self.assertIn('--read-only', args)
                    self.assertIn('65534:65534', args)
                    self.assertIn('no-new-privileges', args)
                    raise subprocess.TimeoutExpired(args, 120)
                return subprocess.CompletedProcess(args, 0, stdout='', stderr='')
            with patch.object(builder, 'run', side_effect=docker), patch.object(builder.subprocess, 'run') as cleanup:
                with self.assertRaises(subprocess.TimeoutExpired):
                    builder.build('aarch64-bookworm', output)
                self.assertEqual(cleanup.call_count, 1)
            self.assertFalse(output.exists())
            self.assertFalse(list(output.parent.glob('.picotool-export-*')))


if __name__ == '__main__':
    unittest.main()
