#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Application-owned, idle-only RP1 reboot reconciliation through public plans.

The worker runs in its own systemd service because provider activation stops and
restarts WsprryPi. Provider journals prove history, never persisted route policy.
"""
import contextlib
import fcntl
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import time
import uuid

import route_application as companion
import rp1_gpclk_dkms_install as installer

OWNERSHIP = Path('/var/lib/wsprrypi/rp1-gpclk-dkms-installation.json')
CHECKPOINT = Path('/var/lib/wsprrypi/rp1-runtime-reconcile.json')
ENVIRONMENT = Path('/run/wsprrypi-rp1/startup.env')
LOCK = Path('/run/lock/wsprrypi-rp1-reconcile.lock')
BINDING = Path('/etc/rp1-gpclk-dkms/runtime-controller.json')
SCHEMA = 'wsprrypi-rp1-reboot-reconcile-v1'
require = installer.require


class ReconciliationBusy(Exception):
    """Another application-owned reconciliation already holds the lock."""


def strict_json(data):
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, 'duplicate JSON key')
            result[key] = value
        return result
    return json.loads(data, object_pairs_hook=pairs)


def validate_checkpoint(value):
    require(isinstance(value, dict) and set(value) ==
        {'schema', 'bootId', 'bindingSha256', 'route', 'activationPlanSha256', 'phase'},
        'invalid reboot reconciliation checkpoint')
    require(value['schema'] == SCHEMA and value['phase'] in ('pending', 'complete'),
            'unsupported reboot reconciliation checkpoint')
    uuid.UUID(value['bootId'])
    require(value['route'] in (None, 'gpio4', 'gpio20'), 'invalid saved reboot route')
    for name in ('bindingSha256', 'activationPlanSha256'):
        require(isinstance(value[name], str) and installer.SHA256.fullmatch(value[name]),
                'invalid reboot checkpoint identity')
    return value


class Linux:
    def __init__(self):
        self.runner = installer.Runner(False)

    def bootstrap_capable(self):
        companion.inspect_service()
        require(b'WSPRRYPI_RP1_REBOOT_IDLE' in companion.trusted(companion.BINARY)[0],
                'application lacks separate reboot-idle startup support')

    def ownership(self):
        if not OWNERSHIP.exists() and not OWNERSHIP.is_symlink():
            return None
        strict_json(companion.trusted(OWNERSHIP)[0])
        record, unused, reason = installer.load_ownership_record(OWNERSHIP)
        require(record is not None, 'cannot prove provider ownership: '+str(reason))
        if record['schema'] != installer.RUNTIME_RECORD_SCHEMA:
            return None
        raw, unused = companion.trusted(BINDING)
        binding = strict_json(raw)
        runtime = record['runtime']
        require(installer.sha256_bytes(raw) == runtime['bindingSha256'] and
                binding['sourceCommit'] == runtime['sourceCommit'] and
                binding['artifactSetSha256'] == runtime['artifactSetSha256'] and
                binding['kernel'] == os.uname().release,
                'runtime binding differs from application ownership')
        # Authenticate code and imports before invoking the privileged facade.
        for path, expected in {**binding['files'], **binding['externalFiles']}.items():
            require(installer.sha256_bytes(companion.trusted(Path(path))[0]) == expected,
                    'bound runtime file changed: '+path)
        return record

    def boot(self):
        return Path('/proc/sys/kernel/random/boot_id').read_text().strip()

    def activation_journal(self, name='activation.json'):
        try:
            raw = companion.trusted(Path('/var/lib/rp1-gpclk-dkms/runtime-admin') / name)[0]
        except FileNotFoundError:
            return None
        return strict_json(raw)

    def configuration(self):
        parsed = companion.configuration(companion.trusted(companion.CONFIG)[0])
        return {'backend': parsed.get('Operation', 'Transmit Backend').lower(),
                'route': 'gpio'+str(parsed.getint('GPIO', 'Transmit Pin')),
                'transmit': parsed.getboolean('Operation', 'Transmit')}

    def checkpoint(self):
        try:
            raw, info = companion.trusted(CHECKPOINT)
        except FileNotFoundError:
            return None
        require(stat.S_IMODE(info.st_mode) == 0o600 and info.st_nlink == 1,
                'unsafe reboot checkpoint')
        return validate_checkpoint(strict_json(raw))

    def save(self, value):
        self.checkpoint()  # Refuse to replace foreign or malformed evidence.
        installer.secure_record_parent(CHECKPOINT)
        installer.atomic_json(CHECKPOINT, validate_checkpoint(value))

    def call(self, operation, arguments=()):
        value = installer.runtime_call(self.runner, installer.RUNTIME_PROVIDER,
            operation, arguments, {'activation_required', 'neutral_ready', 'exact_ready',
                                  'recovery_required', 'conflict'})
        require(value.get('contract') == installer.RUNTIME_READINESS_CONTRACT,
                'provider readiness contract differs')
        return value

    def service(self):
        raw = subprocess.check_output(['/usr/bin/systemctl', 'show', 'wsprrypi.service',
            '--property=ActiveState,LoadState,UnitFileState'], text=True, timeout=10)
        return dict(line.split('=', 1) for line in raw.splitlines())

    def environment(self, idle):
        # systemd creates this root-owned RuntimeDirectory before ExecStartPre.
        companion.trusted(Path(__file__))
        installer.secure_record_parent(ENVIRONMENT)
        if ENVIRONMENT.exists() or ENVIRONMENT.is_symlink():
            companion.trusted(ENVIRONMENT)
        fd, name = tempfile.mkstemp(prefix='.startup-', dir=ENVIRONMENT.parent)
        try:
            with os.fdopen(fd, 'wb') as stream:
                os.fchmod(stream.fileno(), 0o600)
                stream.write(b'WSPRRYPI_RP1_REBOOT_IDLE=1\n' if idle else b'')
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(name, ENVIRONMENT)
        finally:
            if os.path.exists(name): os.unlink(name)

    @contextlib.contextmanager
    def lock(self, wait=False):
        fd = os.open(LOCK, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
        try:
            info = os.fstat(fd)
            require(stat.S_ISREG(info.st_mode) and info.st_uid == 0 and
                    stat.S_IMODE(info.st_mode) == 0o600 and info.st_nlink == 1,
                    'unsafe reconciliation lock')
            deadline = time.monotonic() + (30 if wait else 0)
            while True:
                try:
                    fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    break
                except BlockingIOError:
                    if time.monotonic() >= deadline:
                        raise ReconciliationBusy() from None
                    time.sleep(0.1)
            yield
        finally:
            os.close(fd)


def bound_inspection(system, record):
    value = system.call('inspect')
    identity = value.get('identities', {}).get('installedBinding', {})
    require(identity.get('status') == 'valid' and
            identity.get('sha256') == record['runtime']['bindingSha256'] and
            identity.get('value', {}).get('artifactSetSha256') == record['runtime']['artifactSetSha256'],
            'provider inspection differs from owned installation')
    return value


def restore_installation(system):
    """Restore explicit persisted RP1 intent after verified neutral installation.

    This is an installer action, never a general startup default. The public
    provider owns the overlay, module and idle application transaction.
    """
    with system.lock(wait=True):
        config = system.configuration()
        if config['backend'] != 'rp1-gpclk':
            return 'configured backend does not require an RP1 route'
        route = config['route']
        require(route in ('gpio4', 'gpio20'), 'invalid configured RP1 route')
        require(config['transmit'] is False,
                'installation route restoration requires disabled transmission')
        record = system.ownership()
        require(record is not None, 'installation route restoration requires owned runtime')
        boot = system.boot()
        checkpoint = system.checkpoint()
        require(not checkpoint or checkpoint['phase'] == 'complete',
                'unfinished reboot reconciliation requires recovery before installation')
        service = system.service()
        require(service.get('LoadState') == 'loaded' and
                service.get('ActiveState') == 'active' and
                service.get('UnitFileState') not in ('masked', 'masked-runtime'),
                'installation route restoration preserves stopped or masked application')
        initial = bound_inspection(system, record)
        require(initial.get('result') in ('neutral_ready', 'exact_ready'),
                'provider refuses installation route restoration: '+str(initial.get('result')))
        require(initial.get('routes', {}).get('active') in (None, route),
                'active RP1 route differs from configured installation route')
        require(initial.get('safety', {}).get('owner') is False and
                initial.get('safety', {}).get('lease') is False,
                'installation route restoration requires an unowned provider')
        args = ['--route', route, '--requested-route', route,
                '--configured-route', route, '--persisted-route', route]
        planned = system.call('route-plan', args)
        plan = planned.get('routePlan', {})
        digest = plan.get('planSha256')
        body = {key: value for key, value in plan.items() if key != 'planSha256'}
        require(digest == installer.sha256_bytes(installer.canonical(body)) and
                plan.get('version') == 1 and plan.get('operation') == 'select' and
                plan.get('bindingSha256') == record['runtime']['bindingSha256'] and
                all(plan.get(name) == route for name in
                    ('route', 'requestedRoute', 'configuredRoute', 'persistedRoute')),
                'installation route plan differs from configured route or owned binding')
        require(system.configuration() == config and system.ownership() == record and
                system.boot() == boot and system.service() == service,
                'installation configuration, ownership, boot or service changed during planning')
        response = system.call('route-ensure', [*args, '--plan-sha256', digest])
        require(response.get('operation') == 'route-ensure' and
                response.get('planSha256') == digest and
                response.get('response', {}).get('status') in ('restored', 'idempotent-ready'),
                'installation route transaction did not restore the idle application')
        final = system.call('inspect', args[2:])
        identity = final.get('identities', {}).get('installedBinding', {})
        require(final.get('result') == 'exact_ready' and
                all(final.get('routes', {}).get(name) == route for name in
                    ('requested', 'configured', 'persisted', 'active')) and
                identity.get('status') == 'valid' and
                identity.get('sha256') == record['runtime']['bindingSha256'] and
                identity.get('value', {}).get('artifactSetSha256') == record['runtime']['artifactSetSha256'] and
                final.get('safety', {}).get('owner') is False and
                final.get('safety', {}).get('lease') is False and
                final.get('safety', {}).get('operationalReady') is True and
                all(final.get('safety', {}).get(name) == 'quiescent' for name in
                    ('clock', 'gpio', 'dma')) and
                final.get('manager', {}).get('query', {}).get('state', {}).get('outputEnabled') is False,
                'installed RP1 route did not reach exact unowned readiness')
        require(system.configuration() == config and system.ownership() == record and
                system.boot() == boot and system.service() == service,
                'installation state changed during route restoration')
        system.save({'schema': SCHEMA, 'bootId': boot,
            'bindingSha256': record['runtime']['bindingSha256'], 'route': route,
            'activationPlanSha256': record['runtime']['activationPlanSha256'],
            'phase': 'complete'})
        return 'Configured RP1 '+route.upper()+' route verified; transmission remains disabled.'


def journal_of(value):
    return value.get('activation', {}).get('value', {}).get('activationJournal')


def prepare(system):
    """Keep the application available and idle while provider state is unavailable."""
    system.bootstrap_capable()
    try:
        prepare_environment(system)
    except (OSError, ValueError, KeyError, TypeError, installer.ContractError) as error:
        # Inverse deployment removes the binding before restoring the service.
        # Unproven provider state must block administration, not its diagnostics UI.
        system.environment(True)
        print('RP1 provider unavailable; starting application idle: '+str(error), file=sys.stderr)


def prepare_environment(system):
    """Set only the startup idle environment; inspection does not retire records."""
    record = system.ownership()
    if record is None or system.configuration()['backend'] != 'rp1-gpclk':
        system.environment(False)
        return
    # Never query the socket here: this runs while the provider may be
    # waiting for systemctl start with its mutation lock held.
    journal = system.activation_journal()
    checkpoint = system.checkpoint()
    pending = bool(checkpoint and checkpoint['phase'] == 'pending')
    historical = bool(journal and journal['plan']['bootId'] != system.boot())
    application = system.activation_journal('application.json')
    current_route_restoration = bool(application and application.get('boot') == system.boot() and
        application.get('phase') in ('configure-intent', 'start-intent', 'restored', 'stopped', 'administrator-masked'))
    neutral_activation = bool(journal and journal.get('phase') == 'application-restore-intent')
    system.environment(historical or neutral_activation or (pending and not current_route_restoration))


def selected_history(plan, route):
    require(plan.get('version') == 3 and plan.get('activationContext') == 'post-reboot',
            'provider lacks supported terminal reboot reconciliation')
    tx = plan['rebootEvidence']['transactions']['transaction.json']
    if tx is None or tx['phase'] == 'recovered-inhibited':
        return None
    require(tx['phase'] == 'complete-inhibited' and
            {1: 'gpio4', 2: 'gpio20'}.get(tx['target']) == route,
            'persisted route differs from completed prior-boot selection')
    return route


def reconcile(system):
    with system.lock():
        record = system.ownership()
        config = system.configuration()
        if record is None or config['backend'] != 'rp1-gpclk':
            return 'not-applicable'
        boot = system.boot()
        checkpoint = system.checkpoint()
        if checkpoint and checkpoint['phase'] == 'complete':
            if (checkpoint['bootId'] == boot and
                    checkpoint['bindingSha256'] == record['runtime']['bindingSha256']):
                return 'already-complete'
            # A terminal record from a replaced installation proves no current
            # reconciliation. Preserve it on disk until new work succeeds.
            checkpoint = None
        if checkpoint and checkpoint['phase'] == 'pending':
            require(checkpoint['bootId'] == boot and
                    checkpoint['bindingSha256'] == record['runtime']['bindingSha256'],
                    'interrupted reboot reconciliation belongs to another boot or installation')
        service = system.service()
        require(service.get('ActiveState') == 'active' and
                service.get('LoadState') == 'loaded' and
                service.get('UnitFileState') not in ('masked', 'masked-runtime'),
                'reconciliation preserves stopped or administrator-masked application')
        require(config['transmit'] is False, 'reboot reconciliation requires disabled output')
        inspected = bound_inspection(system, record)
        if checkpoint is None or checkpoint['bootId'] != boot:
            journal = journal_of(inspected)
            if journal is None or journal['plan']['bootId'] == boot:
                return 'no-reboot-reconciliation'
            require(inspected.get('result') == 'activation_required',
                    'provider refuses reboot activation: '+str(inspected.get('result')))
            planned = system.call('activation-plan')
            plan = planned.get('activationPlan', {})
            digest = plan.get('planSha256')
            body = {key: value for key, value in plan.items() if key != 'planSha256'}
            require(digest == installer.sha256_bytes(installer.canonical(body)),
                    'activation plan digest differs')
            route = selected_history(plan, config['route'])
            require(plan['bootId'] == boot and plan['bindingSha256'] == record['runtime']['bindingSha256'],
                    'activation plan differs from current owned boot')
            checkpoint = {'schema': SCHEMA, 'bootId': boot,
                'bindingSha256': plan['bindingSha256'], 'route': route,
                'activationPlanSha256': digest, 'phase': 'pending'}
            system.save(checkpoint)
        require(checkpoint['route'] in (None, config['route']),
                'persisted route changed during reboot reconciliation')
        if inspected.get('result') == 'activation_required':
            planned = system.call('activation-plan').get('activationPlan', {})
            require(planned.get('planSha256') == checkpoint['activationPlanSha256'],
                    'reboot activation plan changed; preserve checkpoint')
            response = system.call('activation-ensure',
                ['--plan-sha256', checkpoint['activationPlanSha256']])
            require(response.get('operation') == 'activation-ensure' and
                    response.get('response', {}).get('status') in ('activated-neutral', 'idempotent-no-change'),
                    'neutral reboot activation did not complete')
            inspected = bound_inspection(system, record)
        journal = journal_of(inspected)
        require(journal and journal.get('planSha256') == checkpoint['activationPlanSha256'] and
                journal['phase'] == 'complete-neutral' and journal['plan']['bootId'] == boot,
                'current runtime does not descend from reviewed reboot activation')
        require(inspected.get('result') in ('neutral_ready', 'exact_ready'),
                'runtime requires explicit recovery; preserve its evidence')
        if checkpoint['route'] is not None:
            current = system.configuration()
            require(current == config, 'application route or output changed during recovery')
            route = checkpoint['route']
            args = ['--route', route, '--requested-route', route,
                    '--configured-route', route, '--persisted-route', route]
            planned = system.call('route-plan', args)
            plan = planned.get('routePlan', {})
            digest = plan.get('planSha256')
            body = {key: value for key, value in plan.items() if key != 'planSha256'}
            require(digest == installer.sha256_bytes(installer.canonical(body)) and
                    plan.get('bindingSha256') == checkpoint['bindingSha256'] and
                    plan.get('route') == route,
                    'route plan differs from saved selection')
            response = system.call('route-ensure', [*args, '--plan-sha256', digest])
            require(response.get('response', {}).get('status') in ('restored', 'stopped',
                    'administrator-masked', 'idempotent-ready'), 'route restoration did not complete')
            final = system.call('inspect', args[2:])
            require(final.get('result') == 'exact_ready' and
                    final.get('routes', {}).get('active') == route and
                    final.get('safety', {}).get('owner') is False and
                    final.get('safety', {}).get('lease') is False,
                    'restored route is not ready and unowned')
        require(system.configuration()['transmit'] is False, 'application enabled output during recovery')
        system.save(dict(checkpoint, phase='complete'))
        return 'reconciled-idle'


def main():
    require(os.geteuid() == 0, 'root required')
    require(sys.argv[1:] in (['prepare'], ['reconcile'], ['install']),
            'expected prepare, reconcile or install')
    system = Linux()
    if sys.argv[1] == 'prepare':
        prepare(system)
    elif sys.argv[1] == 'install':
        print(restore_installation(system))
    else:
        # Type=simple starts before application config loading; wait only for
        # the existing idle startup override to take effect, never edit policy.
        for attempt in range(40):
            if system.configuration()['transmit'] is False:
                break
            time.sleep(0.25)
        print(reconcile(system))


if __name__ == '__main__':
    try:
        main()
    except ReconciliationBusy:
        if sys.argv[1:] == ['reconcile']:
            # Route restoration restarts the application and launches this
            # worker while its installer/worker parent still holds the lock.
            print('RP1 reconciliation already in progress')
        else:
            sys.exit('RP1 installation route restoration timed out waiting for reconciliation')
    except (OSError, ValueError, KeyError, TypeError, installer.ContractError,
            subprocess.SubprocessError) as error:
        sys.exit('RP1 startup recovery refused: '+str(error))
