#!/usr/bin/env python3
"""Regression tests for the schema-v1 deterministic UI manifest generator."""

from __future__ import annotations

import importlib.util
import json
import os
import stat
import tempfile
import time
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "generate_ui_manifest.py"
SPEC = importlib.util.spec_from_file_location("generate_ui_manifest", SCRIPT)
assert SPEC and SPEC.loader
manifest_module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(manifest_module)


class UiManifestTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "views").mkdir()
        (self.root / "assets").mkdir()
        (self.root / "cache").mkdir()
        (self.root / "site.js").write_text("const value = 1;\n", encoding="utf-8")
        (self.root / "views" / "index.php").write_text("<?php echo 'ok';\n", encoding="utf-8")
        (self.root / "assets" / "icon.png").write_bytes(b"\x89PNG\r\nfixture")
        (self.root / "cache" / "runtime.json").write_text("{}\n", encoding="utf-8")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def build(self):
        return manifest_module.build_manifest(self.root, "a" * 40, "1.2.3")

    def test_schema_and_identity_are_deterministic(self) -> None:
        first = self.build()
        second = self.build()
        self.assertEqual(first, second)
        self.assertEqual(first["schema_version"], 1)
        self.assertRegex(first["packaged_ui_build_id"], r"^sha256:[0-9a-f]{64}$")
        self.assertEqual(
            [entry["path"] for entry in first["files"]],
            ["assets/icon.png", "site.js", "views/index.php"],
        )
        self.assertEqual(
            manifest_module.serialize_manifest(first),
            manifest_module.serialize_manifest(second),
        )

    def test_content_changes_identity_for_each_covered_file_type(self) -> None:
        original = self.build()["packaged_ui_build_id"]
        candidates = [
            self.root / "site.js",
            self.root / "views" / "index.php",
            self.root / "assets" / "icon.png",
        ]
        for candidate in candidates:
            before = candidate.read_bytes()
            candidate.write_bytes(before + b"changed")
            self.assertNotEqual(self.build()["packaged_ui_build_id"], original)
            candidate.write_bytes(before)

    def test_timestamps_permissions_and_excluded_files_do_not_change_identity(self) -> None:
        original = self.build()["packaged_ui_build_id"]
        target = self.root / "site.js"
        later = time.time() + 3600
        os.utime(target, (later, later))
        target.chmod(stat.S_IRUSR)
        (self.root / "cache" / "runtime.json").write_text('{"changed":true}\n', encoding="utf-8")
        (self.root / "ui-manifest.json").write_text('{"self":"excluded"}\n', encoding="utf-8")
        (self.root / ".DS_Store").write_bytes(b"metadata")
        self.assertEqual(self.build()["packaged_ui_build_id"], original)

    def test_traversal_order_does_not_change_identity(self) -> None:
        first = self.build()
        records = list(reversed(first["files"]))
        records.sort(key=lambda entry: entry["path"].encode("utf-8"))
        self.assertEqual(
            manifest_module.packaged_ui_build_id(records),
            first["packaged_ui_build_id"],
        )

    def test_validator_rejects_malformed_or_unsafe_manifests(self) -> None:
        valid = self.build()
        malformed = []

        missing_field = dict(valid)
        missing_field.pop("source_commit")
        malformed.append(missing_field)

        wrong_schema = dict(valid)
        wrong_schema["schema_version"] = 2
        malformed.append(wrong_schema)

        invalid_commit = dict(valid)
        invalid_commit["source_commit"] = "not-a-commit"
        malformed.append(invalid_commit)

        unsafe_path = json.loads(json.dumps(valid))
        unsafe_path["files"][0]["path"] = "../escape.js"
        malformed.append(unsafe_path)

        invalid_hash = json.loads(json.dumps(valid))
        invalid_hash["files"][0]["sha256"] = "ABC"
        malformed.append(invalid_hash)

        unsorted = json.loads(json.dumps(valid))
        unsorted["files"].reverse()
        malformed.append(unsorted)

        wrong_identity = dict(valid)
        wrong_identity["packaged_ui_build_id"] = f"sha256:{'0' * 64}"
        malformed.append(wrong_identity)

        for candidate in malformed:
            with self.subTest(candidate=candidate):
                with self.assertRaises(manifest_module.ManifestError):
                    manifest_module.validate_manifest(candidate)

    def test_generator_rejects_symbolic_links(self) -> None:
        link = self.root / "linked.js"
        try:
            link.symlink_to(self.root / "site.js")
        except (OSError, NotImplementedError):
            self.skipTest("symbolic links are unavailable")
        with self.assertRaises(manifest_module.ManifestError):
            self.build()

    def test_excluded_runtime_tree_is_not_inspected(self) -> None:
        link = self.root / "cache" / "runtime-link"
        try:
            link.symlink_to(self.root / "site.js")
        except (OSError, NotImplementedError):
            self.skipTest("symbolic links are unavailable")
        self.build()


if __name__ == "__main__":
    unittest.main()
