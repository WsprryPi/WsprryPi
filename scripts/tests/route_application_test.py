#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Offline tests of the installed route configuration companion."""
import os
import json
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import route_application as app

SOURCE = Path(__file__).resolve().parents[2]


class Tests(unittest.TestCase):
    def snapshot(self, **changes):
        return dict(contract=app.CONTRACT, backend='rp1-gpclk', route='gpio20', transmit=False,
                    config=str(app.STATE)+'/generation-1/configuration', **changes)

    def test_route_request_uses_backend_command_and_never_opens_state(self):
        value=self.snapshot()
        with patch.object(app,'trusted',return_value=(b'--route-maintenance',None)) as trusted, \
             patch.object(app.subprocess,'run',return_value=subprocess.CompletedProcess([],0,json.dumps(value).encode(),b'')) as run:
            self.assertEqual(app.configure('gpio20'),value)
        trusted.assert_called_once_with(app.BINARY)
        self.assertEqual(run.call_args.args[0],[str(app.BINARY),'--route-maintenance','--state-directory',str(app.STATE),'--configure-route','gpio20'])

    def test_invalid_route_refuses_before_invoking_backend(self):
        with patch.object(app,'trusted',side_effect=AssertionError('binary touched')):
            with self.assertRaises(ValueError):app.configure('gpio19')

    def test_closed_route_state_and_confirmed_outcome(self):
        valid=self.snapshot()
        invalid=[{**valid,'extra':True},{**valid,'transmit':1},{**valid,'route':'gpio4'},
                 {**valid,'backend':'gpio'},{**valid,'config':'/another/configuration'},
                 {**valid,'contract':'unknown'},None]
        for value in invalid:
            with self.subTest(value=value),patch.object(app,'trusted',return_value=(b'--route-maintenance',None)), \
                 patch.object(app.subprocess,'run',return_value=subprocess.CompletedProcess([],0,json.dumps(value).encode(),b'')):
                with self.assertRaises(ValueError):app.configure('gpio20')

    def test_failed_backend_is_not_a_file_edit_fallback(self):
        with patch.object(app,'trusted',return_value=(b'--route-maintenance',None)), \
             patch.object(app.subprocess,'run',side_effect=subprocess.CalledProcessError(2,['fixture'])):
            with self.assertRaises(subprocess.CalledProcessError):app.configure('gpio20')

    def test_online_route_read_uses_verified_ipc_and_only_unavailability_selects_stopped_admission(self):
        value=self.snapshot()
        successful=subprocess.CompletedProcess([],0,json.dumps(value).encode(),b'')
        with patch.object(app,'trusted',return_value=(b'--route-maintenance',None)), \
             patch.object(app.subprocess,'run',return_value=successful) as run:
            self.assertEqual(app.state(),value)
            self.assertEqual(run.call_args.args[0],[str(app.BINARY),'--route-observation'])
        with patch.object(app,'trusted',return_value=(b'--route-maintenance',None)), \
             patch.object(app.subprocess,'run',side_effect=[subprocess.CompletedProcess([],3,b'',b'unavailable'),successful]) as run:
            self.assertEqual(app.state(),value)
            self.assertEqual(run.call_args_list[1].args[0],[str(app.BINARY),'--route-maintenance','--state-directory',str(app.STATE)])
        with patch.object(app,'trusted',return_value=(b'--route-maintenance',None)), \
             patch.object(app.subprocess,'run',return_value=subprocess.CompletedProcess([],2,b'',b'protocol refused')) as run:
            with self.assertRaises(subprocess.CalledProcessError):app.state()
            self.assertEqual(run.call_count,1)

    def test_installed_service_layouts_and_old_binary_rejection(self):
        for suffix in ('', ' --no-web'):
            text = '{ path=/usr/local/bin/wsprrypi ; argv[]=/usr/local/bin/wsprrypi -J --state-directory /var/lib/wsprrypi/application-state'+suffix+' ; ignore_errors=no ; }\n'
            with patch.object(app.subprocess, 'check_output', return_value=text), patch.object(app, 'trusted', return_value=(b'WSPRRYPI_ROUTE_RESTORE_IDLE', None)):
                app.inspect_service()
            with patch.object(app.subprocess, 'check_output', return_value=text), patch.object(app, 'trusted', return_value=(b'old binary', None)):
                with self.assertRaises(ValueError): app.inspect_service()
        with patch.object(app.subprocess, 'check_output', return_value=''):
            with self.assertRaises(ValueError): app.inspect_service()

    def test_transient_confirmation_and_only_supported_service_options(self):
        base = '/usr/local/bin/wsprrypi -J --state-directory /var/lib/wsprrypi/application-state'
        confirmation = dict(enabled=True, route='GPIO20', operation_id='managed-operation-21',
            physical_connection_confirmed=True, attenuation_and_load_confirmed=True,
            bounded_operation_confirmed=True, non_radiating_topology_confirmed=True,
            experimental_status_acknowledged=True)
        flag = ' --rp1-development-confirmation-json '
        for encoding in (json.dumps(confirmation), json.dumps(confirmation, separators=(',', ':'))):
            for before, after in (('', ''), (' --no-web', ''), ('', ' --no-web')):
                command = base + before + flag + encoding + after
                text = '{ path=/usr/local/bin/wsprrypi ; argv[]=' + command + ' ; ignore_errors=no ; }\n'
                with patch.object(app.subprocess, 'check_output', return_value=text), patch.object(app, 'trusted', return_value=(b'WSPRRYPI_ROUTE_RESTORE_IDLE', None)):
                    app.inspect_service()
        invalid = [base+' --no-web --no-web', base+'.other', base+' --backend simulated',
                   base+flag+'null', base+flag+'{}', base+flag+json.dumps(confirmation)+' --transmit-gpio 4',
                   base+flag+json.dumps(confirmation)+flag+json.dumps(confirmation),
                   base+flag+json.dumps(confirmation)+'--no-web',
                   base+flag+json.dumps(confirmation)[:-1]+',"enabled":true}']
        for field, value in (('enabled', False), ('enabled', 1), ('route', 'GPIO19'),
                             ('operation_id', 'short'), ('operation_id', 'operation;chain'),
                             ('operation_id', 'operation with spaces'), ('unexpected', True)):
            invalid.append(base+flag+json.dumps({**confirmation, field:value}))
        for command in invalid:
            with self.subTest(command=command), self.assertRaises(ValueError):
                app.validate_service_arguments(command)
        text = '{ path=/usr/local/bin/wsprrypi ; argv[]='+base+' ; ignore_errors=no ; } { path=/bin/sh ; argv[]=/bin/sh ; }'
        with patch.object(app.subprocess, 'check_output', return_value=text), self.assertRaises(ValueError):
            app.inspect_service()

    def test_bootstrap_installer_uses_selected_checkout_and_honors_dry_run(self):
        for dry_run in ('true', 'false'):
            script = ('source "$INSTALLER"\n'
                'FGGLD= RESET= FGGRN= FGRED= MOVE_UP= CLEAR_LINE=\n'
                'python3() { printf "%s\\n" "$*" >>"$CALLS"; }\n'
                'install() { printf "%s\\n" "$*" >>"$CALLS"; }\n'+
                'ACTION=install\nLOCAL_REPO_DIR=/selected-checkout\nDRY_RUN='+dry_run+
                '\nmanage_route_application\n')
            with tempfile.TemporaryDirectory() as directory:
                calls = Path(directory)/'calls'
                result = subprocess.check_output(['bash', '-c', script], text=True, cwd='/tmp',
                    env={**os.environ, 'INSTALLER': str(SOURCE/'scripts/install.sh'), 'CALLS': str(calls)})
                self.assertIn('Install route application companion.', result)
                self.assertIn('Install runtime reconciliation support.', result)
                if dry_run == 'true':
                    self.assertFalse(calls.exists())
                else:
                    self.assertIn('/selected-checkout/scripts/route_application.py', calls.read_text())
                    self.assertIn('/usr/local/lib/wsprrypi/route_application.py', calls.read_text())


if __name__ == '__main__': unittest.main()
