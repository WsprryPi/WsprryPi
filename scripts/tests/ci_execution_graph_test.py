#!/usr/bin/env python3
"""Execute source-identity refusals and the actual standalone Make dispatch rules."""
import collections
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import yaml
from fixture_commands import run_shell

ROOT = Path(__file__).resolve().parents[2]
SELECT = ROOT / 'scripts/ci/selected_source.py'
WORKFLOW = ROOT / '.github/workflows/debian-non-hardware.yml'
SPEC = importlib.util.spec_from_file_location('selected_source', SELECT)
SOURCE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SOURCE)


def reference(value):
    """Read one Actions context path, allowing whitespace/dot/bracket notation."""
    body = re.fullmatch(r'\s*\$\{\{\s*(.*?)\s*\}\}\s*', value)
    if body is None:
        raise AssertionError('expected one direct Actions output reference')
    text = body[1]
    first = re.match(r'[A-Za-z_][A-Za-z0-9_-]*', text)
    if first is None:
        raise AssertionError('missing Actions context')
    parts = [first[0]]
    rest = text[first.end():]
    while rest.strip():
        member = re.match(r"\s*(?:\.\s*([A-Za-z_][A-Za-z0-9_-]*)|\[\s*'([A-Za-z_][A-Za-z0-9_-]*)'\s*\])", rest)
        if member is None:
            raise AssertionError('fallbacks or computations cannot replace the selected source')
        parts.append(member[1] or member[2])
        rest = rest[member.end():]
    return tuple(parts)


def source_arguments(step):
    """Require actual helper execution, not an echo containing its operation."""
    words = shlex.split(step['run'], comments=True)
    if not words or words.pop(0) != 'python3':
        raise AssertionError('selected-source owner must execute Python')
    if words[:1] == ['-B']:
        words.pop(0)
    if not words or Path(words.pop(0)) != Path('scripts/ci/selected_source.py'):
        raise AssertionError('selected-source owner must execute the actual helper')
    return ['$PICO_SHA' if value == '${PICO_SHA}' else value for value in words]


def run(argv, cwd, watchdog=30, **environment):
    env = {k: v for k, v in os.environ.items() if not k.startswith('GIT_')}
    env.update(environment)
    # Resolve the trusted supervisor before applying a fixture's mocked PATH.
    source = 'export PATH=' + shlex.quote(env['PATH']) + '; exec ' + shlex.join(argv)
    env['PATH'] = os.environ['PATH']
    return run_shell(source, cwd=cwd, env=env,
                     stage='graph-fixture:' + Path(argv[0]).name, watchdog=watchdog)


def rule(source, target):
    lines = source.splitlines()
    start = next(i for i, line in enumerate(lines) if line.startswith(target + ':'))
    end = start + 1
    while end < len(lines) and lines[end].startswith('\t'):
        end += 1
    return '\n'.join(lines[start:end]) + '\n'


def make_goals(job):
    goals = []
    for step in job['steps']:
        for line in step.get('run', '').replace('\\\n', ' ').splitlines():
            words = shlex.split(line, comments=True)
            if words and words[0] == 'strace' and 'make' in words:
                words = words[words.index('make'):]
            if words and words[0] == 'make':
                goals += [word for word in words[1:] if (word.endswith('-test') or word == 'ci-build-budget') and '=' not in word]
    return collections.Counter(goals)


class OwnedGitTests(unittest.TestCase):
    def test_timeout_and_interruption_terminate_owned_group_and_bound_reaping(self):
        for primary in (subprocess.TimeoutExpired(['git'], 30), KeyboardInterrupt()):
            child = mock.Mock(pid=424242)
            child.communicate.side_effect = [primary, ('', '')]
            child.poll.return_value = None
            with self.subTest(primary=type(primary).__name__), mock.patch.object(SOURCE.subprocess, 'Popen', return_value=child) as launch, mock.patch.object(SOURCE.os, 'killpg') as kill:
                with self.assertRaises(ValueError if isinstance(primary, subprocess.TimeoutExpired) else KeyboardInterrupt) as caught:
                    SOURCE.git('status')
                if isinstance(primary, subprocess.TimeoutExpired):
                    self.assertIn('stage=source-git', str(caught.exception))
                    self.assertIn('hang_guard=30s', str(caught.exception))
                self.assertTrue(launch.call_args.kwargs['start_new_session'])
                kill.assert_called_once_with(child.pid, SOURCE.signal.SIGKILL)
                self.assertEqual(child.communicate.call_args_list, [mock.call(timeout=30), mock.call(timeout=2)])
                child.stdout.close.assert_called_once()
                child.stderr.close.assert_called_once()

    def test_primary_cleanup_and_pipe_close_failures_remain_visible(self):
        child = mock.Mock(pid=424242)
        child.communicate.side_effect = [subprocess.TimeoutExpired(['git'], 30), RuntimeError('reap failed')]
        child.poll.return_value = None
        child.stdout.close.side_effect = OSError('close failed')
        with mock.patch.object(SOURCE.subprocess, 'Popen', return_value=child), mock.patch.object(SOURCE.os, 'killpg', side_effect=PermissionError('kill failed')):
            with self.assertRaises(BaseExceptionGroup) as caught:
                SOURCE.git('status')
        errors = caught.exception.exceptions
        self.assertEqual(len(errors), 4)
        self.assertIn('stage=source-git', str(errors[0]))
        self.assertEqual([str(x) for x in errors[1:]], ['kill failed', 'reap failed', 'close failed'])
        child.stderr.close.assert_called_once()

    def test_successful_leader_cannot_abandon_background_members(self):
        child = mock.Mock(pid=424242, returncode=0)
        child.communicate.return_value = ('a'*40, '')
        with mock.patch.object(SOURCE.subprocess, 'Popen', return_value=child), mock.patch.object(SOURCE.os, 'killpg') as kill:
            with self.assertRaisesRegex(ValueError, 'owned descendants remaining'):
                SOURCE.git('status')
        self.assertEqual(kill.call_args_list, [mock.call(child.pid, 0), mock.call(child.pid, SOURCE.signal.SIGKILL)])


class SourceIdentityTests(unittest.TestCase):
    def test_default_requires_committed_contract_provenance(self):
        provenance = self.root / 'provenance.json'
        output = self.root / 'output'
        sha = self.git('rev-parse', 'HEAD')
        for revision, dirty in ((sha, True), ('devel', False), (None, False), (sha, None)):
            with self.subTest(revision=revision, dirty=dirty):
                provenance.write_text(json.dumps(dict(source_base_revision=revision, source_uncommitted=dirty)))
                self.assertNotEqual(self.select('paired', provenance, GITHUB_OUTPUT=str(output)).returncode, 0)
                self.assertFalse(output.exists())
        provenance.write_text(json.dumps(dict(source_base_revision=sha, source_uncommitted=False)))
        self.assertEqual(self.select('paired', provenance, GITHUB_OUTPUT=str(output)).returncode, 0)
        self.assertEqual(output.read_text(), f'pico_ref={sha}\n')

    def test_source_dispatch_rejects_echo_and_preserves_argument_meaning(self):
        self.assertEqual(source_arguments({'run': 'python3 -B ./scripts/ci/selected_source.py require "${PICO_SHA}"'}), ['require', '$PICO_SHA'])
        for command in ('echo verify "$PICO_SHA" repo', 'true record repo', 'python3 -c "verify"'):
            with self.assertRaises(AssertionError): source_arguments({'run': command})

    def test_output_identity_is_format_independent_and_rejects_fallbacks(self):
        expected = ('needs', 'change_scope', 'outputs', 'pico_sha')
        for value in ("${{needs.change_scope.outputs.pico_sha}}", "${{ needs ['change_scope'] . outputs ['pico_sha'] }}"):
            self.assertEqual(reference(value), expected)
        for value in ("devel", "${{ needs.change_scope.outputs.pico_sha || 'devel' }}", "${{ '' }}"):
            with self.assertRaises(AssertionError): reference(value)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='wsprrypi-source-graph-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / 'pico'
        self.repo.mkdir()
        self.git('init', '-b', 'devel')
        (self.repo / 'source').write_text('first\n')
        self.git('add', 'source')
        self.commit()

    def git(self, *args):
        result = run(['git', '-c', 'core.hooksPath=/dev/null', '-c', 'commit.gpgsign=false',
                      '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', *args], self.repo)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.strip()

    def commit(self):
        self.git('commit', '-am', 'fixture')
        return self.git('rev-parse', 'HEAD')

    def select(self, *args, **env):
        # Invoke main so interruption reaches its active owned-Git cleanup.
        # An outer process timeout must not kill this owner before cleanup.
        stdout, stderr = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', [str(SELECT), *map(str, args)]), mock.patch.dict(os.environ, env), contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            try:
                code = SOURCE.main()
            except SystemExit as result:
                code = result.code
        return subprocess.CompletedProcess(args, code, stdout.getvalue(), stderr.getvalue())

    def test_moving_ref_is_resolved_once_and_both_consumers_keep_that_commit(self):
        output = self.root / 'output'
        result = self.select('record', self.repo, GITHUB_OUTPUT=str(output))
        self.assertEqual(result.returncode, 0, result.stderr)
        pinned = output.read_text().strip().removeprefix('pico_sha=')
        (self.repo / 'source').write_text('second\n')
        current = self.commit()
        self.assertNotEqual(current, pinned)
        for carrier in ('network', 'usb'):
            checkout = self.root / carrier
            result = run(['git', 'clone', '--quiet', '--no-hardlinks', str(self.repo), str(checkout)], self.root)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(run(['git', 'checkout', '--detach', pinned], checkout).returncode, 0)
            verified = self.select('verify', pinned, checkout)
            self.assertEqual(verified.returncode, 0, verified.stderr)
        refused = self.select('verify', current, self.root / 'usb')
        self.assertNotEqual(refused.returncode, 0)
        self.assertIn('stage=source-verification', refused.stderr)

    def test_invalid_missing_dirty_and_wrong_root_sources_refuse_before_output(self):
        for value in ('', 'devel', '0' * 40, 'A' * 40, 'a' * 39, 'a' * 40 + '\nother'):
            with self.subTest(value=value):
                self.assertNotEqual(self.select('require', value).returncode, 0)
        for value in ('', '--help', 'invalid ref', '../escape'):
            self.assertNotEqual(self.select('selection', value).returncode, 0)
        for value in ('devel', 'refs/heads/devel', self.git('rev-parse', 'HEAD')):
            self.assertEqual(self.select('selection', value).returncode, 0)
        output = self.root / 'output'
        (self.repo / 'source').write_text('dirty\n')
        self.assertNotEqual(self.select('record', self.repo, GITHUB_OUTPUT=str(output)).returncode, 0)
        self.assertFalse(output.exists())
        (self.repo / 'source').write_text('first\n')
        (self.repo / 'untracked').write_text('unexpected source\n')
        self.assertNotEqual(self.select('record', self.repo, GITHUB_OUTPUT=str(output)).returncode, 0)
        (self.repo / 'untracked').unlink()
        (self.repo / 'nested').mkdir()
        self.assertNotEqual(self.select('record', self.repo / 'nested', GITHUB_OUTPUT=str(output)).returncode, 0)
        self.assertFalse(output.exists())

    def test_workflow_uses_one_immutable_owner_and_verifies_both_consumers(self):
        jobs = yaml.safe_load(WORKFLOW.read_text())['jobs']
        self.assertEqual(set(jobs), {'change_scope', 'macos-simulated-profile', 'macos-si5351-profile',
            'wtp-network-loopback', 'wtp-usb-management-pair', 'strict-i2c-profile',
            'ubuntu-gcc13-release', 'non-hardware-validation'})
        self.assertEqual(jobs['macos-si5351-profile']['strategy']['matrix']['runner'], ['macos-15', 'macos-15-intel'])
        resolver = jobs['change_scope']
        self.assertEqual(reference(resolver['outputs']['pico_sha']), ('steps', 'pico-output', 'outputs', 'pico_sha'))
        selected = [s for s in resolver['steps'] if s.get('with', {}).get('repository') == 'WsprryPi/WsprryPico']
        self.assertEqual(len(selected), 1)
        self.assertEqual(reference(selected[0]['with']['ref']), ('steps', 'requested-pico', 'outputs', 'pico_ref'))
        self.assertFalse(selected[0].get('continue-on-error', False))
        recorder = next(s for s in resolver['steps'] if s.get('id') == 'pico-output')
        self.assertEqual(source_arguments(recorder), ['record', selected[0]['with']['path']])
        self.assertFalse(recorder.get('continue-on-error', False))
        for carrier in ('wtp-network-loopback', 'wtp-usb-management-pair'):
            steps = jobs[carrier]['steps']
            checkout = next(i for i, s in enumerate(steps) if s.get('with', {}).get('repository') == 'WsprryPi/WsprryPico')
            self.assertEqual(reference(steps[checkout]['with']['ref']), ('needs', 'change_scope', 'outputs', 'pico_sha'))
            for operation, before in (('require', True), ('verify', False)):
                matches = [(i, s) for i, s in enumerate(steps) if operation in shlex.split(s.get('run', ''))]
                self.assertEqual(len(matches), 1)
                index, step = matches[0]
                self.assertEqual(index < checkout, before)
                self.assertEqual(reference(step['env']['PICO_SHA']), ('needs', 'change_scope', 'outputs', 'pico_sha'))
                expected = [operation, '$PICO_SHA']
                if operation == 'verify': expected.append(steps[checkout]['with']['path'])
                self.assertEqual(source_arguments(step), expected)
                self.assertNotIn('if', step, 'admitted carrier source gates must be unconditional')
                self.assertFalse(step.get('continue-on-error', False))
            self.assertFalse(steps[checkout].get('continue-on-error', False))
        for name in ('wtp-network-loopback','wtp-usb-management-pair','strict-i2c-profile',
                     'ubuntu-gcc13-release','non-hardware-validation'):
            self.assertEqual(make_goals(jobs[name])['ci-build-budget'],1,name)
        for name in ('macos-simulated-profile','macos-si5351-profile'):
            self.assertIn(shlex.split(jobs[name]['env']['MAKEFLAGS']),
                [['-j2'],['-j','2'],['--jobs=2'],['--jobs','2']])
        goals = make_goals(jobs['non-hardware-validation'])
        for required in ('wtp-plan-test', 'wtp-fleet-test', 'wtp-production-cpp-test', 'wtp-api-cpp-test',
                         'wtp-config-cpp-test', 'ci-focused-state-test', 'ci-network-policy-test',
                         'ci-support-lifecycle-test', 'ci-installer-offline-test', 'php2shell-test'):
            self.assertEqual(goals[required], 1, required)
        for duplicate in ('wtp-production-test', 'wtp-api-test', 'wtp-config-test', 'legacy-tone-frequency-test', 'test-tone-lifecycle-test'):
            self.assertEqual(goals[duplicate], 0, duplicate)
        self.assertEqual(make_goals(jobs['wtp-network-loopback'])['wtp-tls-test'], 0)
        self.assertEqual(make_goals(jobs['wtp-network-loopback'])['wtp-network-runtime-test'], 1)


class MakeDispatchTests(unittest.TestCase):
    def test_focused_owners_execute_required_leaves_and_forward_failure(self):
        required = {
            'ci-focused-state-test': 'guarded-mode-change-persistence-test band-gpio-default-convergence-test band-gpio-rotation-test selector-shutdown-cleanup-test monitor-file-regression-test gpio-line-resolver-test non-wspr-repeat-policy-test',
            'ci-network-policy-test': 'support-request-guard-test privileged-network-policy-test backend-http-guard-test websocket-upgrade-guard-test privileged-network-transaction-test privileged-network-runtime-test privileged-network-admin-test',
            'ci-support-lifecycle-test': 'support-bundle-result-validator-test support-bundle-runtime-test support-bundle-download-file-test support-bundle-checksum-file-test support-bundle-archive-digest-test support-bundle-download-preparation-test support-bundle-job-directory-remover-test support-bundle-startup-cleanup-test',
        }
        source = (ROOT / 'src/Makefile').read_text()
        phony = '\n'.join(line for line in source.splitlines() if line.startswith('.PHONY:'))
        for owner, children in required.items():
            expected = children.split()
            for failed in (None, *expected):
                with self.subTest(owner=owner, failed=failed), tempfile.TemporaryDirectory(prefix='wsprrypi-owner-') as directory:
                    root = Path(directory)
                    leaves = []
                    for child in expected:
                        rule(source, child)  # Missing production entry points are not stand-in successes.
                        leaves.append(child+':\n\t@echo '+child+' >> log\n\t@'+('exit 17' if child == failed else 'true'))
                        (root / child).touch()
                    (root / owner).touch()
                    (root / 'Makefile').write_text(phony+'\n'+rule(source, owner)+'\n'+'\n'.join(leaves))
                    result = run(['make', '--no-print-directory', owner], root)
                    if failed is None:
                        self.assertEqual(result.returncode, 0, result.stderr)
                        self.assertEqual(collections.Counter((root / 'log').read_text().splitlines()), collections.Counter(expected))
                    else:
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn(failed, result.stderr)

    def fixture(self, root):
        source = (ROOT / 'src/Makefile').read_text()
        targets = ('$(BIN_DIR)/wtp_production_test', 'wtp-production-cpp-test', 'wtp-api-cpp-test',
            'wtp-config-cpp-test', 'wtp-production-test', 'wtp-api-test', 'wtp-config-test',
            'wtp-ui-test', 'wtp-management-ui-test', 'pico-usb-installer-cpp-test',
            'pico-usb-installer-offline-test', 'pico-usb-installer-test', 'support-bundle-runtime-installer-test', 'ci-installer-offline-test')
        rules = '\n'.join(rule(source, target) for target in targets)
        phony = '\n'.join(line for line in source.splitlines() if line.startswith('.PHONY:'))
        (root / 'tests').mkdir()
        (root / 'tests/wtp_production_test.cpp').touch()
        (root / 'bin').mkdir()
        spy = root / 'bin/node'
        spy.write_text('#!'+sys.executable+'''
import os, pathlib, sys
if sys.argv[1] != 'compiler':
    allowed = {
      'node': [('..', 'WsprryPi-UI', 'tests', x) for x in ('wtp_ui_test.js', 'wtp_management_test.js', 'wtp_firmware_test.js')],
      'python3': [('..', 'scripts', 'tests', x) for x in ('pico_usb_deploy_test.py', 'pico_usb_bundle_test.py', 'apache_privileged_network_policy_renderer_test.py')],
      'bash': [('..', 'scripts', 'tests', 'support_bundle_runtime_install_test.sh')],
    }
    program = pathlib.Path(sys.argv[0]).name
    args = sys.argv[1:]
    if program == 'python3' and args[:1] == ['-B']: args = args[1:]
    if len(args) != 1 or pathlib.Path(args[0]).parts not in allowed.get(program, []): sys.exit(64)
    name = pathlib.Path(args[0]).name
    with open(os.environ['GRAPH_LOG'], 'a') as output: output.write(name+'\\n')
    if os.environ.get('FAIL_CASE') == name:
        print('stage=child-failure;child='+name, file=sys.stderr)
        sys.exit(19)
    sys.exit(0)
name = pathlib.Path(sys.argv[sys.argv.index('-o')+1])
if name.name not in ('wtp_production_test', 'wtp_browser_api_test', 'wtp_configuration_test', 'pico_usb_installer_test'): sys.exit(64)
if os.environ.get('OMIT_OUTPUT'): sys.exit(0)
name.parent.mkdir(parents=True, exist_ok=True)
name.write_text('#!/bin/sh\\nprintf "%s\\\\n" '+name.name+' >> "$GRAPH_LOG"\\nif [ "$FAIL_CASE" = '+name.name+' ]; then echo stage=child-failure\\;child='+name.name+' >&2; exit 17; fi\\n')
name.chmod(0o755)
''')
        spy.chmod(0o755)
        for program in ('python3', 'bash'):
            (root / 'bin' / program).symlink_to(spy.name)
        # Only dispatch is replaced here; incremental compilation is independently
        # exercised through the included production rules by network_build_test.py.
        build = '\n$(NETWORK_BIN_DIR)/%:\n\t$(CXX) -o $@\n'
        (root / 'Makefile').write_text('Q := @\nBIN_DIR := build/bin\nNETWORK_BIN_DIR := build/network/incremental/test/bin\nWTP_NETWORK_BUILD_DIR := build/network\nCXX := '+sys.executable+' '+str(spy)+' compiler\n'+phony+'\n'+rules+build)
        for target in targets[1:]: (root / target).touch()  # Existing same-name files cannot suppress public suites.

    def dispatch(self, goals, expected, **env):
        with tempfile.TemporaryDirectory(prefix='wsprrypi-make-owner-') as directory:
            root = Path(directory)
            self.fixture(root)
            log = root / 'log'
            result = run(['make', '--no-print-directory', *goals], root,
                PATH=str(root / 'bin')+os.pathsep+os.environ['PATH'], GRAPH_LOG=str(log), **env)
            if expected is not None:
                self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                self.assertEqual(collections.Counter(log.read_text().splitlines()), collections.Counter(expected))
            return result

    def test_cpp_owners_and_standalone_aggregates_execute_each_required_child(self):
        cpp = ['wtp_production_test', 'wtp_browser_api_test', 'wtp_configuration_test']
        self.dispatch(['wtp-production-cpp-test', 'wtp-api-cpp-test', 'wtp-config-cpp-test'], cpp)
        self.dispatch(['wtp-production-test', 'wtp-api-test', 'wtp-config-test'], cpp+['wtp_ui_test.js', 'wtp_management_test.js'])

    def test_offline_installer_owner_keeps_cpp_python_shell_and_standalone_ui(self):
        expected = ['pico_usb_installer_test', 'pico_usb_deploy_test.py', 'pico_usb_bundle_test.py',
                    'support_bundle_runtime_install_test.sh', 'apache_privileged_network_policy_renderer_test.py']
        self.dispatch(['ci-installer-offline-test'], expected)
        self.dispatch(['pico-usb-installer-test'], expected[:3]+['wtp_firmware_test.js'])
        for leaf in expected:
            with self.subTest(leaf=leaf):
                result = self.dispatch(['ci-installer-offline-test'], None, FAIL_CASE=leaf)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('stage=child-failure;child='+leaf, result.stderr)

    def test_cpp_ui_failures_and_missing_executable_propagate(self):
        for leaf in ('wtp_production_test', 'wtp_browser_api_test', 'wtp_configuration_test', 'wtp_ui_test.js', 'wtp_management_test.js'):
            with self.subTest(leaf=leaf):
                result = self.dispatch(['wtp-production-test', 'wtp-api-test', 'wtp-config-test'], None, FAIL_CASE=leaf)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('stage=child-failure;child='+leaf, result.stderr.replace('; child=', ';child='))
        missing = self.dispatch(['wtp-api-cpp-test'], None, OMIT_OUTPUT='1')
        self.assertNotEqual(missing.returncode, 0)
        self.assertIn('wtp_browser_api_test', missing.stderr)


if __name__ == '__main__':
    unittest.main()
