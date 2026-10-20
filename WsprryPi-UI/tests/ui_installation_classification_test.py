#!/usr/bin/env python3
"""Regression tests for installed WsprryPi UI identity classification."""

from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "generate_ui_manifest.py"
SPEC = importlib.util.spec_from_file_location("generate_ui_manifest", SCRIPT)
assert SPEC and SPEC.loader
manifest_module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(manifest_module)


class UiInstallationClassificationTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "views").mkdir()
        (self.root / "cache").mkdir()
        (self.root / "backups").mkdir()
        (self.root / "site.js").write_text("const packaged = true;\n", encoding="utf-8")
        (self.root / "views" / "index.php").write_text("<?php echo 'packaged';\n", encoding="utf-8")
        self.manifest_path = self.root / "ui-manifest.json"
        self.packaged_manifest = manifest_module.build_manifest(
            self.root, "b" * 40, "1.2.3"
        )
        self.write_manifest(self.packaged_manifest)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_manifest(self, manifest) -> None:
        self.manifest_path.write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
        )

    def classify(self):
        return manifest_module.classify_installed_ui(self.root, self.manifest_path)

    def assert_unknown(self, result, installed_identity_available=True) -> None:
        self.assertEqual(result["installed_state"], "unknown")
        if installed_identity_available:
            self.assertRegex(
                result["installed_ui_build_id"], r"^sha256:[0-9a-f]{64}$"
            )
        else:
            self.assertIsNone(result["installed_ui_build_id"])
        self.assertTrue(result["error"])
        self.assertEqual(result["modified_files"], [])
        self.assertEqual(result["added_files"], [])
        self.assertEqual(result["missing_files"], [])

    def test_matching_installation_is_packaged(self) -> None:
        result = self.classify()
        self.assertEqual(result["installed_state"], "packaged")
        self.assertEqual(
            result["installed_ui_build_id"], result["packaged_ui_build_id"]
        )
        self.assertEqual(result["modified_files"], [])
        self.assertEqual(result["added_files"], [])
        self.assertEqual(result["missing_files"], [])
        self.assertIsNone(result["error"])

    def test_changed_added_and_missing_files_are_reported(self) -> None:
        (self.root / "site.js").write_text("const userEdited = true;\n", encoding="utf-8")
        (self.root / "custom.css").write_text("body { color: purple; }\n", encoding="utf-8")
        (self.root / "views" / "index.php").unlink()

        result = self.classify()

        self.assertEqual(result["installed_state"], "locally_modified")
        self.assertNotEqual(
            result["installed_ui_build_id"], result["packaged_ui_build_id"]
        )
        self.assertEqual(result["modified_files"], ["site.js"])
        self.assertEqual(result["added_files"], ["custom.css"])
        self.assertEqual(result["missing_files"], ["views/index.php"])

    def test_stable_user_edit_is_classified_without_being_changed(self) -> None:
        edited = b"const userEdited = true;\n"
        target = self.root / "site.js"
        target.write_bytes(edited)

        first = self.classify()
        second = self.classify()

        self.assertEqual(first, second)
        self.assertEqual(first["installed_state"], "locally_modified")
        self.assertEqual(first["modified_files"], ["site.js"])
        self.assertEqual(target.read_bytes(), edited)

    def test_excluded_runtime_and_backup_files_do_not_change_state(self) -> None:
        (self.root / "cache" / "spots.json").write_text("runtime\n", encoding="utf-8")
        (self.root / "backups" / "site.js").write_text("old\n", encoding="utf-8")

        result = self.classify()

        self.assertEqual(result["installed_state"], "packaged")
        self.assertEqual(result["added_files"], [])

    def test_unreadable_manifest_is_unknown(self) -> None:
        with mock.patch.object(
            manifest_module.Path,
            "read_text",
            side_effect=PermissionError("manifest permission denied"),
        ):
            self.assert_unknown(self.classify())

    def test_unreadable_installed_file_is_unknown(self) -> None:
        with mock.patch.object(
            manifest_module,
            "sha256_file",
            side_effect=PermissionError("installed file permission denied"),
        ):
            result = self.classify()
        self.assert_unknown(result, installed_identity_available=False)
        self.assertIsNone(result["packaged_ui_build_id"])

    def test_unreadable_installed_directory_is_unknown(self) -> None:
        def denied_walk(*args, **kwargs):
            kwargs["onerror"](PermissionError("installed directory permission denied"))
            yield from ()

        with mock.patch.object(manifest_module.os, "walk", side_effect=denied_walk):
            result = self.classify()
        self.assert_unknown(result, installed_identity_available=False)
        self.assertIsNone(result["packaged_ui_build_id"])

    def test_malformed_json_is_unknown(self) -> None:
        self.manifest_path.write_text("{not json\n", encoding="utf-8")
        self.assert_unknown(self.classify())

    def test_unsupported_schema_is_unknown(self) -> None:
        manifest = dict(self.packaged_manifest)
        manifest["schema_version"] = 2
        self.write_manifest(manifest)
        self.assert_unknown(self.classify())

    def test_unsafe_manifest_path_is_unknown(self) -> None:
        manifest = json.loads(json.dumps(self.packaged_manifest))
        manifest["files"][0]["path"] = "../outside.js"
        self.write_manifest(manifest)
        self.assert_unknown(self.classify())


if __name__ == "__main__":
    unittest.main()
