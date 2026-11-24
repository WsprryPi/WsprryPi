#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Windows boundary failure policies; no native account or SCM qualification."""
import json
from contextlib import nullcontext
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'src'))
from application_recovery import archive
from application_recovery.lifecycle import BackendLifecycle
from application_recovery.state_owner import SCHEMA
from application_recovery.windows_service import ControllerService
from application_recovery.windows_backend import backend_command
from application_recovery.jobs import BackendUnavailable, BackendRefused, RecoveryBusy


class BackendReplyPolicy(unittest.TestCase):
    def api(self, reply=b'{"ready":true}', code=0):
        api = Mock()
        def start(*args):
            args[-1]._obj.value = 17
            return 0
        def poll(token, buffer, capacity, size, done, status):
            buffer.value = reply
            size._obj.value, done._obj.value, status._obj.value = len(reply), 1, code
            return 0
        api.wrn_backend_start.side_effect = start
        api.wrn_backend_poll.side_effect = poll
        api.wrn_backend_close.return_value = 0
        return api

    def test_positive_reply_is_preserved(self):
        api = self.api()
        with patch('application_recovery.native_files.library', return_value=api):
            self.assertEqual(backend_command({}, 'observe'), {'ready': True})
        api.wrn_backend_close.assert_called_once_with(17)

    def test_native_start_poll_and_cleanup_failure_are_unknown(self):
        for stage, status in (('start', 1), ('poll', 1), ('close', 1), ('close', 3)):
            with self.subTest(stage=stage, status=status):
                api = self.api()
                operation = getattr(api, 'wrn_backend_' + stage)
                operation.side_effect = None
                operation.return_value = status
                with patch('application_recovery.native_files.library', return_value=api), self.assertRaises(BackendUnavailable):
                    backend_command({}, 'observe')
                if stage == 'start':
                    api.wrn_backend_close.assert_not_called()
                else:
                    api.wrn_backend_close.assert_called_once_with(17)

    def test_malformed_reply_is_unknown(self):
        api = self.api(b'{"ready":true,"ready":false}')
        with patch('application_recovery.native_files.library', return_value=api), self.assertRaises(BackendUnavailable):
            backend_command({}, 'observe')

    def test_closed_backend_refusals_remain_refusals(self):
        for code, exception in (('busy', RecoveryBusy), ('storage_unavailable', BackendRefused)):
            api = self.api(json.dumps({'ok': False, 'error': code}).encode(), 2)
            with self.subTest(code=code), patch('application_recovery.native_files.library', return_value=api), self.assertRaises(exception):
                backend_command({}, 'observe')


class HelperReplacementPolicy(unittest.TestCase):
    def setUp(self):
        self.principal = {'schema': SCHEMA, 'service_pid': 17, 'sid': 'S-1-5-80-1-2-3-4-5',
                          'mode': 'recovery-owner', 'helper_nonce': 'a'*32, 'identity': 'b'*32}
        self.service = ControllerService.__new__(ControllerService)
        self.service.bind_owner(self.principal)
        self.service.query = Mock(return_value=(ControllerService.RUNNING, 17))
        self.service.stop, self.service.start = Mock(), Mock()
        self.lifecycle = BackendLifecycle.__new__(BackendLifecycle)
        self.lifecycle.config = {'kind': 'windows_service'}
        self.lifecycle.native_service = self.service

    def exchange(self, value):
        return patch('application_recovery.state_owner.Wire', return_value=Mock(receive=Mock(return_value=value)))

    def test_current_authenticated_helper_may_restart(self):
        with patch('application_recovery.windows_owner_pipe.OwnerPipe.connect', return_value=nullcontext()) as connection, self.exchange(self.principal):
            self.lifecycle.start()
        connection.assert_called_once_with(17)
        self.service.stop.assert_called_once_with(17)
        self.service.start.assert_called_once_with()

    def test_replaced_pid_refuses_before_connect_or_stop(self):
        self.service.query.return_value = (ControllerService.RUNNING, 18)
        with patch('application_recovery.windows_owner_pipe.OwnerPipe.connect') as connection:
            with self.assertRaises(archive.RecoveryError):
                self.lifecycle.start()
        connection.assert_not_called()
        self.service.stop.assert_not_called()
        self.service.start.assert_not_called()

    def test_reused_pid_changed_mode_nonce_or_owner_never_stops_replacement(self):
        alternatives = [dict(self.principal, mode='controller', helper_nonce=None),
                        dict(self.principal, helper_nonce='c'*32), dict(self.principal, service_pid=18),
                        dict(self.principal, sid='S-1-5-80-6-7-8-9-10'), dict(self.principal, identity='c'*32),
                        dict(self.principal, extra=True)]
        for value in alternatives:
            with self.subTest(value=value), patch('application_recovery.windows_owner_pipe.OwnerPipe.connect', return_value=nullcontext()), self.exchange(value):
                with self.assertRaises(archive.RecoveryError):
                    self.lifecycle.start()
            self.service.stop.assert_not_called()
            self.service.start.assert_not_called()


if __name__ == '__main__':
    unittest.main()
