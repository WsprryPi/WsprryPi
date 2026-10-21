#!/usr/bin/env python3
"""Offline PHP2Shell configuration tests; no PHP, daemons, root or network."""

import contextlib
from datetime import date
import importlib.util
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[1] / "php2shell-mitigate.py"
SPEC = importlib.util.spec_from_file_location("php2shell_mitigate", SCRIPT)
helper = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = helper
SPEC.loader.exec_module(helper)


class MitigationTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        (self.root / "etc").mkdir()
        (self.root / "etc/os-release").write_text('ID=debian\nVERSION_ID="12"\nVERSION_CODENAME=bookworm\nPRETTY_NAME="Debian GNU/Linux 12"\n')
        (self.root / "var/www").mkdir(parents=True)
        self.apache = self.tree("8.2", "apache2")

    def tree(self, version, sapi):
        directory = self.root / "etc/php" / version / sapi
        (directory / "conf.d").mkdir(parents=True)
        (directory / "php.ini").write_text(
            "[PHP]\nmemory_limit=128M\npost_max_size=8M\nupload_max_filesize=2M\n"
            "[Session]\nsession.upload_progress.enabled=On\n")
        return directory

    def inspect(self, directory=None, unlimited=False):
        return helper.inspect(directory or self.apache, unlimited)

    def apply(self, plans):
        with contextlib.redirect_stdout(io.StringIO()):
            helper.apply_plans(plans)

    def snapshot(self):
        return {str(p.relative_to(self.root)): (p.read_bytes(), p.stat().st_mtime_ns)
                for p in self.root.rglob("*") if p.is_file()}

    def test_audit_is_read_only_and_does_not_execute_commands(self):
        before = self.snapshot()
        with patch.object(helper.subprocess, "run") as command, contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(helper.main(["--root", str(self.root)]), 1)
        command.assert_not_called()
        self.assertIn("CHECK COMPLETE", output.getvalue())
        self.assertIn("No settings changed.", output.getvalue())
        self.assertIn("Apache: status unavailable", output.getvalue())
        self.assertNotIn("Planned override", output.getvalue())
        self.assertEqual(before, self.snapshot())

    def test_cli_offline_apply_refused_without_changes(self):
        before = self.snapshot()
        result = subprocess.run([sys.executable, "-B", str(SCRIPT), "--root", str(self.root), "--apply"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("offline roots are audit-only", result.stderr)
        self.assertEqual(before, self.snapshot())

    def test_discovery_selects_web_sapis_only(self):
        self.tree("8.2", "cli")
        fpm = self.tree("8.5", "fpm")
        cgi = self.tree("8.4", "cgi")
        self.assertEqual(set(helper.discover(self.root, None)), {self.apache, fpm, cgi})
        self.assertEqual(helper.discover(self.root, ["fpm"]), [fpm])

    def test_apply_changes_only_owned_snippet_and_is_idempotent(self):
        original = self.snapshot()
        plan, _, _ = self.inspect()
        self.assertNotIn("memory_limit", plan.settings)
        self.assertNotIn("fatal_error_backtraces", plan.settings)
        self.apply([plan])
        for name, (contents, mtime) in original.items():
            self.assertEqual((self.root / name).read_bytes(), contents)
            self.assertEqual((self.root / name).stat().st_mtime_ns, mtime)
        self.assertEqual(plan.target.stat().st_mode & 0o777, 0o644)
        before = self.snapshot()
        self.apply([self.inspect()[0]])
        self.assertEqual(before, self.snapshot())

    def test_memory_requires_opt_in_and_repeat_apply_preserves_it(self):
        plan, _, _ = self.inspect(unlimited=True)
        self.assertEqual(plan.settings["memory_limit"], "-1")
        self.apply([plan])
        repeated, values, _ = self.inspect()
        self.assertEqual(values["memory_limit"], "-1")
        self.assertEqual(repeated.contents, plan.contents)

    def test_php85_backtraces_disabled_for_each_sapi(self):
        for sapi in helper.SAPIS:
            plan, _, _ = self.inspect(self.tree("8.5", sapi))
            self.assertEqual(plan.settings["fatal_error_backtraces"], "0")

    def test_limits_preserve_stricter_values_and_correct_unlimited_post(self):
        for post, upload, expected in [("4M", "1M", (4 * helper.MIB, helper.MIB)),
                                       ("0", "64M", (8 * helper.MIB, 2 * helper.MIB)),
                                       ("1K", "2M", (1024, 512))]:
            with self.subTest(post=post):
                (self.apache / "php.ini").write_text(f"post_max_size={post}\nupload_max_filesize={upload}\n")
                plan, _, _ = self.inspect()
                self.assertEqual(tuple(int(plan.settings[key]) for key in ["post_max_size", "upload_max_filesize"]), expected)

    def test_later_ini_override_detected_after_apply(self):
        (self.apache / "conf.d/zz-local.ini").write_text("session.upload_progress.enabled=1\n")
        self.apply([self.inspect()[0]])
        _, values, _ = self.inspect()
        self.assertTrue(any("session.upload_progress.enabled" in finding for finding in helper.settings_findings(values, "8.2")))

    def test_noncanonical_request_limits_are_never_relaxed(self):
        for key, value in [("post_max_size", "010"), ("upload_max_filesize", "01M"),
                           ("post_max_size", "${POST_LIMIT}"), ("upload_max_filesize", "1M*2")]:
            with self.subTest(key=key, value=value):
                (self.apache / "php.ini").write_text(f"{key}={value}\n")
                before = self.snapshot()
                with self.assertRaisesRegex(ValueError, "Cannot safely preserve noncanonical"):
                    self.inspect()
                self.assertEqual(before, self.snapshot())

    def test_restart_guidance_is_shown_even_when_limit_inspection_fails(self):
        (self.apache / "php.ini").write_text("post_max_size=010\n")
        with contextlib.redirect_stdout(io.StringIO()) as output, contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(helper.main(["--root", str(self.root), "--verbose"]), 2)
        self.assertIn("sudo systemctl restart apache2.service", output.getvalue())
        self.assertIn("CHECK FAILED", output.getvalue())

    def test_ini_comments_quotes_duplicates_and_scoped_settings(self):
        (self.apache / "conf.d/20-test.ini").write_text(
            '; memory_limit=-1\nmemory_limit = "-1" ; mitigation\n'
            "post_max_size='4M'\npost_max_size=2M\n"
            "[PATH=/site]\nsession.upload_progress.enabled=1\n[HOST=example]\npost_max_size=0\n")
        _, values, scoped = self.inspect()
        self.assertEqual(values["memory_limit"], "-1")
        self.assertEqual(values["post_max_size"], "2M")
        self.assertEqual(len(scoped), 2)

    def test_settings_are_not_reported_as_configured_when_implicit_or_invalid(self):
        self.assertEqual(len(helper.settings_findings({}, "8.5")), 5)
        for value in ["Off", "false", "0", "no", "none", ""]:
            self.assertTrue(helper.disabled(value))
        for value in ["0", "-1", "8MB", "${LIMIT}", "8M*2"]:
            self.assertFalse(helper.quantity(value) and 0 < helper.quantity(value) <= 8 * helper.MIB)
        self.assertEqual(helper.settings_findings({
            "session.upload_progress.enabled": "off", "fatal_error_backtraces": "0",
            "memory_limit": "-1", "post_max_size": "8M", "upload_max_filesize": "2M"}, "8.5"), [])

    def test_overrides_flagged_without_leaking_configuration_values(self):
        fpm = self.tree("8.5", "fpm")
        (fpm / "pool.d").mkdir()
        (fpm / "pool.d/www.conf").write_text("php_admin_value[memory_limit]=SECRET\n")
        apache = self.root / "etc/apache2"
        apache.mkdir()
        (apache / "site.conf").write_text("php_value auto_detect_line_endings 1\n")
        (self.root / "var/www/.htaccess").write_text("php_flag session.upload_progress.enabled On\n")
        (self.root / "var/www/.user.ini").write_text("date.timezone=SECRET\n")
        findings = helper.override_findings(self.root, [self.apache, fpm], [self.root / "var/www"])
        self.assertEqual(len(findings), 4)
        self.assertNotIn("SECRET", "\n".join(findings))

    def test_server_inventory_skips_unrelated_binary_keys_and_reads_standard_configs(self):
        nginx = self.root / "etc/nginx"
        (nginx / "sites-enabled").mkdir(parents=True)
        (nginx / "server.key").write_bytes(b"\xff\x00unrelated private key")
        (nginx / "server.key").chmod(0o000)
        (nginx / "sites-enabled/default").write_text('fastcgi_param PHP_VALUE "memory_limit=128M";\n')
        (nginx / "fastcgi_params").write_text('fastcgi_param PHP_ADMIN_VALUE "date.timezone=invalid";\n')
        findings = helper.override_findings(self.root, [self.apache], [self.root / "var/www"])
        self.assertEqual(len(findings), 2)
        self.assertFalse(any("server.key" in item for item in findings))

    def test_missing_root_and_unreadable_files_fail_without_writes(self):
        with patch.object(helper, "read_config", side_effect=PermissionError("fixture denied")), contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(helper.main(["--root", str(self.root)]), 2)
        with contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(helper.main(["--root", str(self.root / "missing")]), 2)
        self.assertFalse((self.apache / "conf.d" / helper.NAME).exists())

    def test_foreign_file_and_symlink_refused(self):
        target = self.apache / "conf.d" / helper.NAME
        target.write_text("; operator owned\n")
        with self.assertRaises(ValueError):
            self.inspect()
        target.unlink()
        target.symlink_to(self.apache / "php.ini")
        with self.assertRaises(ValueError):
            self.inspect()

    def test_configuration_fifo_and_device_symlink_refused(self):
        fifo = self.root / "fifo"
        os.mkfifo(fifo)
        with self.assertRaises(ValueError):
            helper.read_config(fifo)
        device = self.root / "device"
        device.symlink_to("/dev/null")
        with self.assertRaises(ValueError):
            helper.read_config(device)

    def test_oversized_configuration_refused(self):
        large = self.root / "large"
        large.write_bytes(b"x" * (2 * helper.MIB + 1))
        with self.assertRaises(ValueError):
            helper.read_config(large)

    def test_package_ini_symlink_remains_supported(self):
        source = self.root / "package.ini"
        source.write_text("session.upload_progress.enabled=off\n")
        (self.apache / "conf.d/20-session.ini").symlink_to(source)
        self.assertEqual(self.inspect()[1]["session.upload_progress.enabled"], "off")

    def test_all_destinations_preflight_before_any_write(self):
        first, _, _ = self.inspect()
        second, _, _ = self.inspect(self.tree("8.5", "fpm"))
        second.target.parent.chmod(0o777)
        with self.assertRaises(ValueError):
            self.apply([first, second])
        self.assertFalse(first.target.exists())

    def test_directory_symlink_hardlink_and_changed_target_refused(self):
        plan, _, _ = self.inspect()
        original = plan.target.parent
        renamed = original.with_name("real-conf.d")
        original.rename(renamed)
        original.symlink_to(renamed, target_is_directory=True)
        with self.assertRaises(ValueError):
            self.apply([plan])
        original.unlink()
        renamed.rename(original)
        self.apply([plan])
        plan, _, _ = self.inspect()
        os.link(plan.target, plan.target.with_suffix(".linked"))
        with self.assertRaises(ValueError):
            self.apply([plan])
        plan.target.with_suffix(".linked").unlink()
        plan.target.write_text(helper.MARKER + "; changed\n")
        with self.assertRaises(ValueError):
            self.apply([plan])

    def test_owned_update_creates_exact_backup(self):
        self.apply([self.inspect()[0]])
        plan, _, _ = self.inspect(unlimited=True)
        self.apply([plan])
        backups = list(plan.target.parent.glob(helper.NAME + ".bak-*"))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_bytes(), plan.previous)
        self.assertEqual(backups[0].stat().st_mode & 0o777, 0o600)

    def test_failed_atomic_replace_preserves_old_file_and_cleans_temporary(self):
        self.apply([self.inspect()[0]])
        plan, _, _ = self.inspect(unlimited=True)
        with patch.object(helper.os, "replace", side_effect=OSError("fixture failure")), self.assertRaises(OSError):
            self.apply([plan])
        self.assertEqual(plan.target.read_bytes(), plan.previous)
        self.assertFalse(list(plan.target.parent.glob("." + helper.NAME + ".*")))

    def test_distribution_backport_and_prerelease_not_assumed_fixed(self):
        for package_version, expected in [("8.2.35-1", True), ("1:8.2.36-1", True),
                                          ("8.2.34-1+deb12u9", False), ("8.2.35~rc1-1", False)]:
            with self.subTest(version=package_version):
                self.assertEqual(helper.version_fixed("8.2", package_version), expected)

    def report(self, inventory, os_info=None):
        with contextlib.redirect_stdout(io.StringIO()) as output:
            findings = helper.package_guidance(os_info or helper.detect_os(self.root, date(2026, 10, 21)), inventory, Path("/"), date(2026, 10, 21))
        return output.getvalue(), findings

    def test_detect_supported_debian_and_raspbian_with_date_boundaries(self):
        for identifier, version, codename, end in [("debian", "12", "bookworm", date(2028, 6, 30)),
                                                    ("raspbian", "13", "trixie", date(2030, 6, 30))]:
            with self.subTest(identifier=identifier):
                (self.root / "etc/os-release").write_text(f'ID={identifier}\nVERSION_ID={version}\nVERSION_CODENAME={codename}\n')
                info = helper.detect_os(self.root, end)
                self.assertTrue(info.supported)
                self.assertFalse(helper.detect_os(self.root, date(end.year, 7, 1)).supported)

    def test_unsupported_and_unknown_os_are_not_assumed_supported(self):
        for text, expected in [('ID=debian\nVERSION_ID=11\nVERSION_CODENAME=bullseye\n', "UNSUPPORTED RELEASE"),
                               ('ID=raspbian\nVERSION_ID=10\n', "UNSUPPORTED RELEASE"),
                               ('ID=ubuntu\nID_LIKE=debian\nVERSION_ID=24.04\n', "UNSUPPORTED BY THIS SCRIPT"),
                               ('ID=debian\nVERSION_ID=14\nVERSION_CODENAME=forky\n', "UNSUPPORTED BY THIS SCRIPT"),
                               ('ID=debian\nVERSION_ID=13\nVERSION_CODENAME=bookworm\n', "UNSUPPORTED BY THIS SCRIPT")]:
            with self.subTest(text=text):
                (self.root / "etc/os-release").write_text(text)
                info = helper.detect_os(self.root, date(2026, 10, 21))
                self.assertFalse(info.supported)
                self.assertIn(expected, info.message)

    def test_os_release_fallback_precedence_and_shell_text_not_executed(self):
        fallback = self.root / "usr/lib/os-release"
        fallback.parent.mkdir(parents=True)
        fallback.write_text("ID=raspbian\nVERSION_ID=13\n")
        self.assertEqual(helper.detect_os(self.root).identifier, "debian")
        (self.root / "etc/os-release").unlink()
        self.assertEqual(helper.detect_os(self.root).identifier, "raspbian")
        (self.root / "etc/os-release").write_text('ID=debian\nVERSION_ID=12\nPRETTY_NAME="$(touch should-not-exist)"\n')
        self.assertEqual(helper.detect_os(self.root).name, "$(touch should-not-exist)")
        self.assertFalse((self.root / "should-not-exist").exists())

    def test_offline_absolute_os_symlink_does_not_read_host(self):
        (self.root / "etc/os-release").unlink()
        (self.root / "etc/os-release").symlink_to("/usr/lib/os-release")
        self.assertEqual(helper.detect_os(self.root).identifier, "unknown")

    def test_inventory_queries_only_read_only_metadata_and_disables_apt_cache_writes(self):
        installed = ("libapache2-mod-php8.2\tinstalled\t8.2.34-1\tphp8.2\n"
                     "php8.2-common\tinstalled\t8.2.34-1\tphp8.2\n"
                     "php8.2-cli\tinstalled\t8.2.34-1\tphp8.2\n"
                     "php8.2-memcached\tinstalled\t3.2\tphp-memcached\n"
                     "php8.1-fpm\tconfig-files\t8.1.33-1\tphp8.1\n")
        policy = "".join(f"{name}:\n  Installed: 8.2.34-1\n  Candidate: 8.2.35-1~deb12u1\n" for name in ["libapache2-mod-php8.2", "php8.2-common", "php8.2-cli"])
        results = [subprocess.CompletedProcess([], 0, value) for value in [installed, "php8.2-common\n", policy]]
        with patch.object(helper.sys, "platform", "linux"), patch.object(helper.subprocess, "run", side_effect=results) as run:
            inventory = helper.package_inventory(Path("/"))
        self.assertEqual(len(inventory.packages), 3)
        self.assertEqual(inventory.packages["php8.2-common"].candidate, "8.2.35-1~deb12u1")
        self.assertEqual(inventory.held, {"php8.2-common"})
        self.assertEqual([call.args[0][0] for call in run.call_args_list], ["/usr/bin/dpkg-query", "/usr/bin/apt-mark", "/usr/bin/apt-cache"])
        for call in run.call_args_list:
            self.assertNotIn("shell", call.kwargs)
            self.assertEqual(call.kwargs["env"]["LC_ALL"], "C")
        self.assertIn("Dir::Cache::pkgcache=", run.call_args_list[-1].args[0])

    def test_inventory_unknown_candidates_and_query_failures_are_explicit(self):
        installed = "php8.4-fpm:arm64\tinstalled\t8.4.26-1\tphp8.4\n"
        with patch.object(helper.sys, "platform", "linux"), patch.object(helper, "query", side_effect=[installed, None, "php8.4-fpm:arm64:\n  Candidate: (none)\n"]):
            inventory = helper.package_inventory(Path("/"))
        self.assertIsNone(inventory.held)
        self.assertIsNone(inventory.packages["php8.4-fpm:arm64"].candidate)
        self.assertTrue(inventory.issues)
        with patch.object(helper.subprocess, "run", side_effect=FileNotFoundError):
            self.assertIsNone(helper.query(["/usr/bin/dpkg-query"]))
        with patch.object(helper.subprocess, "run", side_effect=subprocess.TimeoutExpired("fixture", 10)):
            self.assertIsNone(helper.query(["/usr/bin/apt-cache"]))

    def test_multiarch_policy_and_hold_names_are_handled_conservatively(self):
        installed = "php8.4-fpm:arm64\tinstalled\t8.4.26-1\tphp8.4\n"
        with patch.object(helper.sys, "platform", "linux"), patch.object(helper, "query", side_effect=[installed, "", "php8.4-fpm:\n  Candidate: 8.4.27-1\n"]):
            inventory = helper.package_inventory(Path("/"))
        self.assertEqual(inventory.packages["php8.4-fpm:arm64"].candidate, "8.4.27-1")
        self.assertTrue(helper.held_package("php8.4-fpm:arm64", {"php8.4-fpm"}))
        self.assertTrue(helper.held_package("php8.4-fpm", {"php8.4-fpm:arm64"}))
        self.assertFalse(helper.held_package("php8.4-fpm:armhf", {"php8.4-fpm:arm64"}))

    def test_legacy_php5_source_version_and_unmapped_config_branch_flag_eol(self):
        installed = "libapache2-mod-php5\tinstalled\t5.4.45-0+deb7u1\tphp5\n"
        with patch.object(helper.sys, "platform", "linux"), patch.object(helper, "query", side_effect=[installed, "", "libapache2-mod-php5:\n  Candidate: 5.4.45-0+deb7u1\n"]):
            inventory = helper.package_inventory(Path("/"))
        self.assertEqual(inventory.packages["libapache2-mod-php5"].branch, "5.4")
        output, _ = self.report(inventory)
        self.assertIn("PHP 5.4: UPSTREAM UNSUPPORTED/EOL", output)
        self.tree("7.4", "fpm")
        with contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(helper.main(["--root", str(self.root)]), 1)
        self.assertIn("PHP 7.4: UPSTREAM UNSUPPORTED/EOL", output.getvalue())

    def test_exact_upgrade_commands_include_installed_common_cli_and_module(self):
        packages = {name: helper.Package(name, "8.2", "8.2.34-1", "8.2.35-1~deb12u1")
                    for name in ["php8.2-common", "php8.2-cli", "libapache2-mod-php8.2"]}
        output, findings = self.report(helper.Inventory(packages, set(), []))
        names = "libapache2-mod-php8.2 php8.2-cli php8.2-common"
        self.assertIn("sudo apt update", output)
        self.assertIn("LOCAL indexes", output)
        self.assertIn("PATCHED CANDIDATE", output)
        self.assertIn("apt-get --simulate install --only-upgrade --no-remove " + names, output)
        self.assertIn("sudo apt-get install --only-upgrade --no-remove " + names, output)
        self.assertTrue(findings)  # Old installed packages remain unfixed by an audit.

    def test_holds_unverified_candidates_and_unsupported_os_withhold_upgrade(self):
        package = helper.Package("php8.2-common", "8.2", "8.2.34-1", "8.2.35-1~deb12u1")
        supported = helper.detect_os(self.root, date(2026, 10, 21))
        unsupported = helper.OSInfo("Debian 11", "debian", "11", "bullseye", False, "UNSUPPORTED RELEASE")
        for os_info, candidate, held in [(supported, package.candidate, {package.name}),
                                         (supported, "8.2.34-1", set()), (supported, None, set()),
                                         (supported, package.candidate, None),
                                         (unsupported, package.candidate, set())]:
            with self.subTest(candidate=candidate, held=held):
                package.candidate = candidate
                output, findings = self.report(helper.Inventory({package.name: package}, held, []), os_info)
                self.assertIn("UPGRADE COMMAND WITHHELD", output)
                self.assertNotIn("sudo apt-get install", output)
                self.assertTrue(findings)

    def test_already_fixed_upstream_eol_and_new_unknown_branches_are_distinct(self):
        for branch, installed, expected in [("8.4", "8.4.27-1", "FIXED INSTALLED"),
                                            ("7.4", "7.4.33-99", "UPSTREAM UNSUPPORTED/EOL"),
                                            ("8.6", "8.6.0-1", "UNSUPPORTED BY THIS SCRIPT")]:
            package = helper.Package("php" + branch + "-common", branch, installed, installed)
            output, _ = self.report(helper.Inventory({package.name: package}, set(), []))
            self.assertIn(expected, output)
            self.assertNotIn("sudo apt-get install", output)

    def test_restart_commands_deduplicate_apache_and_identify_each_fpm_version(self):
        directories = [self.apache, self.tree("8.4", "apache2"), self.tree("8.2", "fpm"),
                       self.tree("8.5", "fpm"), self.tree("8.2", "cgi")]
        with contextlib.redirect_stdout(io.StringIO()) as output:
            helper.restart_guidance(directories)
        text = output.getvalue()
        self.assertEqual(text.count("sudo systemctl restart apache2.service"), 1)
        self.assertIn("sudo systemctl restart php8.2-fpm.service", text)
        self.assertIn("sudo systemctl restart php8.5-fpm.service", text)
        self.assertIn("persistent FastCGI launcher is unknown", text)
        self.assertIn("actually serving", text)

    @unittest.skipUnless(shutil.which("shellcheck"), "ShellCheck is not installed")
    def test_generated_upgrade_and_restart_commands_pass_shellcheck(self):
        packages = {name: helper.Package(name, "8.2", "8.2.34-1", "8.2.35-1~deb12u1")
                    for name in ["libapache2-mod-php8.2", "php8.2-common:armhf", "php8.2-cli"]}
        report, _ = self.report(helper.Inventory(packages, set(), []))
        directories = [self.apache, self.tree("8.2", "fpm"), self.tree("8.5", "fpm")]
        with contextlib.redirect_stdout(io.StringIO()) as output:
            helper.restart_guidance(directories)
        report += output.getvalue()
        commands = [line.strip() for line in report.splitlines() if line.startswith("  ")]
        commands.extend(line.split(": ", 1)[1] for line in report.splitlines() if line.startswith("Inspect package policy: "))
        self.assertTrue(any("apt-get install" in command for command in commands))
        self.assertTrue(any("systemctl restart" in command for command in commands))
        result = subprocess.run([shutil.which("shellcheck"), "--shell=sh", "-"],
                                input="#!/bin/sh\nset -eu\n" + "\n".join(commands) + "\n",
                                capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unsupported_os_and_restart_commands_appear_in_cli_audit(self):
        (self.root / "etc/os-release").write_text("ID=debian\nVERSION_ID=11\nVERSION_CODENAME=bullseye\n")
        before = self.snapshot()
        result = subprocess.run([sys.executable, "-B", str(SCRIPT), "--root", str(self.root), "--verbose"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("UNSUPPORTED RELEASE", result.stdout)
        self.assertIn("sudo systemctl restart apache2.service", result.stdout)
        self.assertEqual(before, self.snapshot())

    def report_scenario(self, patched=False, mitigated=False):
        """Representative multi-package installation from the operator report."""
        shutil.rmtree(self.root / "etc/php/8.2")
        for sapi in ["apache2", "cgi"]:
            tree = self.tree("8.4", sapi)
            (tree / "php.ini").write_text("memory_limit=128M\npost_max_size=8M\nupload_max_filesize=2M\n")
        (self.root / "etc/os-release").write_text('ID=debian\nVERSION_ID=13\nVERSION_CODENAME=trixie\nPRETTY_NAME="Debian GNU/Linux 13 (trixie)"\n')
        apache = self.root / "etc/apache2"
        (apache / "mods-available").mkdir(parents=True)
        (apache / "mods-enabled").mkdir()
        policy = apache / "mods-available/php8.4.conf"
        policy.write_text("php_admin_value open_basedir none\n")
        (apache / "mods-enabled/php8.4.conf").symlink_to(policy)
        names = ["libapache2-mod-php8.4", "php8.4", "php8.4-cgi", "php8.4-cli", "php8.4-common",
                 "php8.4-mbstring", "php8.4-opcache", "php8.4-readline", "php8.4-xml"]
        version = "8.4.27-1~deb13u1" if patched else "8.4.26-1~deb13u1"
        if mitigated:
            self.apply([self.inspect(directory)[0] for directory in helper.discover(self.root, None)])
        return helper.Inventory({name: helper.Package(name, "8.4", version, version) for name in names}, set(), [])

    def test_report_groups_packages_and_deduplicates_config_symlinks(self):
        inventory = self.report_scenario()
        before = self.snapshot()
        with patch.object(helper, "package_inventory", return_value=inventory), patch.object(helper, "service_status", return_value={"apache2.service": "inactive"}), contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(helper.main(["--root", str(self.root)]), 1)
        text = output.getvalue()
        self.assertIn("CHECK COMPLETE", text)
        self.assertIn("(9 packages)", text)
        self.assertEqual(text.count("PHP 8.4: 8.4.26"), 1)
        self.assertIn("Apache: not running", text)
        self.assertIn("Configuration notes: 1 item (informational)", text)
        self.assertNotIn("sudo systemctl restart", text)
        self.assertNotIn("PACKAGE php8.4", text)
        self.assertNotIn("unsupported/unverified OS", text)
        self.assertEqual(before, self.snapshot())

    def test_report_verbose_preserves_full_evidence(self):
        inventory = self.report_scenario()
        with patch.object(helper, "package_inventory", return_value=inventory), contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(helper.main(["--root", str(self.root), "--verbose"]), 1)
        text = output.getvalue()
        self.assertIn("Detailed evidence", text)
        self.assertIn("PACKAGE php8.4-common", text)
        self.assertIn("Planned override", text)

    def test_patched_operator_report_succeeds_with_finite_memory_and_config_note(self):
        inventory = self.report_scenario(patched=True, mitigated=True)
        before = self.snapshot()
        for arguments in [[], ["--verbose"]]:
            with self.subTest(arguments=arguments), patch.object(helper, "package_inventory", return_value=inventory), patch.object(helper, "service_status", return_value={"apache2.service": "active"}), contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(helper.main(["--root", str(self.root), *arguments]), 0)
            text = output.getvalue()
            self.assertIn("Result: PHP security fix installed.", text)
            self.assertIn("Optional mitigations (not required for patched PHP)", text)
            self.assertIn("Configuration notes: 1 item (informational)", text)
            self.assertIn("Worker restart history", text)
            self.assertNotIn("review needed", text)
            self.assertNotIn("SECURITY ", text)
            self.assertNotIn("sudo apt update", text)
            self.assertNotIn("a PHP update provides the complete fix", text)
            if arguments:
                self.assertIn("OPTIONAL MITIGATION", text)
                self.assertIn("CONFIG NOTE", text)
        self.assertEqual(before, self.snapshot())

    def test_patched_php_does_not_require_the_partial_mitigation_profile(self):
        inventory = self.report_scenario(patched=True)
        fpm = self.tree("8.5", "fpm")
        (fpm / "php.ini").write_text("memory_limit=128M\npost_max_size=64M\nupload_max_filesize=64M\n")
        package = helper.Package("php8.5-fpm", "8.5", "8.5.12-1", "8.5.12-1")
        inventory.packages[package.name] = package
        with patch.object(helper, "package_inventory", return_value=inventory), contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(helper.main(["--root", str(self.root), "--verbose"]), 0)
        text = output.getvalue()
        self.assertIn("not disabled in 3 web configurations", text)
        self.assertIn("Fatal backtraces: not disabled", text)
        self.assertIn("outside the mitigation profile", text)
        self.assertIn("OPTIONAL MITIGATION", text)
        self.assertNotIn("SECURITY ", text)

    def test_mixed_patched_and_unverified_packages_still_need_security_review(self):
        inventory = self.report_scenario(patched=True, mitigated=True)
        inventory.packages["php8.4-cli"].installed = "8.4.26-1~deb13u1"
        with patch.object(helper, "package_inventory", return_value=inventory), contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(helper.main(["--root", str(self.root), "--verbose"]), 1)
        text = output.getvalue()
        self.assertIn("Result: security review needed.", text)
        self.assertIn("SECURITY php8.4-cli: installed fix unverified", text)
        self.assertNotIn("Result: PHP security fix installed.", text)
        self.assertIn("Optional partial mitigations", text)

    def test_patched_packages_do_not_hide_unknown_serving_packages(self):
        inventory = self.report_scenario(patched=True)
        del inventory.packages["libapache2-mod-php8.4"]
        self.tree("8.2", "fpm")
        with patch.object(helper, "package_inventory", return_value=inventory), contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(helper.main(["--root", str(self.root), "--verbose"]), 1)
        text = output.getvalue()
        self.assertIn("serving package libapache2-mod-php8.4 not established", text)
        self.assertIn("serving package php8.2-fpm not established", text)
        self.assertNotIn("Result: PHP security fix installed.", text)

    def test_upgrade_metadata_does_not_undo_a_verified_installed_fix(self):
        inventory = self.report_scenario(patched=True)
        for package in inventory.packages.values():
            package.candidate = None
        inventory.held = None
        inventory.issues = ["APT hold state unavailable", "APT candidates unavailable"]
        with patch.object(helper, "package_inventory", return_value=inventory), contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(helper.main(["--root", str(self.root), "--verbose"]), 0)
        text = output.getvalue()
        self.assertIn("Result: PHP security fix installed.", text)
        self.assertIn("UPGRADE NOTE APT candidates unavailable", text)
        self.assertNotIn("SECURITY ", text)
        package = inventory.packages["php8.4-common"]
        package.installed = "8.4.26-1~deb13u1"
        with patch.object(helper, "package_inventory", return_value=inventory), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(helper.main(["--root", str(self.root)]), 1)

    def test_patched_held_packages_with_older_candidates_are_not_security_findings(self):
        inventory = self.report_scenario(patched=True)
        inventory.held = set(inventory.packages)
        for package in inventory.packages.values():
            package.candidate = "8.4.26-1~deb13u1"
        with patch.object(helper, "package_inventory", return_value=inventory), contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(helper.main(["--root", str(self.root), "--verbose"]), 0)
        self.assertIn("FIXED INSTALLED", output.getvalue())
        self.assertNotIn("SECURITY ", output.getvalue())

    def test_patched_php_on_unsupported_os_still_needs_security_review(self):
        inventory = self.report_scenario(patched=True)
        (self.root / "etc/os-release").write_text("ID=debian\nVERSION_ID=11\nVERSION_CODENAME=bullseye\n")
        with patch.object(helper, "package_inventory", return_value=inventory), contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(helper.main(["--root", str(self.root)]), 1)
        self.assertIn("UNSUPPORTED RELEASE", output.getvalue())
        self.assertIn("Result: security review needed.", output.getvalue())

    def test_patched_upstream_eol_branch_still_has_a_support_finding(self):
        package = helper.Package("php8.2-common", "8.2", "8.2.35-1", "8.2.35-1")
        inventory = helper.Inventory({package.name: package}, set(), [])
        with contextlib.redirect_stdout(io.StringIO()):
            findings = helper.package_guidance(helper.detect_os(self.root), inventory, self.root, date(2027, 1, 1))
        self.assertTrue(any("UNSUPPORTED/EOL" in finding for finding in findings))

    def test_installed_inventory_failure_still_prevents_success(self):
        self.report_scenario(patched=True)
        inventory = helper.Inventory({}, None, ["Installed PHP package inventory unavailable"], False)
        with patch.object(helper, "package_inventory", return_value=inventory), contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(helper.main(["--root", str(self.root), "--verbose"]), 1)
        self.assertIn("SECURITY Installed PHP package inventory unavailable", output.getvalue())
        self.assertNotIn("Result: PHP security fix installed.", output.getvalue())

    def test_patched_packages_do_not_hide_inspection_failures(self):
        inventory = self.report_scenario(patched=True)
        (self.root / "etc/php/8.4/apache2/php.ini").write_text("post_max_size=010\n")
        before = self.snapshot()
        with patch.object(helper, "package_inventory", return_value=inventory), contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(helper.main(["--root", str(self.root)]), 2)
        self.assertIn("CHECK FAILED", output.getvalue())
        self.assertNotIn("Result: PHP security fix installed.", output.getvalue())
        self.assertEqual(before, self.snapshot())

    def test_no_php_installed_is_not_a_failed_run(self):
        shutil.rmtree(self.root / "etc/php")
        with patch.object(helper, "package_inventory", return_value=helper.Inventory({}, set(), [])), contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(helper.main(["--root", str(self.root)]), 0)
        self.assertIn("NOTHING TO CHECK", output.getvalue())
        self.assertIn("No distribution PHP packages", output.getvalue())
        self.assertNotIn("CHECK FAILED", output.getvalue())

    def test_partial_apply_preserves_written_file_evidence(self):
        first, _, _ = self.inspect()
        second, _, _ = self.inspect(self.tree("8.4", "fpm"))
        original = helper.os.replace
        changes = []

        def replace(source, destination):
            if destination == second.target:
                raise OSError("fixture failure on second file")
            original(source, destination)

        with patch.object(helper.os, "replace", side_effect=replace), contextlib.redirect_stdout(io.StringIO()), self.assertRaises(OSError):
            helper.apply_plans([first, second], changes)
        self.assertEqual(changes, [("write", first.target)])
        report = helper.AuditReport(changes=changes, error="fixture failure on second file")
        with contextlib.redirect_stdout(io.StringIO()) as output:
            helper.summary(report, type("Args", (), {"allow_unlimited_memory": False})())
        self.assertIn("CHECK FAILED", output.getvalue())
        self.assertIn("Settings files changed: 1", output.getvalue())
        self.assertIn(str(first.target), output.getvalue())

    def test_service_queries_are_passive_and_unknown_is_not_stopped(self):
        inventory = helper.Inventory({}, set(), [])
        with patch.object(helper.sys, "platform", "linux"), patch.object(helper, "query", return_value="LoadState=loaded\nActiveState=active\n") as query:
            states = helper.service_status(Path("/"), [self.apache], inventory)
        self.assertEqual(states, {"apache2.service": "active"})
        self.assertEqual(query.call_args.args[0], ["/usr/bin/systemctl", "show", "--property=LoadState,ActiveState", "apache2.service"])
        with patch.object(helper.sys, "platform", "linux"), patch.object(helper, "query", return_value=None):
            self.assertEqual(helper.service_status(Path("/"), [self.apache], inventory)["apache2.service"], "unknown")
        with patch.object(helper, "query") as query:
            helper.service_status(self.root, [self.apache], inventory)
        query.assert_not_called()

    def test_compact_report_fits_wide_and_narrow_terminals(self):
        inventory = self.report_scenario()
        for width in [80, 52]:
            with self.subTest(width=width), patch.object(helper.shutil, "get_terminal_size", return_value=os.terminal_size((width, 24))), patch.object(helper, "package_inventory", return_value=inventory), contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(helper.main(["--root", str(self.root)]), 1)
            self.assertTrue(all(len(line) <= width for line in output.getvalue().splitlines()))
            self.assertLess(len(output.getvalue().splitlines()), 42)

    @unittest.skipUnless(shutil.which("shellcheck"), "ShellCheck is not installed")
    def test_compact_multiline_commands_pass_shellcheck(self):
        inventory = self.report_scenario()
        for package in inventory.packages.values():
            package.candidate = "8.4.27-1~deb13u1"
        for width in [80, 52]:
            with patch.object(helper.shutil, "get_terminal_size", return_value=os.terminal_size((width, 24))), patch.object(helper, "package_inventory", return_value=inventory), patch.object(helper, "service_status", return_value={"apache2.service": "active"}), contextlib.redirect_stdout(io.StringIO()) as output:
                helper.main(["--root", str(self.root)])
            # Text wrapping also indents prose. Extract command blocks only.
            lines, commands = output.getvalue().splitlines(), []
            continued = False
            for line in lines:
                if line.startswith(("  sudo ", "  apt-get ")) or continued:
                    commands.append(line)
                    continued = line.endswith("\\")
            script = "#!/bin/sh\nset -eu\n" + "\n".join(commands) + "\n"
            self.assertIn("systemctl restart apache2.service", script)
            result = subprocess.run([shutil.which("shellcheck"), "--shell=sh", "-"], input=script, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
