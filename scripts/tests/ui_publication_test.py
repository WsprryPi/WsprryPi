#!/usr/bin/env python3
"""Hardware-free regression tests for coherent UI artifact publication."""

from __future__ import annotations

import importlib.util
import io
import json
import os
from pathlib import Path
import stat
import tempfile
import unittest
from unittest import mock
from contextlib import redirect_stdout


ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


PUBLISHER = load_module("copy_ui", ROOT / "scripts" / "copy_ui.py")
MANIFEST = load_module(
    "generate_ui_manifest",
    ROOT / "WsprryPi-UI" / "scripts" / "generate_ui_manifest.py",
)


class UiPublicationTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        self.target = self.root / "live" / "wsprrypi"
        (self.source / "views").mkdir(parents=True)
        (self.source / "cache").mkdir()
        (self.source / "backups").mkdir()
        (self.source / "index.php").write_text("<?php echo 'new';\n", encoding="utf-8")
        (self.source / "site.css").write_text("body { color: black; }\n", encoding="utf-8")
        (self.source / "views" / "operation.php").write_text("operation\n", encoding="utf-8")
        (self.source / "cache" / "runtime.json").write_text("runtime\n", encoding="utf-8")
        (self.source / "backups" / "old.php").write_text("backup\n", encoding="utf-8")

    def tearDown(self):
        self.temporary.cleanup()

    def publish(self, **kwargs):
        kwargs.setdefault("backup_parent", self.root / "external-backups")
        return PUBLISHER.publish_ui(
            self.source,
            self.target,
            source_commit="a" * 40,
            application_version="3.2.0-test",
            manifest_module=MANIFEST,
            **kwargs,
        )

    def test_published_tree_exactly_matches_manifest(self):
        status = self.publish()
        manifest_path = self.target / "ui-manifest.json"
        validated = MANIFEST.load_manifest(manifest_path)
        installed = MANIFEST.classify_installed_ui(self.target, manifest_path)

        self.assertEqual(status["final_installed_state"], "packaged")
        self.assertEqual(installed["installed_state"], "packaged")
        self.assertEqual(
            installed["installed_ui_build_id"],
            validated["packaged_ui_build_id"],
        )
        self.assertEqual(
            validated["files"],
            MANIFEST.collect_file_records(self.target),
        )
        self.assertEqual(
            stat.S_IMODE(manifest_path.stat().st_mode),
            0o444,
            "the installed manifest must not be runtime-writable",
        )

    def test_repository_ui_artifact_converges(self):
        repository_target = self.root / "repository-live" / "wsprrypi"
        status = PUBLISHER.publish_ui(
            ROOT / "WsprryPi-UI" / "data",
            repository_target,
            source_commit="b" * 40,
            application_version="3.2.0-test",
            manifest_module=MANIFEST,
        )
        installed = MANIFEST.classify_installed_ui(
            repository_target, repository_target / "ui-manifest.json"
        )
        self.assertEqual(installed["installed_state"], "packaged")
        self.assertEqual(
            installed["packaged_ui_build_id"], installed["installed_ui_build_id"]
        )
        self.assertEqual(status["final_installed_ui_build_id"], installed["installed_ui_build_id"])

    def test_runtime_and_backup_paths_do_not_change_identity(self):
        status = self.publish()
        (self.target / "cache" / "new-runtime.json").write_text("changed\n")
        (self.target / "backups" / "new-backup.php").write_text("changed\n")
        installed = MANIFEST.classify_installed_ui(
            self.target, self.target / "ui-manifest.json"
        )
        self.assertEqual(installed["installed_state"], "packaged")
        self.assertEqual(
            installed["installed_ui_build_id"], status["final_installed_ui_build_id"]
        )

    def test_locally_modified_files_are_backed_up_and_replaced(self):
        self.publish()
        prior_manifest = (self.target / "ui-manifest.json").read_bytes()
        (self.target / "index.php").chmod(0o644)
        (self.target / "index.php").write_text("locally modified\n")
        (self.target / "custom.php").write_text("custom addition\n")
        (self.target / "site.css").unlink()

        result = self.publish(backup_parent=self.root / "external-backups")
        backup = Path(result["backup_directory"])
        self.assertEqual(result["prior_state"], "locally_modified")
        self.assertEqual(result["modified_files"], ["index.php"])
        self.assertEqual(result["added_files"], ["custom.php"])
        self.assertEqual(result["missing_files"], ["site.css"])
        self.assertEqual((backup / "files" / "index.php").read_text(), "locally modified\n")
        self.assertEqual((backup / "files" / "custom.php").read_text(), "custom addition\n")
        self.assertEqual(Path(result["prior_manifest_backup"]).read_bytes(), prior_manifest)
        report = json.loads(Path(result["modification_report_path"]).read_text())
        self.assertTrue(report["backup_verified"])
        self.assertTrue(report["replacement_completed"])
        self.assertTrue(result["replacement_completed"])
        self.assertFalse((self.target / "custom.php").exists())
        final = MANIFEST.classify_installed_ui(self.target, self.target / "ui-manifest.json")
        self.assertEqual(final["installed_state"], "packaged")

    def test_unknown_prior_state_backs_up_complete_covered_tree(self):
        self.target.mkdir(parents=True)
        (self.target / "index.php").write_text("unknown old index\n")
        (self.target / "custom.php").write_text("unknown custom\n")
        (self.target / "cache").mkdir()
        (self.target / "cache" / "runtime").write_text("excluded\n")
        result = self.publish(backup_parent=self.root / "external-backups")
        backup = Path(result["backup_directory"])
        self.assertEqual(result["prior_state"], "unknown")
        self.assertEqual((backup / "files" / "index.php").read_text(), "unknown old index\n")
        self.assertEqual((backup / "files" / "custom.php").read_text(), "unknown custom\n")
        self.assertFalse((backup / "files" / "cache").exists())

    def test_backup_failure_prevents_replacement(self):
        self.publish()
        live_file = self.target / "index.php"
        live_file.chmod(0o644)
        live_file.write_text("must survive\n")
        with mock.patch.object(
            PUBLISHER, "_copy_verified", side_effect=OSError("injected backup failure")
        ):
            with self.assertRaises(PUBLISHER.UiPublicationError) as caught:
                self.publish(backup_parent=self.root / "external-backups")
        self.assertEqual(live_file.read_text(), "must survive\n")
        self.assertFalse(caught.exception.result["replacement_completed"])
        self.assertFalse(caught.exception.result["backup_verified"])

    def test_live_churn_during_backup_prevents_replacement(self):
        self.publish()
        live_file = self.target / "index.php"
        live_file.chmod(0o644)
        live_file.write_text("must survive\n")
        real_copy = PUBLISHER._copy_verified

        def copy_then_change(*args):
            real_copy(*args)
            (self.target / "late-change.php").write_text("arrived during backup\n")

        with mock.patch.object(PUBLISHER, "_copy_verified", side_effect=copy_then_change):
            with self.assertRaisesRegex(PUBLISHER.UiPublicationError, "changed while") as caught:
                self.publish(backup_parent=self.root / "external-backups")
        self.assertEqual(live_file.read_text(), "must survive\n")
        self.assertTrue((self.target / "late-change.php").exists())
        self.assertFalse(caught.exception.result["replacement_completed"])

    def test_fail_on_modifications_refuses_replacement(self):
        self.publish()
        live_file = self.target / "index.php"
        live_file.chmod(0o644)
        live_file.write_text("must survive\n")
        with self.assertRaisesRegex(PUBLISHER.UiPublicationError, "replacement refused"):
            self.publish(
                backup_parent=self.root / "external-backups",
                fail_on_ui_modifications=True,
            )
        self.assertEqual(live_file.read_text(), "must survive\n")

    def render(self, result):
        result_path = self.root / "result.json"
        PUBLISHER._write_json_atomic(result_path, result)
        output = io.StringIO()
        with redirect_stdout(output):
            status = PUBLISHER.render_result(result_path)
        return status, output.getvalue()

    def test_final_renderer_suppresses_routine_success(self):
        for prior_state in ("packaged", "not_installed"):
            with self.subTest(prior_state=prior_state):
                result = PUBLISHER._new_result()
                result.update({
                    "prior_state": prior_state,
                    "replacement_completed": True,
                })
                status, rendered = self.render(result)
                self.assertEqual(status, 0)
                self.assertEqual(rendered, "")

    def test_final_renderer_summarizes_successful_backup(self):
        result = PUBLISHER._new_result()
        result.update({
            "prior_state": "locally_modified",
            "modified_files": ["index.php"],
            "added_files": ["custom.php"],
            "missing_files": ["site.css"],
            "prior_manifest_backup": "/backup/prior-manifest.json",
            "modification_report_path": "/backup/modification-report.json",
            "backup_directory": "/backup",
            "backup_verified": True,
            "replacement_completed": True,
        })
        status, rendered = self.render(result)
        self.assertEqual(status, 0)
        for expected in (
            "Customized UI backed up",
            "saved your UI changes and installed the stock UI",
            "Changes: 1 modified, 1 added, 1 missing",
            "Backup: /backup",
            "Details: /backup/modification-report.json",
        ):
            self.assertIn(expected, rendered)
        self.assertNotIn("Prior state", rendered)
        self.assertNotIn("Prior manifest", rendered)
        self.assertNotIn("Replacement completed", rendered)

    def test_final_renderer_reports_unknown_state_backup_without_inventory(self):
        result = PUBLISHER._new_result()
        result.update({
            "prior_state": "unknown",
            "backup_directory": "/backup",
            "backup_verified": True,
            "replacement_completed": True,
        })
        status, rendered = self.render(result)
        self.assertEqual(status, 0)
        self.assertIn("Existing UI backed up", rendered)
        self.assertIn("could not verify the prior UI", rendered)
        self.assertIn("Backup: /backup", rendered)
        self.assertNotIn("Changes:", rendered)

    def test_final_renderer_always_reports_publication_error(self):
        result = PUBLISHER._new_result()
        result.update({
            "prior_state": "locally_modified",
            "modified_files": ["index.php"],
            "backup_directory": "/backup",
            "backup_verified": False,
            "replacement_completed": False,
            "error": "publication stopped",
        })
        status, rendered = self.render(result)
        self.assertEqual(status, 0)
        self.assertIn("UI replacement issue", rendered)
        self.assertIn("UI publication failed: publication stopped", rendered)
        self.assertIn("Existing changes: 1 modified", rendered)
        self.assertIn("Backup (not verified): /backup", rendered)
        self.assertIn("Stock UI installation did not complete", rendered)

    def test_final_renderer_reports_unreadable_result(self):
        missing = self.root / "missing-result.json"
        output = io.StringIO()
        with redirect_stdout(output):
            status = PUBLISHER.render_result(missing)
        self.assertEqual(status, 1)
        self.assertIn("UI replacement issue", output.getvalue())
        self.assertIn("Publication details are unavailable", output.getvalue())

    def test_final_renderer_reports_structurally_invalid_result(self):
        invalid = self.root / "invalid-result.json"
        invalid.write_text("[]\n", encoding="utf-8")
        output = io.StringIO()
        with redirect_stdout(output):
            status = PUBLISHER.render_result(invalid)
        self.assertEqual(status, 1)
        self.assertIn("UI replacement issue", output.getvalue())
        self.assertIn("invalid format", output.getvalue())

    def test_validation_failure_leaves_live_tree_untouched(self):
        self.target.mkdir(parents=True)
        old_file = self.target / "index.php"
        old_file.write_text("old live UI\n", encoding="utf-8")
        with mock.patch.object(
            MANIFEST, "load_manifest", side_effect=ValueError("invalid staged manifest")
        ):
            with self.assertRaises(PUBLISHER.UiPublicationError):
                self.publish()
        self.assertEqual(old_file.read_text(encoding="utf-8"), "old live UI\n")
        self.assertFalse((self.target / "ui-manifest.json").exists())

    def test_staging_failure_leaves_live_tree_untouched(self):
        self.target.mkdir(parents=True)
        old_file = self.target / "index.php"
        old_file.write_text("old live UI\n", encoding="utf-8")
        os.symlink(self.source / "index.php", self.source / "unsafe-link.php")
        with self.assertRaises(PUBLISHER.UiPublicationError):
            self.publish()
        self.assertEqual(old_file.read_text(encoding="utf-8"), "old live UI\n")

    def test_symlink_publication_target_is_rejected(self):
        actual = self.root / "actual-live"
        actual.mkdir()
        marker = actual / "marker"
        marker.write_text("untouched\n")
        self.target.parent.mkdir(parents=True)
        os.symlink(actual, self.target)
        with self.assertRaisesRegex(PUBLISHER.UiPublicationError, "must not be a symlink"):
            self.publish()
        self.assertEqual(marker.read_text(), "untouched\n")

    def test_runtime_ownership_requires_root(self):
        with mock.patch.object(PUBLISHER.os, "geteuid", return_value=501):
            with self.assertRaisesRegex(PUBLISHER.UiPublicationError, "root privileges"):
                self.publish(owner="www-data", group="www-data")
        self.assertFalse(self.target.exists())

    def test_static_tree_is_runtime_immutable_but_cache_is_runtime_owned(self):
        staged = self.root / "permission-stage"
        (staged / "cache").mkdir(parents=True)
        (staged / "index.php").write_text("static\n")
        (staged / "ui-manifest.json").write_text("{}\n")
        (staged / "cache" / "runtime.json").write_text("runtime\n")
        ownership = {}

        def record_chown(path, uid, gid):
            ownership[Path(path)] = (uid, gid)

        with (
            mock.patch.object(PUBLISHER.os, "geteuid", return_value=0),
            mock.patch.object(PUBLISHER.pwd, "getpwnam", return_value=mock.Mock(pw_uid=33)),
            mock.patch.object(PUBLISHER.grp, "getgrnam", return_value=mock.Mock(gr_gid=44)),
            mock.patch.object(PUBLISHER.os, "chown", side_effect=record_chown),
        ):
            PUBLISHER._set_tree_permissions(staged, "www-data", "www-data")

        self.assertEqual(ownership[staged], (0, 0))
        self.assertEqual(ownership[staged / "index.php"], (0, 0))
        self.assertEqual(ownership[staged / "ui-manifest.json"], (0, 0))
        self.assertEqual(ownership[staged / "cache"], (33, 44))
        self.assertEqual(ownership[staged / "cache" / "runtime.json"], (33, 44))
        self.assertEqual(stat.S_IMODE((staged / "ui-manifest.json").stat().st_mode), 0o444)

    def test_prepublication_failure_leaves_live_tree_untouched(self):
        self.target.mkdir(parents=True)
        old_file = self.target / "index.php"
        old_file.write_text("old live UI\n", encoding="utf-8")

        def fail_before_publish(stage: Path):
            self.assertTrue((stage / "ui-manifest.json").is_file())
            raise RuntimeError("injected prepublication failure")

        with self.assertRaises(PUBLISHER.UiPublicationError):
            self.publish(before_publish=fail_before_publish)
        self.assertEqual(old_file.read_text(encoding="utf-8"), "old live UI\n")

    def test_publish_rename_failure_restores_live_tree(self):
        self.target.mkdir(parents=True)
        old_file = self.target / "index.php"
        old_file.write_text("old live UI\n", encoding="utf-8")
        real_replace = os.replace
        rejected = False

        def fail_new_tree_once(source, destination):
            nonlocal rejected
            source_path = Path(source)
            if source_path.name.startswith(".wsprrypi.stage.") and not rejected:
                rejected = True
                raise OSError("injected publication rename failure")
            return real_replace(source, destination)

        with mock.patch.object(PUBLISHER.os, "replace", side_effect=fail_new_tree_once):
            with self.assertRaises(PUBLISHER.UiPublicationError):
                self.publish()
        self.assertEqual(old_file.read_text(encoding="utf-8"), "old live UI\n")

    def test_postpublication_cleanup_failure_reports_replacement_truthfully(self):
        self.target.mkdir(parents=True)
        (self.target / "index.php").write_text("unknown old UI\n")
        real_rmtree = PUBLISHER.shutil.rmtree

        def fail_previous_cleanup(path, *args, **kwargs):
            if ".wsprrypi.previous." in Path(path).name:
                raise OSError("injected prior-tree cleanup failure")
            return real_rmtree(path, *args, **kwargs)

        with mock.patch.object(PUBLISHER.shutil, "rmtree", side_effect=fail_previous_cleanup):
            with self.assertRaises(PUBLISHER.UiPublicationError) as caught:
                self.publish()
        self.assertTrue(caught.exception.result["replacement_completed"])
        self.assertEqual(caught.exception.result["final_installed_state"], "packaged")
        final = MANIFEST.classify_installed_ui(self.target, self.target / "ui-manifest.json")
        self.assertEqual(final["installed_state"], "packaged")

    def test_postvalidation_change_is_not_published(self):
        self.target.mkdir(parents=True)
        old_file = self.target / "index.php"
        old_file.write_text("old live UI\n", encoding="utf-8")

        def change_staged_file(stage: Path):
            os.chmod(stage / "site.css", 0o644)
            (stage / "site.css").write_text("changed after validation\n")

        with self.assertRaisesRegex(
            PUBLISHER.UiPublicationError, "changed after manifest validation"
        ):
            self.publish(before_publish=change_staged_file)
        self.assertEqual(old_file.read_text(encoding="utf-8"), "old live UI\n")


if __name__ == "__main__":
    unittest.main()
