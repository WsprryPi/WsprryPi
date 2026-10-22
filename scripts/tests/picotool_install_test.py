#!/usr/bin/env python3
"""Hardware-free release-download, package-integrity and rollback coverage."""
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'scripts'))
import picotool_install as tool
import build_picotool as builder


def fixture(directory, target='armv6-bookworm'):
    payload, output = directory/'payload', directory/'export'
    payload.mkdir()
    output.mkdir()
    for name in ('picotool', 'enc_bootloader.elf', 'xip_ram_perms.elf', 'rp2350_otp_contents.json'):
        (payload/name).write_bytes(name.encode())
    (payload/'licenses').mkdir()
    (payload/'licenses/NOTICE').write_text('upstream notice')
    record = builder.package(payload, output, target)
    data = (output/record['asset']).read_bytes()
    policy = {'schema': 'wsprrypi-picotool-release/1', 'version': '2.3.1',
              'release_tag': 'v4.0.0-beta.1', 'targets': {
                  name: {**record, 'asset': 'picotool-' + name + '.tar.gz'} for name in tool.TARGETS}}
    return policy, data


class PicotoolTest(unittest.TestCase):
    @staticmethod
    def output(*arguments, **_kwargs):
        return ('picotool v2.3.1 (Linux, GNU, Release)\n' if arguments[1] == 'version'
                else ' '.join(tool.OPTIONS[arguments[2]]))

    def test_validate_only_uses_version_and_help(self):
        with patch.object(tool, 'run', side_effect=self.output) as run:
            tool.validate('/fixture/picotool')
        self.assertEqual([call.args[1:] for call in run.call_args_list],
                         [('version',), *(('help', name) for name in tool.OPTIONS)])

    def test_reject_incompatible_or_usbless_tool(self):
        for report in ('picotool v2.2.0 (Linux)', 'picotool v3.0.0 (Linux)',
                       'picotool v2.3.1 (Linux) without USB support', 'unknown'):
            with self.subTest(report=report), patch.object(tool, 'run', return_value=report):
                with self.assertRaises(ValueError):
                    tool.validate('picotool')

    def test_reject_missing_command_options(self):
        for operation in tool.OPTIONS:
            def output(*arguments, **kwargs):
                return '--serial-number' if arguments[1:] == ('help', operation) else self.output(*arguments)
            with self.subTest(operation=operation), patch.object(tool, 'run', side_effect=output):
                with self.assertRaises(ValueError):
                    tool.validate('picotool')

    def test_missing_broken_and_hung_installed_tool(self):
        with patch.object(tool.shutil, 'which', return_value=None):
            self.assertFalse(tool.existing_tool())
        for error in (OSError('missing library'), subprocess.TimeoutExpired('picotool', 30)):
            with patch.object(tool.shutil, 'which', return_value='/fixture/picotool'), \
                    patch.object(tool, 'validate', side_effect=error):
                self.assertFalse(tool.existing_tool())

    def test_compatible_tool_is_kept_without_network_or_host_detection(self):
        with patch.object(tool, 'existing_tool', return_value=True), \
                patch.object(tool, 'download') as fetch, patch.object(tool, 'host_target') as host:
            self.assertEqual(tool.install({}), 'reused')
        fetch.assert_not_called()
        host.assert_not_called()

    def test_all_four_targets_download_and_install_complete_support_files(self):
        for target in tool.TARGETS:
            with self.subTest(target=target), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                policy, data = fixture(root)
                urls = []
                def fetch(url, limit):
                    urls.append((url, limit))
                    return data
                with patch.object(tool, 'existing_tool', return_value=False), \
                        patch.object(tool, 'host_target', return_value=tuple(target.split('-'))), \
                        patch.object(tool, 'validate_elf') as elf, patch.object(tool, 'validate'):
                    self.assertEqual(tool.install(policy, root=root, fetch=fetch), 'installed')
                destination = root/tool.PREFIX.relative_to('/')
                self.assertTrue((root/'usr/local/bin/picotool').is_symlink())
                self.assertEqual((destination/'xip_ram_perms.elf').read_bytes(), b'xip_ram_perms.elf')
                self.assertEqual(elf.call_args.args[1], target)
                self.assertEqual(urls, [('https://github.com/WsprryPi/WsprryPi/releases/download/'
                                        'v4.0.0-beta.1/picotool-' + target + '.tar.gz', len(data))])

    def test_corrupt_download_never_executes_or_replaces_command(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            policy, data = fixture(root)
            for corrupted in (data[:-1], b'x' + data[1:]):
                with patch.object(tool, 'existing_tool', return_value=False), \
                        patch.object(tool, 'host_target', return_value=('armv6', 'bookworm')), \
                        patch.object(tool, 'validate') as validate:
                    with self.assertRaisesRegex(ValueError, 'SHA-256'):
                        tool.install(policy, root=root, fetch=lambda *_: corrupted)
                    validate.assert_not_called()
                self.assertFalse((root/'usr/local/bin/picotool').exists())

    def test_unsafe_archive_entries_and_incomplete_inventory_are_refused(self):
        for name, kind in (('../escape', tarfile.REGTYPE), ('/escape', tarfile.REGTYPE),
                           ('picotool', tarfile.SYMTYPE), ('unexpected', tarfile.REGTYPE)):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                policy, _data = fixture(root)
                buffer = io.BytesIO()
                with tarfile.open(fileobj=buffer, mode='w:gz') as archive:
                    info = tarfile.TarInfo(name)
                    info.type = kind
                    archive.addfile(info, io.BytesIO())
                data = buffer.getvalue()
                record = {**policy['targets']['armv6-bookworm'], 'bytes': len(data), 'sha256': tool.sha(data)}
                with self.assertRaises(ValueError):
                    tool.unpack(data, record, root/'unpacked')
                self.assertFalse((root/'escape').exists())

    def test_wrong_cpu_and_failed_final_validation_restore_previous_install(self):
        for fail_at in ('elf', 'final'):
            with self.subTest(fail_at=fail_at), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                policy, data = fixture(root)
                destination = root/tool.PREFIX.relative_to('/')
                destination.mkdir(parents=True)
                (destination/'installation.json').write_text('{"schema":"wsprrypi-picotool-installation/1"}')
                (destination/'picotool').write_text('previous-tool')
                command = root/'usr/local/bin/picotool'
                command.parent.mkdir(parents=True)
                command.write_text('previous-command')
                with patch.object(tool, 'existing_tool', return_value=False), \
                        patch.object(tool, 'host_target', return_value=('armv6', 'bookworm')), \
                        patch.object(tool, 'validate_elf', side_effect=ValueError('wrong CPU') if fail_at == 'elf' else None), \
                        patch.object(tool, 'validate', side_effect=[None, ValueError('final check failed')]):
                    with self.assertRaises(ValueError):
                        tool.install(policy, root=root, fetch=lambda *_: data)
                self.assertEqual(command.read_text(), 'previous-command')
                self.assertEqual((destination/'picotool').read_text(), 'previous-tool')

    def test_unknown_directory_and_symlink_are_preserved(self):
        for symlink in (False, True):
            with self.subTest(symlink=symlink), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                policy, data = fixture(root)
                destination = root/tool.PREFIX.relative_to('/')
                destination.parent.mkdir(parents=True)
                if symlink:
                    destination.symlink_to(root/'payload')
                else:
                    destination.mkdir()
                    (destination/'keep').write_text('custom')
                with patch.object(tool, 'existing_tool', return_value=False), \
                        patch.object(tool, 'host_target', return_value=('armv6', 'bookworm')):
                    with self.assertRaises(ValueError):
                        tool.install(policy, root=root, fetch=lambda *_: data)
                self.assertTrue(destination.is_symlink() if symlink else (destination/'keep').exists())

    def test_unsupported_host_and_download_failure_never_compile(self):
        for failure in (ValueError('unsupported host'), OSError('download unavailable')):
            with patch.object(tool, 'existing_tool', return_value=False), \
                    patch.object(tool, 'host_target', side_effect=failure), patch.object(tool, 'run') as run:
                with self.assertRaises((ValueError, OSError)):
                    tool.install({})
                run.assert_not_called()

    def test_download_refusal_never_executes_a_candidate(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            policy, _data = fixture(root)
            def refused(*_args):
                raise OSError('release asset unavailable')
            with patch.object(tool, 'existing_tool', return_value=False), \
                    patch.object(tool, 'host_target', return_value=('armv6', 'bookworm')), \
                    patch.object(tool, 'run') as run:
                with self.assertRaisesRegex(OSError, 'unavailable'):
                    tool.install(policy, root=root, fetch=refused)
                run.assert_not_called()
            self.assertFalse((root/'usr').exists())

    def test_install_is_readable_with_a_restrictive_umask(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            policy, data = fixture(root)
            previous = os.umask(0o077)
            try:
                with patch.object(tool, 'existing_tool', return_value=False), \
                        patch.object(tool, 'host_target', return_value=('armv6', 'bookworm')), \
                        patch.object(tool, 'validate_elf'), patch.object(tool, 'validate'):
                    tool.install(policy, root=root, fetch=lambda *_: data)
            finally:
                os.umask(previous)
            destination = root/tool.PREFIX.relative_to('/')
            self.assertEqual(destination.stat().st_mode & 0o777, 0o755)
            self.assertEqual((destination/'licenses').stat().st_mode & 0o777, 0o755)
            self.assertEqual((destination/'picotool').stat().st_mode & 0o777, 0o755)


class InstallerTest(unittest.TestCase):
    def run_shell(self, **settings):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root/'scripts').mkdir()
            (root/'scripts/picotool_install.py').write_text('''import os, sys
from pathlib import Path
assert sys.argv[1:] == ['--policy', str(Path(os.environ['LOCAL_REPO_DIR'])/'config/picotool-release.json')]
Path(os.environ['EVENTS']).write_text('download-helper\\n')
sys.exit(int(os.environ['INSTALL_FAILURE']))
''')
            environment = {**os.environ, 'INSTALLER': str(ROOT/'scripts/install.sh'),
                           'LOCAL_REPO_DIR': str(root), 'EVENTS': str(root/'events'),
                           'ACTION': 'install', 'DRY_RUN': 'false', 'INSTALL_FAILURE': '0', **settings}
            result = subprocess.run(['bash', '-c', r'''
set -euo pipefail
source "$INSTALLER"
debug_start() { printf ''; }
debug_filter() { printf ' %q' "$@"; }
exec_command() {
    shift
    local arguments=() argument
    for argument in "$@"; do
        [[ -z "$argument" ]] || arguments+=("$argument")
    done
    [[ "$DRY_RUN" == true ]] || "${arguments[@]}"
}
apt-get() { return 99; }
cmake() { return 99; }
make() { return 99; }
ensure_picotool
'''], env=environment, text=True, capture_output=True)
            events = (root/'events').read_text() if (root/'events').exists() else ''
            return result, events

    def test_install_uses_only_download_helper(self):
        result, events = self.run_shell()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(events, 'download-helper\n')

    def test_dry_run_and_uninstall_never_invoke_helper(self):
        for settings in ({'DRY_RUN': 'true'}, {'ACTION': 'uninstall'}):
            result, events = self.run_shell(**settings)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(events, '')

    def test_download_failure_stops_installer(self):
        result, events = self.run_shell(INSTALL_FAILURE='1')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(events, 'download-helper\n')


if __name__ == '__main__':
    unittest.main()
