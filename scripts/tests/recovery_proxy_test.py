# SPDX-License-Identifier: MIT
"""Managed proxy-port changes and interrupted reload using fake service calls."""
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'src'))
from application_recovery import archive, proxy


class RecoveryProxyTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='recovery-proxy-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.job = self.root / 'job';self.job.mkdir(mode=0o700)
        self.sites = self.root / 'apache'
        for name in ('sites-available', 'sites-enabled'):
            (self.sites / name).mkdir(parents=True)
        self.site = self.sites / 'sites-available/default.conf'
        self.before = ('<VirtualHost *:80>\n' + proxy.START + '\n'
                       '    ProxyPass /wsprrypi/api/recovery http://127.0.0.1:31415/api/recovery\n'
                       '    ProxyPass /wsprrypi/socket ws://127.0.0.1:31416/socket\n' + proxy.END + '\n'
                       '    ProxyPass /unrelated http://127.0.0.1:20000/\n</VirtualHost>\n').encode()
        self.site.write_bytes(self.before);self.site.chmod(0o640)
        (self.sites / 'sites-enabled/default.conf').symlink_to(self.site)
        self.calls = []

    def service(self, command, **kwargs):
        self.calls.append(command)
        return SimpleNamespace(returncode=0)

    def test_only_managed_upstreams_change_and_completed_poll_does_not_rewrite(self):
        proxy.reconcile(self.job, 42200, 42201, sites=self.sites, run=self.service)
        after = self.site.read_bytes()
        self.assertIn(b'http://127.0.0.1:42200/api/recovery', after)
        self.assertIn(b'ws://127.0.0.1:42201/socket', after)
        self.assertIn(b'http://127.0.0.1:20000/', after)
        self.assertEqual(self.site.stat().st_mode & 0o777, 0o640)
        self.assertEqual(self.calls, [['/usr/sbin/apache2ctl', 'configtest'], ['/usr/bin/systemctl', 'reload', 'apache2.service']])
        before = self.site.stat().st_mtime_ns
        proxy.reconcile(self.job, 42200, 42201, sites=self.sites, run=self.service)
        self.assertEqual(before, self.site.stat().st_mtime_ns)
        self.assertEqual(len(self.calls), 2)

    def test_reload_failure_retains_original_and_can_resume_without_replacing_again(self):
        def fail(command, **kwargs):
            return SimpleNamespace(returncode=int(command[0] == '/usr/bin/systemctl'))
        with self.assertRaises(archive.RecoveryError):
            proxy.reconcile(self.job, 42200, 42201, sites=self.sites, run=fail)
        self.assertEqual((self.job / 'proxy-0.before').read_bytes(), self.before)
        stamp = self.site.stat().st_mtime_ns
        proxy.reconcile(self.job, 42200, 42201, sites=self.sites, run=self.service)
        self.assertEqual(stamp, self.site.stat().st_mtime_ns)

    def test_admin_changes_and_external_symlink_are_not_overwritten(self):
        def fail(*args, **kwargs):
            raise OSError('synthetic interruption before successful reload')
        with self.assertRaises(OSError):
            proxy.reconcile(self.job, 42200, 42201, sites=self.sites, run=fail)
        self.site.write_bytes(b'Administrator replacement\n')
        with self.assertRaises(archive.RecoveryError):
            proxy.reconcile(self.job, 42200, 42201, sites=self.sites, run=self.service)
        self.assertEqual(self.site.read_bytes(), b'Administrator replacement\n')


if __name__ == '__main__':
    unittest.main()
