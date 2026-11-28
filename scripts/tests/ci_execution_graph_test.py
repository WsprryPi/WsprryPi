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
MACOS_RUNTIME_JOBS = ('macos-simulated-profile', 'macos-controller-recovery', 'macos-pico-carriers')
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
                goals += [word for word in words[1:] if (word.endswith('-test') or word in ('ci-build-budget', 'semantics-test-portable')) and '=' not in word]
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

    def test_manual_macos_runtime_scope_preserves_default_full_validation(self):
        workflow = yaml.safe_load(WORKFLOW.read_text())
        # PyYAML's YAML 1.1 loader reads the Actions `on` key as True.
        triggers = workflow.get('on', workflow.get(True))
        selection = triggers['workflow_dispatch']['inputs']['macos_runtime_only']
        self.assertEqual(selection['type'], 'boolean')
        self.assertIs(selection['default'], False)
        profiles = triggers['workflow_dispatch']['inputs']['macos_runtime_profiles']
        self.assertEqual(profiles['type'], 'choice')
        self.assertEqual(profiles['default'], 'all')
        expression = workflow['jobs']['macos-simulated-profile']['strategy']['matrix']['runner']
        pattern = r"\$\{\{ fromJSON\('(.+)'\)\[\(inputs\.macos_runtime_only && inputs\.macos_runtime_profiles\) \|\| 'all'\] \}\}"
        match = re.fullmatch(pattern, expression)
        self.assertIsNotNone(match, 'full dispatch must ignore partial profile selection')
        mapping = json.loads(match[1])
        self.assertEqual(set(profiles['options']), set(mapping))
        self.assertEqual(mapping['all'], ['macos-26', 'xcode-27'])
        self.assertEqual(mapping['arm64'], mapping['all'])
        for runner in mapping['all']:
            self.assertEqual(mapping[runner], [runner])
        self.assertIsNone(mapping.get('self-hosted'))
        jobs = workflow['jobs']
        self.assertNotIn('if', jobs['change_scope'])
        runtime_guard = "${{ !cancelled() && (needs.change_scope.result != 'success' || needs.change_scope.outputs.run_full_ci != 'false') }}"
        for name in MACOS_RUNTIME_JOBS:
            job = jobs[name]
            self.assertEqual(job['if'], runtime_guard, name)
            self.assertEqual(job['needs'], 'change_scope', name)
            self.assertEqual(job['strategy']['matrix']['runner'], expression, name)
            self.assertIs(job['strategy']['fail-fast'], False, name)
        build_checks = jobs['ci-build-regressions']
        self.assertEqual(build_checks['needs'], 'change_scope')
        self.assertEqual(build_checks['if'], runtime_guard)
        for name, job in jobs.items():
            if name not in ('change_scope', 'ci-build-regressions') and name not in MACOS_RUNTIME_JOBS:
                self.assertIn('inputs.macos_runtime_only != true', job['if'], name)
                self.assertIn('!cancelled()', job['if'], name)
                self.assertIn("needs.change_scope.outputs.run_full_ci != 'false'", job['if'], name)

    def test_macos_controller_scope_preserves_full_semantics_and_runtime_gates(self):
        workflow = yaml.safe_load(WORKFLOW.read_text())
        triggers = workflow.get('on', workflow.get(True))
        selection = triggers['workflow_dispatch']['inputs']['macos_controller_only']
        self.assertEqual(selection['type'], 'boolean')
        self.assertIs(selection['default'], False)
        jobs = workflow['jobs']
        job = jobs['macos-simulated-profile']
        steps = {step.get('name'): step for step in job['steps']}
        scope = 'inputs.macos_runtime_only && inputs.macos_controller_only'
        portable = steps['Run portable semantics profile']
        self.assertEqual(portable['if'], '${{ !(' + scope + ') }}')
        self.assertIn(['make', 'semantics-test-portable', 'SUDO='],
                      [shlex.split(line) for line in portable['run'].splitlines()])
        state = steps['Run controller state and native adapter contracts']
        self.assertEqual(state['if'], '${{ ' + scope + ' }}')
        goals = make_goals({'steps': [state]})
        for target in ('sqlite-state-store-test', 'state-migration-test', 'controller-identity-store-test',
                       'recovery-capture-backend-test', 'usb-cdc-native-test', 'native-socket-test',
                       'local-control-transport-test', 'database-controller-runtime-test'):
            self.assertEqual(goals[target], 1, target)
        self.assertNotIn('if', steps['Run controller and target management contracts'])
        browser_unit = steps['Run the WTP browser unit test when Linux UI checks are excluded']
        self.assertEqual(browser_unit['if'], '${{ inputs.macos_runtime_only == true }}')
        self.assertEqual(shlex.split(browser_unit['run']), ['node', 'WsprryPi-UI/tests/wtp_ui_test.js'])
        linux_ui = next(step for step in jobs['debian-browser-validation']['steps']
                        if step.get('name') == 'Run UI component and browser regressions')
        self.assertIn('npm test', linux_ui['run'].splitlines())
        package = json.loads((ROOT / 'WsprryPi-UI/package.json').read_text())
        self.assertEqual(package['scripts']['test'], 'npm run test:unit')
        self.assertIn('node tests/wtp_ui_test.js', package['scripts']['test:unit'].split(' && '))
        self.assertNotIn('Check container build export handling', steps)
        container = next(step for step in jobs['ci-build-regressions']['steps']
                         if step.get('name') == 'Check container build export handling')
        self.assertNotIn('if', container)
        self.assertEqual(container['run'].splitlines(), ['python3 -B scripts/tests/container_build_test.py',
                                                          'python3 -B scripts/tests/container_avahi_contract_test.py'])
        recovery = {step.get('name'): step for step in workflow['jobs']['macos-controller-recovery']['steps']}
        self.assertNotIn('if', recovery['Build and audit the isolated controller profile'])
        after_portable = recovery['Run bounded recovery role, relocation, and restart matrix']
        full = recovery['Run the complete recovery matrix for controller-only dispatches']
        self.assertEqual(after_portable['if'], '${{ !(' + scope + ') }}')
        self.assertEqual(full['if'], '${{ ' + scope + ' }}')
        self.assertEqual(make_goals({'steps': [after_portable]})['recovery-integration-after-portable-test'], 1)
        self.assertEqual(make_goals({'steps': [full]})['recovery-integration-test'], 1)
        self.assertEqual(recovery['Qualify pairing and controller roles without hardware']['if'], '${{ inputs.macos_runtime_only != true }}')
        # Portable semantics already runs both local-transmitter recurrence tests.
        # Do not rerun their precision-dependent launches in a later step.
        core_goals = make_goals(job)
        self.assertNotIn('native-recurrence-test', core_goals)
        self.assertEqual(core_goals['wtp-managed-output-test'], 1)
        self.assertEqual(core_goals['pico-usb-management-contract-test'], 0)
        carriers = jobs['macos-pico-carriers']
        self.assertEqual(carriers['strategy']['matrix']['carrier'], ['network', 'usb'])
        self.assertFalse(carriers['strategy']['fail-fast'])
        self.assertEqual(make_goals(carriers)['pico-usb-management-contract-test'], 1)
        carrier_steps = {step.get('name'): step for step in carriers['steps']}
        for name, carrier in (
                ('Exercise actual native Mac controller and Pico network peers', 'network'),
                ('Build the actual software Pico USB endpoint on native Mac', 'usb'),
                ('Exercise native Mac clients through production Pico USB handlers', 'usb')):
            step = carrier_steps[name]
            self.assertEqual(step['if'], f"matrix.carrier == '{carrier}'")
            self.assertFalse(step.get('continue-on-error', False))
            if carrier == 'usb':
                self.assertNotIn('credentials-v3', step['run'])
        mbedtls, = [step for step in carriers['steps'] if step.get('with', {}).get('repository') == 'Mbed-TLS/mbedtls']
        self.assertEqual(mbedtls['if'], "matrix.carrier == 'network'")
        source = (ROOT / 'src/Makefile').read_text()
        self.assertIn('-j1 native-recurrence-test native-recurrence-main-test', source)
        self.assertRegex(source, r'(?m)^semantics-test-full semantics-test-portable-profile: .*\brecovery-worker-process-test\b')

    def test_workflow_uses_one_immutable_owner_and_verifies_both_consumers(self):
        jobs = yaml.safe_load(WORKFLOW.read_text())['jobs']
        self.assertEqual(set(jobs), {*MACOS_RUNTIME_JOBS, 'change_scope', 'macos-si5351-profile',
            'pico-network-management-pair', 'pico-usb-management-pair', 'strict-i2c-profile',
            'ubuntu-gcc13-release', 'non-hardware-validation', 'native-controller-components',
            'linux-native-contracts', 'debian-browser-validation', 'debian-installer-support',
            'ci-build-regressions'})
        self.assertEqual(jobs['macos-si5351-profile']['strategy']['matrix']['runner'], ['macos-26', 'xcode-27'])
        self.assertEqual(jobs['native-controller-components']['strategy']['matrix']['runner'], ['ubuntu-24.04', 'macos-26', 'xcode-27', 'windows-2025'])
        native_steps = {step['name']: step for step in jobs['native-controller-components']['steps'] if 'name' in step}
        for name, condition, script in (
                ('Check native controller bundle and proxy contracts', "runner.os != 'Linux'", 'controller_native_bundle_test.py'),
                ('Check macOS installer dispatch and service definitions', "runner.os == 'macOS'", 'controller_install_test.py')):
            step = native_steps[name]
            self.assertEqual(step['if'], condition)
            self.assertEqual(shlex.split(step['run']), ['python', '-B', f'scripts/tests/{script}'])
            self.assertFalse(step.get('continue-on-error', False))
        for profile in (*MACOS_RUNTIME_JOBS, 'macos-si5351-profile'):
            self.assertEqual(jobs[profile]['env']['MACOSX_DEPLOYMENT_TARGET'], "${{ matrix.runner == 'xcode-27' && '27.0' || '26.0' }}")
            self.assertEqual(jobs[profile]['env']['WSPRRYPI_DISABLE_HARDWARE_ACCESS'], '1')
            steps = jobs[profile]['steps']
            self.assertEqual(steps[0]['with']['persist-credentials'], False)
            preflight = next(i for i, step in enumerate(steps) if step.get('name') == 'Record the native minimum-OS software profile')
            self.assertTrue(all('make ' not in step.get('run', '') for step in steps[:preflight]))
        resolver = jobs['change_scope']
        self.assertEqual(reference(resolver['outputs']['pico_sha']), ('steps', 'pico-output', 'outputs', 'pico_sha'))
        selected = [s for s in resolver['steps'] if s.get('with', {}).get('repository') == 'WsprryPi/WsprryPico']
        self.assertEqual(len(selected), 1)
        self.assertEqual(reference(selected[0]['with']['ref']), ('steps', 'requested-pico', 'outputs', 'pico_ref'))
        self.assertFalse(selected[0].get('continue-on-error', False))
        recorder = next(s for s in resolver['steps'] if s.get('id') == 'pico-output')
        self.assertEqual(source_arguments(recorder), ['record', selected[0]['with']['path']])
        self.assertFalse(recorder.get('continue-on-error', False))
        for carrier in ('pico-network-management-pair', 'pico-usb-management-pair', 'macos-pico-carriers'):
            steps = jobs[carrier]['steps']
            checkout = next(i for i, s in enumerate(steps) if s.get('with', {}).get('repository') == 'WsprryPi/WsprryPico')
            self.assertEqual(reference(steps[checkout]['with']['ref']), ('needs', 'change_scope', 'outputs', 'pico_sha'))
            for operation, before in (('require', True), ('verify', False)):
                matches = [(i, s) for i, s in enumerate(steps) if operation in shlex.split(s.get('run', ''))]
                self.assertEqual(len(matches), 1 if operation == 'require' else 2)
                for index, step in matches:
                    self.assertEqual(index < checkout, before)
                    self.assertEqual(reference(step['env']['PICO_SHA']), ('needs', 'change_scope', 'outputs', 'pico_sha'))
                    expected = [operation, '$PICO_SHA']
                    if operation == 'verify': expected.append(steps[checkout]['with']['path'])
                    self.assertEqual(source_arguments(step), expected)
                    self.assertNotIn('if', step, 'admitted carrier source gates must be unconditional')
                    self.assertFalse(step.get('continue-on-error', False))
                if operation == 'verify' and carrier != 'macos-pico-carriers':
                    recorded = next(i for i, step in enumerate(steps) if step.get('name') == 'Record frozen pair source manifests')
                    budget = next(i for i, step in enumerate(steps) if step.get('name') == 'Select bounded build budget')
                    checked = next(i for i, step in enumerate(steps) if step.get('name') == 'Verify frozen pair sources after the complete recipe')
                    final_recipes = (
                        ('Render logging, USB, and Fleet controls with mocked responses',
                         'Render network and recovery controls with production readbacks')
                        if carrier == 'pico-network-management-pair'
                        else ('Actual UI producer through production Pi and Pico USB handlers',)
                    )
                    self.assertLess(matches[0][0], recorded)
                    self.assertLess(recorded, budget)
                    for title in final_recipes:
                        recipe = next(i for i, step in enumerate(steps) if step.get('name') == title)
                        self.assertLess(budget, recipe)
                        self.assertLess(recipe, matches[1][0])
                    self.assertLess(matches[1][0], checked)
                    for step in (steps[recorded], steps[checked]):
                        self.assertNotIn('if', step)
                        self.assertFalse(step.get('continue-on-error', False))
            self.assertFalse(steps[checkout].get('continue-on-error', False))
        for name in ('pico-network-management-pair','pico-usb-management-pair','strict-i2c-profile',
                     'ubuntu-gcc13-release','non-hardware-validation','linux-native-contracts',
                     'debian-browser-validation','debian-installer-support'):
            self.assertEqual(make_goals(jobs[name])['ci-build-budget'],1,name)
        for name in (*MACOS_RUNTIME_JOBS, 'macos-si5351-profile'):
            self.assertIn(shlex.split(jobs[name]['env']['MAKEFLAGS']),
                [['-j2'],['-j','2'],['--jobs=2'],['--jobs','2']])
        semantics = jobs['non-hardware-validation']
        native = jobs['linux-native-contracts']
        for job in (semantics, native):
            self.assertEqual(job['needs'], 'change_scope')
            self.assertEqual(job['timeout-minutes'], 40)
            self.assertEqual(job['env']['WSPRRYPI_DISABLE_HARDWARE_ACCESS'], '1')
        self.assertEqual(native['if'], semantics['if'])
        semantics_goals, native_goals = make_goals(semantics), make_goals(native)
        for required in ('wtp-managed-output-test', 'wtp-fleet-test',
                         'wtp-production-cpp-test', 'wtp-api-cpp-test', 'wtp-config-cpp-test'):
            self.assertEqual(native_goals[required], 1, required)
            self.assertEqual(semantics_goals[required], 0, required)
        for required in ('semantics-test', 'startup-quiesce-parent-test', 'ci-focused-state-test',
                         'ci-network-policy-test', 'ci-support-lifecycle-test'):
            self.assertEqual(semantics_goals[required], 1, required)
            self.assertEqual(native_goals[required], 0, required)
        installer_goals = make_goals(jobs['debian-installer-support'])
        for required in ('ci-installer-offline-test', 'php2shell-test', 'support-bundle-age-roundtrip-test'):
            self.assertEqual(installer_goals[required], 1, required)
            self.assertEqual(semantics_goals[required] + native_goals[required], 0, required)
        for duplicate in ('wtp-production-test', 'wtp-api-test', 'wtp-config-test', 'legacy-tone-frequency-test', 'test-tone-lifecycle-test'):
            self.assertEqual(semantics_goals[duplicate] + native_goals[duplicate], 0, duplicate)
        self.assertEqual(native_goals['native-recurrence-test'], 0)
        makefile = (ROOT / 'src/Makefile').read_text()
        self.assertIn('-j1 native-recurrence-test native-recurrence-main-test',
                      rule(makefile, 'semantics-test-full'))
        macos_goals = make_goals(jobs['macos-simulated-profile'])
        self.assertEqual(macos_goals['semantics-test-portable'], 1)
        self.assertEqual(macos_goals['native-recurrence-test'], 0)
        self.assertEqual(make_goals(jobs['pico-network-management-pair'])['wtp-tls-test'], 0)
        self.assertEqual(make_goals(jobs['pico-network-management-pair'])['pi-network-runtime-regression-test'], 1)
        self.assertEqual(macos_goals['wtp-pi-identity-test'], 1)
        self.assertEqual(make_goals(jobs['macos-si5351-profile'])['wtp-pi-identity-test'], 0)

    def test_independent_macos_carriers_verify_both_sources_and_run_the_real_recipes(self):
        job = yaml.safe_load(WORKFLOW.read_text())['jobs']['macos-pico-carriers']
        self.assertEqual(job['strategy']['matrix']['carrier'], ['network', 'usb'])
        steps = job['steps']
        names = [step.get('name') for step in steps]
        pi = [i for i, name in enumerate(names) if name == 'Verify immutable Pi source for native Mac carriers']
        pico = [i for i, name in enumerate(names) if name == 'Verify immutable Pico source for native Mac carriers']
        self.assertEqual(len(pi), 2)
        self.assertEqual(len(pico), 2)
        for index in (*pi, *pico):
            self.assertNotIn('if', steps[index])
            self.assertFalse(steps[index].get('continue-on-error', False))
        for name, carrier in (('Exercise actual native Mac controller and Pico network peers', 'network'),
                              ('Build the actual software Pico USB endpoint on native Mac', 'usb'),
                              ('Exercise native Mac clients through production Pico USB handlers', 'usb')):
            index = names.index(name)
            self.assertEqual(steps[index]['if'], "matrix.carrier == '" + carrier + "'")
            self.assertLess(max(pi[0], pico[0]), index)
            self.assertLess(index, min(pi[1], pico[1]))
        tls = next(step for step in steps if step.get('with', {}).get('repository') == 'Mbed-TLS/mbedtls')
        self.assertEqual(tls['if'], "matrix.carrier == 'network'")
        self.assertEqual(tls['with']['ref'], '0bebf8b8c7f07abe3571ded48a11aa907a1ffb20')
        self.assertEqual(tls['with']['submodules'], 'recursive')
        network = steps[names.index('Exercise actual native Mac controller and Pico network peers')]
        for target in ('pi-network-runtime-regression-test', 'pico-https-management-interop-test', 'pico-control-interop-test'):
            self.assertEqual(make_goals({'steps': [network]})[target], 1)
        usb = steps[names.index('Exercise native Mac clients through production Pico USB handlers')]
        commands = [shlex.split(line) for line in usb['run'].splitlines()]
        self.assertEqual([next(word for word in line if word.endswith('.js')).rsplit('/', 1)[-1] for line in commands],
                         ['pico_usb_control_pair_test.js', 'pico_management_usb_pair_test.js', 'fleet_operations_pair_test.js'])
        self.assertEqual(len(commands[-1]), 5, 'Fleet USB must launch without network credentials')

        gate = steps[pi[0]]['run']
        sha = self.git('rev-parse', 'HEAD')
        self.assertEqual(run(['bash', '-e', '-c', gate], self.repo, GITHUB_SHA=sha).returncode, 0)
        self.assertNotEqual(run(['bash', '-e', '-c', gate], self.repo, GITHUB_SHA='0'*40).returncode, 0)
        (self.repo / 'untracked').touch()
        self.assertNotEqual(run(['bash', '-e', '-c', gate], self.repo, GITHUB_SHA=sha).returncode, 0)

    def test_pico_browser_independence_and_production_readback_order(self):
        pair = yaml.safe_load(WORKFLOW.read_text())['jobs']['pico-network-management-pair']
        self.assertEqual(pair['strategy']['matrix']['suite'], ['interop', 'browser'])
        self.assertFalse(pair['strategy']['fail-fast'])
        recipes = {step['name']: step for step in pair['steps'] if 'name' in step}
        runtime = recipes['Select the Pico browser test runtime']
        self.assertNotIn('if', runtime)
        self.assertEqual(runtime['with']['node-version'], '24.8.0')
        self.assertLess(pair['steps'].index(runtime), pair['steps'].index(recipes['Install UI development dependencies']))
        for suite, names in (
                ('interop', ('Validate retained Fleet records without execution',
                             'Parent TLS, shared API and actual Pico interoperability',
                             'Current shared Setup through the actual Pi and Pico handlers',
                             'Render network and recovery controls with production readbacks')),
                ('browser', ('Shipped Pico controls through actual public and encrypted HTTP handlers',
                             'Prove edited Pico source selection and genuine build failures',
                             'Render logging, USB, and Fleet controls with mocked responses'))):
            for name in names:
                self.assertEqual(recipes[name]['if'], f"matrix.suite == '{suite}'")
                self.assertFalse(recipes[name].get('continue-on-error', False))
        producer = recipes['Parent TLS, shared API and actual Pico interoperability']
        self.assertIn('make pico-control-interop-test ', producer['run'])
        readbacks = recipes['Render network and recovery controls with production readbacks']
        self.assertLess(pair['steps'].index(producer), pair['steps'].index(readbacks))
        for script in ('wtp_network_ui_integration_test.js', 'wtp_recovery_ui_integration_test.js'):
            self.assertIn(script, readbacks['run'])
            self.assertNotIn(script, recipes['Render logging, USB, and Fleet controls with mocked responses']['run'])

    def test_parallel_jobs_keep_browser_and_release_coverage(self):
        jobs = yaml.safe_load(WORKFLOW.read_text())['jobs']
        semantics = jobs['non-hardware-validation']
        browser = jobs['debian-browser-validation']
        for field in ('needs', 'if', 'container', 'env', 'defaults'):
            self.assertEqual(browser[field], semantics[field], field)
        self.assertEqual(browser['strategy']['matrix']['suite'], ['ui', 'production'])
        self.assertFalse(browser['strategy']['fail-fast'])
        steps = {step.get('name'): step for step in browser['steps']}
        ui = steps['Run UI component and browser regressions']
        self.assertEqual(ui['if'], "matrix.suite == 'ui'")
        self.assertEqual(ui['run'].splitlines(), ['npm test', 'npm run test:browser'])
        integration = steps['Run production browser integration regressions']
        self.assertEqual(integration['if'], "matrix.suite == 'production'")
        self.assertIn('node tests/controller_mode_browser_test.js', integration['run'])
        browser_suite = json.loads((ROOT / 'WsprryPi-UI/package.json').read_text())['scripts']['test:browser']
        pair_steps = jobs['pico-network-management-pair']['steps']
        for test in ('pico_target_editor_state_browser_test.js', 'recurrence_migration_browser_test.js'):
            self.assertIn(f'node tests/{test}', browser_suite)
            self.assertTrue(all(test not in step.get('run', '') for step in pair_steps))
        for target in ('recovery-maintenance-browser-test', 'recovery-maintenance-controller-browser-test',
                       'controller-only-test', 'pico-management-target-test'):
            self.assertEqual(make_goals(browser)[target], 1, target)
            self.assertEqual(make_goals(semantics)[target], 0, target)
        release = jobs['ubuntu-gcc13-release']
        profiles = release['strategy']['matrix']['include']
        self.assertFalse(release['strategy']['fail-fast'])
        self.assertCountEqual([(p['profile'], p['omitted']) for p in profiles], [
            ('rpi-gpio', 'si5351'), ('rp1-gpclk', 'si5351'), ('si5351', 'simulated'),
            ('simulated', 'si5351'), ('si5351,simulated', 'gpio'),
            ('rpi-gpio,rp1-gpclk,si5351,simulated', '')])
        logger = next(step for step in jobs['ci-build-regressions']['steps']
                      if step.get('name') == 'Verify Linux logger shutdown')
        self.assertEqual(logger['if'], 'inputs.macos_runtime_only != true')
        install, test = logger['run'].splitlines()[-2:]
        self.assertTrue(install.startswith('sudo apt-get install '))
        self.assertTrue({'libsystemd-dev', 'pkg-config'}.issubset(install.split()))
        self.assertEqual(test, 'python3 -B scripts/tests/logger_shutdown_test.py')
        self.assertNotIn('logger_shutdown_test.py', '\n'.join(step.get('run', '') for step in release['steps']))

    def test_debian_state_and_recovery_have_one_owner_and_keep_the_trace_audit(self):
        makefile = (ROOT / 'src/Makefile').read_text()
        shared = rule(makefile, 'semantics-test-full semantics-test-portable-profile').split(':', 1)[1].split()
        focused = rule(makefile, 'ci-focused-state-test').split(':', 1)[1].split()
        self.assertTrue(set(shared).isdisjoint(focused))
        jobs = yaml.safe_load(WORKFLOW.read_text())['jobs']
        installer = jobs['debian-installer-support']
        steps = installer['steps']
        prepare = next(step for step in steps if step.get('name') == 'Prepare writable installer build directory')
        reference = next(step for step in steps if step.get('with', {}).get('path') == 'src/build/rp1-provider-reference')
        self.assertIn('runuser -u wsprrypi-ci', installer['defaults']['run']['shell'])
        self.assertNotIn('shell', prepare)
        self.assertEqual(prepare['run'], 'mkdir -p src/build')
        self.assertLess(steps.index(prepare), steps.index(reference))
        for name in ('non-hardware-validation', 'debian-installer-support', 'debian-browser-validation',
                     'linux-native-contracts'):
            job = jobs[name]
            setup = next(step for step in job['steps'] if step.get('name') == 'Install build and audit dependencies')
            self.assertEqual(job['container'], 'debian:trixie-slim')
            self.assertEqual(setup['shell'], 'bash')
            self.assertIn('if [[ ! -s /etc/machine-id ]]; then', setup['run'])
            self.assertIn("tr -d '-' </proc/sys/kernel/random/uuid >/etc/machine-id", setup['run'])
        trace_path = r'/tmp/wsprrypi-[\w-]+\.strace'
        trace_command = 'strace --seccomp-bpf -f -y -e trace=open,openat,openat2,creat'
        for name, gate, audit_name in (
                ('non-hardware-validation', 'traced-semantics', 'Reject transmitter hardware access'),
                ('debian-installer-support', 'traced-installer', 'Reject installer transmitter hardware access'),
                ('debian-installer-support', 'traced-support', 'Reject support-bundle transmitter hardware access')):
            steps = jobs[name]['steps']
            audit = next(step for step in steps if step.get('name') == audit_name)
            self.assertEqual(audit['if'], '${{ !cancelled() && (steps.' + gate +
                             ".outcome == 'success' || steps." + gate + ".outcome == 'failure') }}")
            self.assertFalse(audit.get('continue-on-error', False))
            producers = steps[:steps.index(audit)]
            if name == 'debian-installer-support':
                producers = [step for step in producers if step.get('id') == gate]
            traced = [step['run'] for step in producers if re.search(r'-o ' + trace_path, step.get('run', ''))]
            self.assertTrue(any(step.get('id') == gate and step.get('run') in traced for step in producers))
            emitted = set()
            for command in traced:
                self.assertEqual(command.count(trace_command), len(re.findall(r'-o ' + trace_path, command)))
                emitted.update(re.findall(r'-o (' + trace_path + ')', command))
            if name == 'non-hardware-validation':
                for script in ('verify-simulated-backend.sh', 'verify-simulated-wspr.sh'):
                    self.assertTrue(any(f'../scripts/ci/{script}' in step.get('run', '') for step in producers))
                    source = (ROOT / 'scripts/ci' / script).read_text()
                    self.assertIn(trace_command, source)
                    emitted.update(re.findall(trace_path, source))
            self.assertCountEqual(re.findall(trace_path, audit['run']), emitted)



class MakeDispatchTests(unittest.TestCase):
    def test_macos_recovery_reuses_only_the_worker_check_owned_by_portable_semantics(self):
        source = (ROOT / 'src/Makefile').read_text()
        group = next(line for line in source.splitlines() if line.startswith('RECOVERY_INTEGRATION_TESTS := '))
        full = group.split(':= ', 1)[1].split()
        self.assertEqual(full, ['recovery-state-owner-test', 'recovery-worker-process-test',
                                'recovery-native-owner-controller-test', 'recovery-native-owner-move-test',
                                'recovery-native-owner-crash-test', 'recovery-maintenance-controller-test',
                                'recovery-maintenance-move-test', 'recovery-maintenance-roundtrip-test',
                                'recovery-maintenance-listener-override-test', 'recovery-maintenance-history-test',
                                'recovery-maintenance-crash-test'])
        phony = next(line for line in source.splitlines() if line.startswith('.PHONY: recovery-integration-test '))
        with tempfile.TemporaryDirectory(prefix='wsprrypi-macos-recovery-') as directory:
            root = Path(directory)
            leaves = ''.join(child + ':\n\t@echo ' + child + ' >> log\n\t@test "$$FAIL_CASE" != ' + child + '\n'
                             for child in full)
            (root / 'Makefile').write_text('\n'.join((group, phony, rule(source, 'recovery-integration-test'),
                rule(source, 'recovery-integration-after-portable-test'), leaves)))
            for target, expected in (('recovery-integration-test', full), ('recovery-integration-after-portable-test',
                                       [child for child in full if child != 'recovery-worker-process-test'])):
                with self.subTest(target=target):
                    (root / 'log').unlink(missing_ok=True)
                    result = run(['make', '--no-print-directory', '-j2', target], root)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual((root / 'log').read_text().splitlines(), expected)
                    refused = run(['make', '--no-print-directory', '-j2', target], root, FAIL_CASE=expected[0])
                    self.assertNotEqual(refused.returncode, 0)

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
        (root / 'tests/wtp_retirement_runtime_test.cpp').touch()
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
