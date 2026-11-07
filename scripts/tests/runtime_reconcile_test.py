#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Offline startup orchestration tests; fake public provider, no system effects."""
import contextlib
import copy
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import runtime_reconcile as recovery


def digest(value):
    return recovery.installer.sha256_bytes(recovery.installer.canonical(value))


def digest_bytes(value):
    return recovery.installer.sha256_bytes(value)


class System:
    def __init__(self, route='gpio4', history=True):
        self.record = {'runtime': {'bindingSha256': 'a'*64, 'artifactSetSha256': 'b'*64,
                                  'activationPlanSha256': 'd'*64}}
        self.config = {'backend': 'rp1-gpclk', 'route': route, 'transmit': False}
        self.saved = None
        self.env = None
        self.calls = []
        self.state = 'activation_required'
        self.service_state = {'LoadState': 'loaded', 'ActiveState': 'active', 'UnitFileState': 'enabled'}
        self.current_boot = '00000000-0000-0000-0000-000000000002'
        self.plan = {'version': 1, 'activationContext': 'post-reboot',
            'bindingSha256': 'a'*64, 'artifactSetSha256': 'b'*64,
            'bootId': self.current_boot, 'rebootEvidence': {'transactions': {
                'transaction.json': {'phase': 'complete-inhibited', 'target': 1 if route == 'gpio4' else 2} if history else None}}}
        self.plan_hash = digest(self.plan)
        self.journal = {'phase': 'complete-neutral', 'plan': {'bootId': '00000000-0000-0000-0000-000000000001'}}
        self.active = None
        self.interrupt = None

    def bootstrap_capable(self): pass
    def lock(self, wait=False): return contextlib.nullcontext()
    def ownership(self): return copy.deepcopy(self.record)
    def configuration(self): return copy.deepcopy(self.config)
    def checkpoint(self): return copy.deepcopy(self.saved)
    def activation_journal(self, name='activation.json'):
        return copy.deepcopy(self.journal) if name == 'activation.json' else None
    def boot(self): return self.current_boot
    def service(self): return copy.deepcopy(self.service_state)
    def environment(self, idle): self.env = idle
    def save(self, value):
        self.saved = copy.deepcopy(value)
        if self.interrupt == value['phase']: raise KeyboardInterrupt()
    def call(self, operation, arguments=()):
        self.calls.append((operation, tuple(arguments)))
        if operation == 'inspect':
            return {'result': self.state, 'identities': {'installedBinding': {
                'status': 'valid', 'sha256': 'a'*64, 'value': {'artifactSetSha256': 'b'*64}}},
                'activation': {'value': {'activationJournal': copy.deepcopy(self.journal)}},
                'routes': {'active': self.active, **{key: self.config['route'] for key in
                    ('requested', 'configured', 'persisted')}},
                'manager': {'query': {'state': {'outputEnabled': False}}},
                'safety': {'owner': False, 'lease': False, 'operationalReady': True,
                           'clock': 'quiescent', 'gpio': 'quiescent', 'dma': 'quiescent'}}
        if operation == 'activation-plan':
            return {'activationPlan': dict(self.plan, planSha256=self.plan_hash)}
        if operation == 'activation-ensure':
            assert arguments == ['--plan-sha256', self.plan_hash]
            self.journal = {'phase': 'complete-neutral', 'plan': copy.deepcopy(self.plan), 'planSha256': self.plan_hash}
            self.state = 'neutral_ready'
            if self.interrupt == operation: raise KeyboardInterrupt()
            return {'operation': operation, 'response': {'status': 'activated-neutral'}}
        if operation == 'route-plan':
            body = {'version': 1, 'operation': 'select',
                'route': self.config['route'], 'bindingSha256': 'a'*64,
                **{key: self.config['route'] for key in
                   ('requestedRoute', 'configuredRoute', 'persistedRoute')}}
            return {'routePlan': dict(body, planSha256=digest(body))}
        if operation == 'route-ensure':
            assert arguments[:8] == ['--route', self.config['route'], '--requested-route', self.config['route'],
                                     '--configured-route', self.config['route'], '--persisted-route', self.config['route']]
            self.state = 'exact_ready'
            self.active = self.config['route']
            if self.interrupt == operation: raise KeyboardInterrupt()
            return {'operation': operation, 'planSha256': arguments[-1],
                    'response': {'status': 'restored'}}
        raise AssertionError(operation)


class Tests(unittest.TestCase):
    def setUp(self):
        scratch=tempfile.TemporaryDirectory();self.addCleanup(scratch.cleanup)
        checkpoint=patch.object(recovery,'CHECKPOINT',Path(scratch.name)/'checkpoint.json')
        checkpoint.start();self.addCleanup(checkpoint.stop)

    @contextlib.contextmanager
    def installed_ownership(self, repository, version):
        """Real ownership parsing and binding hashes with mapped trusted files."""
        installer = recovery.installer
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            ownership, binding_path, helper = (root/name for name in ('ownership.json', 'binding.json', 'provider.py'))
            helper.write_bytes(b'# offline bound helper\n')
            requirements=json.loads(installer.requirements_path().read_bytes())
            binding = {'uapiSha256':requirements['uapiSha256'],'sourceCommit': 'a'*40, 'productVersion': version,
                       'artifactSetSha256': 'b'*64, 'kernel': installer.platform.release(),
                       'files': {str(helper): digest_bytes(helper.read_bytes())}, 'externalFiles': {}}
            binding_path.write_bytes(installer.canonical(binding))
            record = {
                'schema': installer.RECORD_SCHEMA, 'repository': repository,
                'owner': 'RP1-GPIO-CLK-DKMS', 'client':'WsprryPi/WsprryPi', 'channel': 'development',
                'installationMethod': 'upstream-development-rollback',
                'sourceCommit': 'a'*40, 'productVersion': version, 'sourceTree': 'c'*40,
                'uapiSha256': 'd'*64, 'versionSource': 'include/rp1_gpclk/version.h',
                'versionSourceSha256': 'e'*64, 'targetKernel': installer.platform.release(),
                'compatibilityIdentity': 'v'+version+'-rp1-development',
                'routeActivation': 'disabled', 'output': 'disabled', 'qualificationClaim': False,
                **{name: 'offline-evidence' for name in (
                    'upstreamEvidence', 'rollbackRecord', 'rollbackRecordSha256',
                    'rollbackEntrypoint', 'rollbackEntrypointSha256', 'installedModulePath',
                    'installedModuleSha256', 'decompressedModuleSha256')},
                'runtime': {
                    'readinessContract': installer.RUNTIME_READINESS_CONTRACT,
                    'bindingSha256': digest_bytes(binding_path.read_bytes()),
                    'artifactSetSha256': 'b'*64, 'sourceCommit': 'a'*40,
                    'productVersion': version, 'targetKernel': installer.platform.release(),
                    'compatibilityIdentities': {r: f'v{version}-rp1-{r}' for r in ('gpio4', 'gpio20')},
                    'deploymentPlanSha256': 'c'*64, 'activationPlanSha256': 'd'*64,
                    'activationRequestId': 'offline-request', 'controllerSession': 1,
                    'controllerGeneration': 0, 'state': 'neutral_ready', 'route': None,
                    'output': 'disabled',
                },
            }
            ownership.write_bytes(installer.canonical(record))
            ownership.chmod(0o600)
            def trusted(path):
                self.assertIn(path, (ownership, binding_path, helper))
                return path.read_bytes(), path.stat()
            requirements = json.loads(installer.requirements_path().read_bytes())
            metadata = {'schemaVersion':1,'contract':'rp1-gpclk-provider-identity-v1',
                        'repository':repository,'module':'rp1-gpclk-dkms','productVersion':version,
                        'debianVersion':version+'-1', 'interfaces':requirements['interfaces'],
                        'capabilities':requirements['capabilities'],'uapiSha256':requirements['uapiSha256'],
                        'compatibilityIdentities':{r:'v'+version+'-rp1-'+r for r in ('gpio4','gpio20')}}
            def public_call(entrypoint, operation, *arguments):
                if operation == 'identity':return {'providerIdentity':metadata}
                if operation == 'status':return {'receipt':json.loads(ownership.read_bytes()),
                                                'recordSha256':digest_bytes(ownership.read_bytes())}
                raise AssertionError('unexpected provider effect: '+operation)
            with patch.object(installer,'retained_facade',return_value=helper), \
                 patch.object(installer,'invoke',side_effect=public_call), \
                 patch.object(recovery, 'OWNERSHIP', ownership), \
                 patch.object(recovery, 'BINDING', binding_path), \
                 patch.object(recovery.companion, 'trusted', side_effect=trusted):
                yield record, ownership, binding_path, helper

    def test_owned_repository_drives_real_startup_validation_and_retries(self):
        installer = recovery.installer
        original = (installer.REPOSITORY, installer.REPOSITORY_URL, installer.REPOSITORY_URL)
        for repository, version in ((installer.REPOSITORY, '0.0.1'),(installer.REPOSITORY,'12.34.56-rc.2+build')):
            for route in ('gpio4', 'gpio20'):
                with self.subTest(repository=repository, route=route), \
                     self.installed_ownership(repository, version) as (record, path, unused, helper):
                    system = System(route)
                    binding_hash = record['runtime']['bindingSha256']
                    # Bind the existing public-plan fake to the real parsed ownership.
                    system.record = record
                    system.plan['bindingSha256'] = binding_hash
                    system.plan_hash = digest(system.plan)
                    fake_call = system.call
                    def call(operation, arguments=()):
                        value = fake_call(operation, arguments)
                        if operation == 'inspect':
                            value['identities']['installedBinding']['sha256'] = binding_hash
                        if operation == 'route-plan':
                            plan = value['routePlan']
                            plan['bindingSha256'] = binding_hash
                            plan['planSha256'] = digest({k: v for k, v in plan.items() if k != 'planSha256'})
                        return value
                    original_bytes = path.read_bytes()
                    with patch.object(system, 'ownership', side_effect=recovery.Linux().ownership), \
                         patch.object(system, 'call', side_effect=call):
                        recovery.prepare(system)
                        self.assertTrue(system.env)
                        self.assertFalse(system.calls, 'startup preparation must not contact the provider')
                        self.assertEqual(recovery.reconcile(system), 'reconciled-idle')
                        calls = copy.deepcopy(system.calls)
                        self.assertEqual(recovery.reconcile(system), 'already-complete')
                        self.assertEqual(system.calls, calls)
                        self.assertEqual(system.active, route)
                        self.assertFalse(system.config['transmit'])
                        helper.write_bytes(b'changed')
                        with self.assertRaisesRegex(installer.ContractError, 'bound runtime file changed'):
                            recovery.reconcile(system)
                        self.assertEqual(system.calls, calls)
                    self.assertEqual(path.read_bytes(), original_bytes)
                    self.assertEqual((installer.REPOSITORY, installer.REPOSITORY_URL, installer.REPOSITORY_URL), original)
                    default_record, unused, reason = installer.load_ownership_record(path)
                    if repository == installer.REPOSITORY:
                        self.assertEqual(default_record, record)
                    else:
                        self.assertIsNone(default_record, 'startup must not change installer selection')
                        self.assertIn('repository differs', reason)

    def test_real_startup_rejects_unknown_authority_versions_and_binding_changes(self):
        installer = recovery.installer
        cases = [
            ('attacker/provider', '0.0.1', None),
            ('WsprryPi/RP1-DKMS', '1.0.1', None),
            ('WsprryPi/RP1-GPCLK-DKMS', '0.0.1', None),
            (installer.REPOSITORY, '0.0.1', 'productVersion'),
            (installer.REPOSITORY, '0.0.1', 'sourceCommit'),
            (installer.REPOSITORY, '0.0.1', 'artifactSetSha256'),
            (installer.REPOSITORY, '0.0.1', 'kernel'),
        ]
        for repository, version, changed in cases:
            with self.subTest(repository=repository, version=version, changed=changed), \
                 self.installed_ownership(repository, version) as (record, path, binding, unused):
                if changed:
                    value = json.loads(binding.read_bytes())
                    value[changed] = 'changed'
                    binding.write_bytes(installer.canonical(value))
                    record['runtime']['bindingSha256'] = digest_bytes(binding.read_bytes())
                    path.write_bytes(installer.canonical(record))
                system = System()
                original = path.read_bytes()
                with patch.object(system, 'ownership', side_effect=recovery.Linux().ownership):
                    recovery.prepare(system)
                    self.assertTrue(system.env)
                    with self.assertRaises(installer.ContractError): recovery.reconcile(system)
                    self.assertFalse(system.calls)
                    self.assertIsNone(system.saved)
                self.assertEqual(path.read_bytes(), original)

    def test_startup_refuses_changed_or_duplicate_ownership_before_provider_calls(self):
        installer = recovery.installer
        with self.installed_ownership(installer.REPOSITORY, '0.0.1') as (record, path, unused, helper):
            changed = copy.deepcopy(record)
            changed['sourceTree'] = 'f'*40
            with patch.object(installer, 'load_ownership_record', return_value=(changed, path.stat(), None)):
                with self.assertRaisesRegex(installer.ContractError, 'ownership changed'):
                    recovery.Linux().ownership()
            path.write_text(path.read_text()[:-1] + ',"repository":"attacker/provider"}')
            with self.assertRaisesRegex(installer.ContractError, 'duplicate JSON key'):
                recovery.Linux().ownership()

    def test_release_ownership_uses_the_same_repository_version_boundary(self):
        installer = recovery.installer
        for repository, version in ((installer.REPOSITORY, '0.0.1'),):
            with self.subTest(repository=repository), \
                 self.installed_ownership(repository, version) as (development, path, unused, helper):
                record = {name: development[name] for name in (
                    'schema', 'repository', 'owner', 'client', 'routeActivation', 'output',
                    'qualificationClaim', 'sourceCommit', 'runtime')}
                record.update(channel='release', installationMethod='debian-package',
                              tag='v'+version, manifest={'productVersion': version}, moduleArtifacts=[])
                path.write_bytes(installer.canonical(record))
                self.assertEqual(recovery.Linux().ownership(), record)
                record['manifest']['productVersion'] = '1.0.2'
                record['runtime']['productVersion'] = '1.0.2'
                record['runtime']['compatibilityIdentities'] = {
                    r: f'v1.0.2-rp1-{r}' for r in ('gpio4', 'gpio20')}
                path.write_bytes(installer.canonical(record))
                with self.assertRaisesRegex(installer.ContractError, 'runtime binding differs'):
                    recovery.Linux().ownership()

    def test_lock_contention_defers_workers_and_bounds_installer_wait(self):
        system = recovery.Linux()
        info = SimpleNamespace(st_mode=0o100600, st_uid=0, st_nlink=1)
        with patch.object(recovery.os, 'open', return_value=41), \
             patch.object(recovery.os, 'fstat', return_value=info), \
             patch.object(recovery.os, 'close') as close, \
             patch.object(recovery.fcntl, 'flock', side_effect=BlockingIOError), \
             patch.object(recovery.time, 'monotonic', side_effect=[0, 1, 0, 31]), \
             patch.object(recovery.time, 'sleep') as sleep:
            for wait in (False, True):
                with self.assertRaises(recovery.ReconciliationBusy):
                    with system.lock(wait=wait): self.fail('contended lock admitted')
            self.assertEqual(close.call_count, 2)
            sleep.assert_not_called()
        with patch.object(recovery.os, 'open', return_value=41), \
             patch.object(recovery.os, 'fstat', return_value=info), \
             patch.object(recovery.os, 'close') as close, \
             patch.object(recovery.fcntl, 'flock', side_effect=[BlockingIOError(), None]), \
             patch.object(recovery.time, 'monotonic', side_effect=[0, 1]), \
             patch.object(recovery.time, 'sleep') as sleep:
            with system.lock(wait=True): pass
            close.assert_called_once_with(41)
            sleep.assert_called_once_with(0.1)

    def test_installation_restores_both_configured_routes_and_retries(self):
        for route in ('gpio4', 'gpio20'):
            for boundary in (None, 'route-ensure', 'complete'):
                with self.subTest(route=route, boundary=boundary):
                    system = System(route)
                    system.state = 'neutral_ready'
                    system.interrupt = boundary
                    if boundary:
                        with self.assertRaises(KeyboardInterrupt):
                            recovery.restore_installation(system)
                    system.interrupt = None
                    self.assertIn('verified', recovery.restore_installation(system))
                    self.assertEqual(system.active, route)
                    self.assertFalse(system.config['transmit'])
                    self.assertEqual(system.saved['bindingSha256'], 'a'*64)
                    self.assertFalse(any(name.startswith('activation-') for name, _ in system.calls))
                    # A completed install checkpoint is never a bypass: retry
                    # must inspect provider state again, then use a fresh plan.
                    count = len(system.calls)
                    recovery.restore_installation(system)
                    self.assertGreater(len(system.calls), count)

    def test_installation_ignores_other_backends_without_provider_calls(self):
        for backend in ('gpio', 'si5351'):
            system = System()
            system.config['backend'] = backend
            system.record = None
            recovery.restore_installation(system)
            self.assertFalse(system.calls)
            self.assertIsNone(system.saved)

    def test_installation_rejects_unsafe_or_unproven_initial_state(self):
        changes = (
            lambda s: s.config.update(transmit=True),
            lambda s: s.config.update(route='gpio17'),
            lambda s: setattr(s, 'record', None),
            lambda s: setattr(s, 'state', 'conflict'),
            lambda s: setattr(s, 'state', 'recovery_required'),
            lambda s: setattr(s, 'state', 'activation_required'),
            lambda s: setattr(s, 'active', 'gpio20'),
            lambda s: setattr(s, 'saved', {'phase': 'pending'}),
            lambda s: s.service_state.update(ActiveState='inactive'),
            lambda s: s.service_state.update(UnitFileState='masked'),
            lambda s: s.record['runtime'].update(bindingSha256='c'*64),
        )
        for mutate in changes:
            system = System(); system.state = 'neutral_ready'; mutate(system)
            with self.subTest(mutate=mutate), self.assertRaises(recovery.installer.ContractError):
                recovery.restore_installation(system)
            self.assertFalse(any(name == 'route-ensure' for name, _ in system.calls))

    def test_installation_rejects_tampered_plans_and_concurrent_changes(self):
        changes = (
            lambda s, r: r['routePlan'].update(planSha256='0'*64),
            lambda s, r: r['routePlan'].update(bindingSha256='c'*64),
            lambda s, r: r['routePlan'].update(route='gpio20'),
            lambda s, r: s.config.update(route='gpio20'),
            lambda s, r: s.config.update(transmit=True),
            lambda s, r: s.record['runtime'].update(bindingSha256='c'*64),
            lambda s, r: s.service_state.update(ActiveState='inactive'),
            lambda s, r: setattr(s, 'current_boot', 'changed'),
        )
        for mutate in changes:
            system = System(); system.state = 'neutral_ready'
            call = system.call
            def changed(operation, arguments=()):
                result = call(operation, arguments)
                if operation == 'route-plan': mutate(system, result)
                return result
            with patch.object(system, 'call', changed), self.assertRaises(recovery.installer.ContractError):
                recovery.restore_installation(system)
            self.assertFalse(any(name == 'route-ensure' for name, _ in system.calls))

    def test_installation_requires_exact_final_evidence_and_idle_application(self):
        changes = (
            lambda s, r: r.update(result='neutral_ready'),
            lambda s, r: r['routes'].update(active='gpio20'),
            lambda s, r: r['routes'].pop('persisted'),
            lambda s, r: r['safety'].update(owner=True),
            lambda s, r: r['safety'].update(lease=True),
            lambda s, r: r['safety'].update(clock='unknown'),
            lambda s, r: r['safety'].update(operationalReady=False),
            lambda s, r: r['manager']['query']['state'].update(outputEnabled=True),
            lambda s, r: r['identities']['installedBinding'].update(sha256='c'*64),
            lambda s, r: s.config.update(transmit=True),
            lambda s, r: s.service_state.update(ActiveState='inactive'),
        )
        for mutate in changes:
            system = System(); system.state = 'neutral_ready'
            call = system.call
            def changed(operation, arguments=()):
                result = call(operation, arguments)
                if operation == 'inspect' and system.active: mutate(system, result)
                return result
            with patch.object(system, 'call', changed), self.assertRaises(recovery.installer.ContractError):
                recovery.restore_installation(system)
            self.assertIsNone(system.saved)

    def test_replaced_binding_does_not_reuse_terminal_reboot_checkpoint(self):
        system = System(); recovery.reconcile(system)
        system.saved['bindingSha256'] = 'e'*64
        system.state = 'neutral_ready'
        system.active = None
        self.assertEqual(recovery.reconcile(system), 'no-reboot-reconciliation')
        self.assertEqual(system.saved['bindingSha256'], 'e'*64, 'retain old evidence until success')
        recovery.restore_installation(system)
        self.assertEqual(system.active, 'gpio4')
        self.assertEqual(system.saved['bindingSha256'], 'a'*64)

    def test_normal_reboot_recovers_only_each_explicit_selected_route(self):
        for route in ('gpio4', 'gpio20'):
            with self.subTest(route=route):
                system = System(route)
                recovery.prepare(system)
                self.assertTrue(system.env)
                self.assertEqual(system.calls, [], 'ExecStartPre must never query the manager socket')
                self.assertEqual(recovery.reconcile(system), 'reconciled-idle')
                self.assertEqual(system.active, route)
                self.assertEqual(system.saved['phase'], 'complete')
                calls = copy.deepcopy(system.calls)
                self.assertEqual(recovery.reconcile(system), 'already-complete')
                self.assertEqual(system.calls, calls)
                self.assertFalse(system.config['transmit'])

    def test_neutral_reboot_and_completed_removal_do_not_select_a_route(self):
        for removed in (False, True):
            system = System(history=False)
            if removed:
                system.plan['rebootEvidence']['transactions']['transaction.json'] = {'phase': 'recovered-inhibited', 'target': 1}
                system.plan_hash = digest(system.plan)
            recovery.reconcile(system)
            self.assertIsNone(system.active)
            self.assertFalse(any(name.startswith('route-') for name, unused in system.calls))

    def test_blocked_history_reports_provider_refusal_without_planning(self):
        for state in ('recovery_required', 'conflict'):
            system = System()
            system.state = state
            with self.assertRaisesRegex(recovery.installer.ContractError,
                                        'provider refuses reboot activation: '+state):
                recovery.reconcile(system)
            self.assertEqual([name for name, unused in system.calls], ['inspect'])
            self.assertIsNone(system.saved)

    def test_saved_route_mismatch_stopped_masked_and_transmission_are_preserved(self):
        changes = (
            lambda s: s.config.update(route='gpio20'),
            lambda s: s.config.update(transmit=True),
            lambda s: s.service_state.update(ActiveState='inactive'),
            lambda s: s.service_state.update(LoadState='masked', UnitFileState='masked'),
            lambda s: s.record['runtime'].update(bindingSha256='c'*64),
            lambda s: setattr(s, 'plan_hash', '0'*64),
            lambda s: s.plan.update(version=2),
        )
        for mutate in changes:
            with self.subTest(mutate=mutate):
                system = System(); mutate(system)
                with self.assertRaises(recovery.installer.ContractError): recovery.reconcile(system)
                self.assertFalse(any(name.endswith('-ensure') for name, unused in system.calls))

    def test_each_worker_checkpoint_boundary_resumes_without_changing_route(self):
        for route in ('gpio4', 'gpio20'):
            for boundary in ('pending', 'activation-ensure', 'route-ensure', 'complete'):
                with self.subTest(route=route, boundary=boundary):
                    system = System(route); system.interrupt = boundary
                    with self.assertRaises(KeyboardInterrupt): recovery.reconcile(system)
                    recovery.prepare(system)
                    system.interrupt = None
                    recovery.reconcile(system)
                    self.assertEqual(system.active, route)
                    self.assertEqual(system.saved['phase'], 'complete')
                    self.assertFalse(system.config['transmit'])

    def test_checkpoint_never_adopts_new_boot_binding_route_or_failed_activation(self):
        changes = (
            lambda s: setattr(s, 'current_boot', '00000000-0000-0000-0000-000000000003'),
            lambda s: s.record['runtime'].update(bindingSha256='c'*64),
            lambda s: s.config.update(route='gpio20'),
            lambda s: setattr(s, 'state', 'recovery_required'),
        )
        for mutate in changes:
            with self.subTest(mutate=mutate):
                system = System(); system.interrupt = 'pending'
                with self.assertRaises(KeyboardInterrupt): recovery.reconcile(system)
                system.interrupt = None; system.calls.clear(); mutate(system)
                with self.assertRaises(recovery.installer.ContractError): recovery.reconcile(system)
                self.assertFalse(any(name.endswith('-ensure') for name, unused in system.calls))

    def test_alternate_backend_and_same_boot_neutral_do_not_select_route(self):
        system = System(); system.config['backend'] = 'si5351'
        self.assertEqual(recovery.reconcile(system), 'not-applicable')
        self.assertFalse(system.calls)
        system = System(); system.journal['plan']['bootId'] = system.boot()
        self.assertEqual(recovery.reconcile(system), 'no-reboot-reconciliation')
        self.assertFalse(any(name.endswith('-ensure') for name, unused in system.calls))

    def test_preparation_preserves_boot_policy_and_requires_no_socket(self):
        source = (Path(__file__).resolve().parents[2]/'src/config_handler.cpp').read_text()
        override = source.split('if (std::getenv("WSPRRYPI_ROUTE_RESTORE_IDLE") != nullptr ||', 1)[1].split('switch (config.enable_on_boot)', 1)[0]
        self.assertIn('WSPRRYPI_RP1_REBOOT_IDLE', override)
        self.assertIn('config.transmit = false', override)
        self.assertNotIn('config.enable_on_boot =', override)
        for old in (True, False):
            system = System(); system.config['transmit'] = old
            recovery.prepare(system)
            self.assertTrue(system.env)
            self.assertEqual(system.config['transmit'], old)
            self.assertFalse(system.calls)

    def test_bootstrap_flag_does_not_hide_current_route_restoration_acknowledgement(self):
        system = System(); system.interrupt = 'activation-ensure'
        with self.assertRaises(KeyboardInterrupt): recovery.reconcile(system)
        with patch.object(system, 'activation_journal', side_effect=lambda name='activation.json':
                system.journal if name == 'activation.json' else {'boot': system.boot(), 'phase': 'start-intent'}):
            recovery.prepare(system)
        self.assertFalse(system.env)
        source = (Path(__file__).resolve().parents[2]/'src/scheduling_runtime.cpp').read_text()
        self.assertIn('restoration && std::getenv("WSPRRYPI_RP1_REBOOT_IDLE") == nullptr', source)
        self.assertIn('!acknowledge_rp1_restoration(restoration, config.transmit)', source)

    def test_two_reboots_each_require_new_boot_plan_and_route_request(self):
        for route in ('gpio4', 'gpio20'):
            system = System(route)
            recovery.reconcile(system)
            first = copy.deepcopy(system.saved)
            system.current_boot = '00000000-0000-0000-0000-000000000003'
            system.plan['bootId'] = system.current_boot
            system.plan_hash = digest(system.plan)
            system.state = 'activation_required'
            system.active = None
            recovery.prepare(system)
            self.assertTrue(system.env)
            recovery.reconcile(system)
            self.assertNotEqual(first['activationPlanSha256'], system.saved['activationPlanSha256'])
            self.assertNotEqual(first['bootId'], system.saved['bootId'])
            self.assertEqual(system.active, route)

    def test_startup_installer_upgrade_removal_and_interrupted_retry(self):
        import install_runtime_reconcile as installation
        for boundary in (None, 0, 1, 2):
            with self.subTest(boundary=boundary), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                (root/'source').write_bytes(b'first')
                targets = {str(root/('installed'+str(i))): ('source', 0o644) for i in range(3)}
                state = root/'state.json'
                def read(path): return path.read_bytes() if path.exists() else None
                replace = installation.os.replace
                calls = []
                def interrupted(source, target):
                    replace(source, target)
                    if str(target) in targets:
                        calls.append(str(target))
                        if len(calls)-1 == boundary: raise KeyboardInterrupt()
                with patch.object(installation, 'ROOT', root), patch.object(installation, 'STATE', state), \
                     patch.object(installation, 'FILES', targets), patch.object(installation, 'read', read), \
                     patch.object(installation.subprocess, 'run') as command:
                    with patch.object(installation.os, 'replace', interrupted):
                        if boundary is None: installation.apply()
                        else:
                            with self.assertRaises(KeyboardInterrupt): installation.apply()
                    installation.apply()
                    self.assertTrue(all(Path(p).read_bytes() == b'first' for p in targets))
                    (root/'source').write_bytes(b'upgrade')
                    installation.apply()
                    self.assertTrue(all(Path(p).read_bytes() == b'upgrade' for p in targets))
                    installation.apply(remove=True)
                    installation.apply(remove=True)
                    self.assertTrue(all(not Path(p).exists() for p in targets))
                    self.assertTrue(all(call.args[0] in (['/usr/bin/systemctl', 'daemon-reload'], ['/usr/bin/systemctl','is-failed','--quiet','wsprrypi-rp1-reconcile.service']) for call in command.call_args_list))
                    self.assertFalse(state.exists())

    def test_absent_startup_removal_creates_no_coordination_or_state(self):
        import install_runtime_reconcile as installation
        with tempfile.TemporaryDirectory() as raw:
            root=Path(raw);state=root/'absent/state.json';target=str(root/'absent/installed')
            read=lambda p:p.read_bytes() if p.exists() else None
            with patch.object(installation,'STATE',state),patch.object(installation,'FILES',{target:('source',0o644)}),patch.object(installation,'read',read),patch.object(installation.os,'geteuid',return_value=0),patch.object(installation.recovery.Linux,'lock') as lock,patch.object(installation.subprocess,'run') as command:
                installation.install(remove=True);installation.apply(remove=True)
                lock.assert_not_called();command.assert_not_called();self.assertFalse(state.parent.exists())

    def test_terminal_checkpoint_retirement_and_incomplete_refusal(self):
        import install_runtime_reconcile as installation
        for phase in ('complete','pending','malformed','unsafe-mode','unreadable'):
            with self.subTest(phase=phase), tempfile.TemporaryDirectory() as directory:
                root=Path(directory);(root/'source').write_bytes(b'owned')
                state=root/'state.json';target=root/'installed';checkpoint=root/'checkpoint.json'
                value={'schema':recovery.SCHEMA,'bootId':'00000000-0000-0000-0000-000000000001',
                       'bindingSha256':'a'*64,'activationPlanSha256':'b'*64,'route':'gpio4','phase':'complete' if phase=='unsafe-mode' else phase}
                def read(p):
                    if phase=='unreadable' and p==checkpoint:raise ValueError('foreign checkpoint')
                    return p.read_bytes() if p.exists() else None
                with patch.object(installation,'ROOT',root),patch.object(installation,'STATE',state), \
                     patch.object(installation,'FILES',{str(target):('source',0o644)}), \
                     patch.object(installation,'read',read),patch.object(recovery,'CHECKPOINT',checkpoint), \
                     patch.object(installation.subprocess,'run'):
                    installation.apply();checkpoint.write_text(json.dumps(value));checkpoint.chmod(0o644 if phase=='unsafe-mode' else 0o600)
                    if phase=='complete':
                        installation.apply(remove=True)
                        self.assertFalse(checkpoint.exists());self.assertFalse(state.exists())
                        self.assertFalse(target.exists())
                    else:
                        before=state.read_bytes()
                        with self.assertRaises((ValueError,recovery.installer.ContractError)):
                            installation.apply(remove=True)
                        self.assertEqual(state.read_bytes(),before);self.assertTrue(target.exists())
                        self.assertEqual(json.loads(checkpoint.read_text()),value)

    def test_startup_inventory_can_grow_without_adopting_foreign_files(self):
        import install_runtime_reconcile as installation
        with tempfile.TemporaryDirectory() as raw:
            root=Path(raw);(root/'source').write_bytes(b'owned')
            state=root/'state.json';first=str(root/'first');added=str(root/'requirements')
            read=lambda p:p.read_bytes() if p.exists() else None
            with patch.object(installation,'ROOT',root),patch.object(installation,'STATE',state),patch.object(installation,'read',read),patch.object(installation.subprocess,'run'):
                with patch.object(installation,'FILES',{first:('source',0o644)}):installation.apply()
                original=state.read_bytes();Path(added).write_bytes(b'foreign')
                with patch.object(installation,'FILES',{first:('source',0o644),added:('source',0o644)}):
                    with self.assertRaisesRegex(ValueError,'differs from recorded ownership'):installation.apply()
                    self.assertEqual(state.read_bytes(),original);self.assertEqual(Path(added).read_bytes(),b'foreign')
                    Path(added).unlink();installation.apply()
                    self.assertEqual(Path(added).read_bytes(),b'owned')
                    installation.apply(remove=True)
                    self.assertFalse(Path(first).exists());self.assertFalse(Path(added).exists());self.assertFalse(state.exists())

    def test_provider_replacement_or_conflict_starts_diagnostics_idle_without_administration(self):
        for error in (FileNotFoundError('binding retired'), recovery.installer.ContractError('ownership differs')):
            system = System()
            with patch.object(system, 'ownership', side_effect=error):
                recovery.prepare(system)
                self.assertTrue(system.env)
                with self.assertRaises(type(error)): recovery.reconcile(system)
            self.assertFalse(system.calls)
        system = System()
        with patch.object(system, 'bootstrap_capable', side_effect=ValueError('old binary')):
            with self.assertRaises(ValueError): recovery.prepare(system)
        self.assertIsNone(system.env)

    def test_duplicate_or_malformed_checkpoint_is_rejected(self):
        with self.assertRaises(recovery.installer.ContractError): recovery.strict_json('{"a":1,"a":2}')
        for value in ({}, {'schema': recovery.SCHEMA}):
            with self.assertRaises(recovery.installer.ContractError): recovery.validate_checkpoint(value)


if __name__ == '__main__': unittest.main()
