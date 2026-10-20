#!/usr/bin/env python3

from __future__ import annotations

from contextlib import redirect_stdout
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "scripts/maintainer/inspect_received_support_bundle.py"
SPEC = importlib.util.spec_from_file_location("maintainer_intake", MODULE_PATH)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)

CASE_ID = "A7K3-M9QF-X2DP"
ARTIFACT_ID = "0123456789abcdef0123456789abcdef"
KEY_ID = "wsprrypi-bundle-2099-01"
REAL_AGE = Path(os.environ.get("WSPRRYPI_REAL_AGE", "/usr/bin/age"))
REAL_AGE_KEYGEN = Path(os.environ.get("WSPRRYPI_REAL_AGE_KEYGEN", "/usr/bin/age-keygen"))


class MaintainerIntakeInspectionTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="wsprrypi-maintainer-intake-")
        self.root = Path(self.temporary.name)
        self.work = self.root / "work"
        self.work.mkdir(mode=0o700)
        self.identity = self.root / f"{KEY_ID}.age-identity.txt"
        self.identity.write_text("AGE-SECRET-KEY-TEST-ONLY\n", encoding="ascii")
        self.identity.chmod(0o600)
        self.age = self.executable("age", "#!/bin/sh\ncat \"$4\"\n")
        self.files = {"logs/example.txt": b"diagnostic bytes\n"}
        self.archive = self.make_archive()
        self.ciphertext = self.root / f"wsprrypi-support-{CASE_ID}-{ARTIFACT_ID}.tar.gz.age"
        shutil.copyfile(self.archive, self.ciphertext)
        self.receipt = self.root / "receipt.json"
        self.write_receipt()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def executable(self, name: str, contents: str) -> Path:
        path = self.root / name
        path.write_text(contents, encoding="ascii")
        path.chmod(0o755)
        return path

    def manifest(self, files: dict[str, bytes] | None = None) -> dict:
        values = self.files if files is None else files
        return {
            "schema_version": 1, "contract_version": 1, "project_id": "wsprrypi",
            "project_version": "1.3.0", "case_id": CASE_ID,
            "created_at_utc": "2026-08-17T12:00:00Z",
            "collection_options": {"configuration_files_included": True,
                                   "full_logs_included": False,
                                   "i2c_probe_requested": False},
            "privacy_categories": ["callsign", "locator", "internal_ip", "logs"],
            "support_context": {"kind": "existing_github_issue",
                                "issue_url": "https://github.com/WsprryPi/WsprryPi/issues/414",
                                "problem_description": None, "contact": None},
            "collection_warnings": [],
            "files": [{"path": path, "size": len(contents),
                       "sha256": hashlib.sha256(contents).hexdigest()}
                      for path, contents in sorted(values.items())],
        }

    def make_archive(self, *, members: list[tuple[str, bytes, str, int]] | None = None,
                     manifest: dict | None = None) -> Path:
        path = self.root / f"archive-{len(list(self.root.glob('archive-*')))}.tar.gz"
        entries = members or [(f"bundle/{name}", contents, "file", 0o600)
                              for name, contents in self.files.items()]
        manifest_bytes = json.dumps(manifest or self.manifest(), sort_keys=True).encode()
        entries.append(("bundle/manifest.json", manifest_bytes, "file", 0o600))
        with tarfile.open(path, "w:gz") as archive:
            root = tarfile.TarInfo("bundle")
            root.type = tarfile.DIRTYPE
            root.mode = 0o700
            archive.addfile(root)
            for name, contents, kind, mode in entries:
                info = tarfile.TarInfo(name)
                info.mode = mode
                info.size = len(contents)
                if kind == "symlink":
                    info.type = tarfile.SYMTYPE
                    info.linkname = "target"
                    info.size = 0
                    archive.addfile(info)
                elif kind == "fifo":
                    info.type = tarfile.FIFOTYPE
                    info.size = 0
                    archive.addfile(info)
                else:
                    archive.addfile(info, io.BytesIO(contents))
        return path

    def write_receipt(self, overrides: dict | None = None) -> None:
        archive_size = self.archive.stat().st_size
        cipher_size = self.ciphertext.stat().st_size
        value = {
            "schema_version": 1, "project_id": "wsprrypi", "case_id": CASE_ID,
            "artifact_id": ARTIFACT_ID, "created_at_utc": "2026-08-17T12:00:00Z",
            "archive_filename": "WsprryPi-support-test.tar.gz",
            "archive_size": archive_size,
            "archive_sha256": hashlib.sha256(self.archive.read_bytes()).hexdigest(),
            "encrypted_filename": self.ciphertext.name, "encrypted_size": cipher_size,
            "encrypted_sha256": hashlib.sha256(self.ciphertext.read_bytes()).hexdigest(),
            "bundle_encryption_key_id": KEY_ID,
            "issue_url": "https://github.com/WsprryPi/WsprryPi/issues/414",
            "upload_state": "encrypted_artifact_downloaded",
        }
        value.update(overrides or {})
        self.receipt.write_text(json.dumps(value), encoding="utf-8")

    def invoke(self, **overrides) -> MODULE.InspectionResult:
        values = {"age": self.age, "ciphertext": self.ciphertext,
                  "receipt_path": self.receipt, "identity": self.identity,
                  "work_directory": self.work, "timeout": 1.0}
        values.update(overrides)
        return MODULE.inspect(**values)

    def use_archive(self, archive: Path) -> None:
        self.archive = archive
        shutil.copyfile(archive, self.ciphertext)
        self.write_receipt()

    def test_success_correlates_without_retaining_plaintext(self) -> None:
        result = self.invoke()
        self.assertEqual(result, MODULE.InspectionResult(
            MODULE.InspectionStatus.inspected, CASE_ID, ARTIFACT_ID, KEY_ID,
            "https://github.com/WsprryPi/WsprryPi/issues/414"))
        self.assertEqual(list(self.work.iterdir()), [])

    def test_manifest_has_an_independent_bounded_size(self) -> None:
        manifest = self.manifest()
        manifest["collection_warnings"] = ["x" * MODULE.MAX_RECEIPT]
        self.use_archive(self.make_archive(manifest=manifest))
        self.assertGreater(
            len(json.dumps(manifest, sort_keys=True).encode()), MODULE.MAX_RECEIPT)
        self.assertEqual(self.invoke().status, MODULE.InspectionStatus.inspected)

        with mock.patch.object(MODULE, "MAX_MANIFEST", 1):
            self.assertEqual(self.invoke().status, MODULE.InspectionStatus.invalid_manifest)
        self.assertEqual(list(self.work.iterdir()), [])

    def test_provisioned_read_only_identity_mode_is_accepted(self) -> None:
        self.identity.chmod(0o400)
        self.assertEqual(self.invoke().status, MODULE.InspectionStatus.inspected)
        self.assertEqual(list(self.work.iterdir()), [])

    def test_production_accepts_an_explicit_safe_age_executable(self) -> None:
        result = MODULE.inspect_production(
            ciphertext=self.ciphertext, receipt_path=self.receipt,
            identity=self.identity, work_directory=self.work, age=self.age)
        self.assertEqual(result.status, MODULE.InspectionStatus.inspected)
        unsafe = self.root / "age-link"
        unsafe.symlink_to(self.age)
        result = MODULE.inspect_production(
            ciphertext=self.ciphertext, receipt_path=self.receipt,
            identity=self.identity, work_directory=self.work, age=unsafe)
        self.assertEqual(result.status, MODULE.InspectionStatus.unsafe_input)

    def test_receipt_is_strict_bounded_and_duplicate_rejecting(self) -> None:
        for override in ({"unexpected": True}, {"schema_version": True},
                         {"case_id": "bad"}, {"archive_size": MODULE.MAX_ARCHIVE + 1},
                         {"issue_url": "https://evil.invalid/414"}):
            with self.subTest(override=override):
                self.write_receipt(override)
                self.assertEqual(self.invoke().status, MODULE.InspectionStatus.invalid_receipt)
        self.receipt.write_text('{"schema_version":1,"schema_version":1}', encoding="ascii")
        self.assertEqual(self.invoke().status, MODULE.InspectionStatus.invalid_receipt)

    def test_key_ciphertext_and_archive_mismatches_fail_closed(self) -> None:
        wrong = self.root / "wsprrypi-bundle-2099-02.age-identity.txt"
        wrong.write_text("secret", encoding="ascii")
        wrong.chmod(0o600)
        self.assertEqual(self.invoke(identity=wrong).status, MODULE.InspectionStatus.key_mismatch)
        self.ciphertext.write_bytes(self.ciphertext.read_bytes() + b"mutation")
        self.assertEqual(self.invoke().status, MODULE.InspectionStatus.ciphertext_mismatch)
        shutil.copyfile(self.archive, self.ciphertext)
        self.write_receipt({"archive_sha256": "0" * 64})
        self.assertEqual(self.invoke().status, MODULE.InspectionStatus.archive_mismatch)
        self.assertEqual(list(self.work.iterdir()), [])

    def test_decrypt_failure_timeout_and_oversize_cleanup(self) -> None:
        failing = self.executable("age-fail", "#!/bin/sh\necho private-error >&2\nexit 4\n")
        self.assertEqual(self.invoke(age=failing).status, MODULE.InspectionStatus.decrypt_failed)
        sleeping = self.executable("age-sleep", "#!/usr/bin/python3\nimport time\ntime.sleep(3)\n")
        self.assertEqual(self.invoke(age=sleeping, timeout=0.05).status,
                         MODULE.InspectionStatus.decrypt_failed)
        oversized = self.executable("age-large", "#!/bin/sh\ncat \"$4\"; printf x\n")
        self.assertEqual(self.invoke(age=oversized).status, MODULE.InspectionStatus.decrypt_failed)
        self.assertEqual(list(self.work.iterdir()), [])

    def test_unsafe_archive_nodes_paths_modes_duplicates_and_manifest(self) -> None:
        cases = [
            [("../escape", b"x", "file", 0o600)],
            [("bundle/link", b"", "symlink", 0o600)],
            [("bundle/fifo", b"", "fifo", 0o600)],
            [("bundle/bad\\name", b"x", "file", 0o600)],
            [("bundle/logs/../escape", b"x", "file", 0o600)],
            [("bundle/duplicate", b"x", "file", 0o600),
             ("bundle/duplicate/", b"x", "file", 0o600)],
            [("bundle/setuid", b"x", "file", 0o4600)],
            [("bundle/duplicate", b"x", "file", 0o600),
             ("bundle/duplicate", b"x", "file", 0o600)],
        ]
        for members in cases:
            with self.subTest(members=members):
                self.use_archive(self.make_archive(members=members))
                self.assertEqual(self.invoke().status, MODULE.InspectionStatus.unsafe_archive)
        bad = self.manifest()
        bad["case_id"] = "0000-0000-0000"
        self.use_archive(self.make_archive(manifest=bad))
        self.assertEqual(self.invoke().status, MODULE.InspectionStatus.invalid_manifest)
        undeclared = dict(self.files)
        undeclared["logs/undeclared.txt"] = b"hidden"
        members = [(f"bundle/{name}", contents, "file", 0o600)
                   for name, contents in undeclared.items()]
        self.use_archive(self.make_archive(members=members, manifest=self.manifest()))
        self.assertEqual(self.invoke().status, MODULE.InspectionStatus.invalid_manifest)
        self.use_archive(self.make_archive())
        with mock.patch.object(MODULE, "MAX_FILES", 0):
            self.assertEqual(self.invoke().status, MODULE.InspectionStatus.unsafe_archive)
        with mock.patch.object(MODULE, "MAX_FILE", 1):
            self.assertEqual(self.invoke().status, MODULE.InspectionStatus.unsafe_archive)
        with mock.patch.object(MODULE, "MAX_EXPANDED", 1):
            self.assertEqual(self.invoke().status, MODULE.InspectionStatus.unsafe_archive)
        with mock.patch.object(MODULE, "MAX_MEMBERS", 1):
            self.assertEqual(self.invoke().status, MODULE.InspectionStatus.unsafe_archive)
        self.assertEqual(list(self.work.iterdir()), [])

    def test_unsafe_files_and_cli_output_do_not_disclose(self) -> None:
        for mode in (0o000, 0o200, 0o440, 0o640, 0o644, 0o660, 0o700):
            with self.subTest(mode=oct(mode)):
                self.identity.chmod(mode)
                self.assertEqual(self.invoke().status, MODULE.InspectionStatus.unsafe_input)
        self.identity.chmod(0o600)
        output = io.StringIO()
        success = MODULE.InspectionResult(MODULE.InspectionStatus.inspected, CASE_ID,
                                          ARTIFACT_ID, KEY_ID, "")
        with mock.patch.object(MODULE, "inspect_production", return_value=success), \
                redirect_stdout(output):
            self.assertEqual(MODULE.main([
                "--ciphertext", str(self.ciphertext), "--receipt", str(self.receipt),
                "--identity", str(self.identity),
                "--work-directory", str(self.work)]), 0)
        rendered = output.getvalue()
        self.assertIn("status: inspected", rendered)
        for forbidden in (str(self.root), "AGE-SECRET", "diagnostic bytes", "private-error"):
            self.assertNotIn(forbidden, rendered)
        output = io.StringIO()
        with redirect_stdout(output):
            self.assertEqual(MODULE.main([
                "--ciphertext", str(self.ciphertext), "--receipt", str(self.receipt),
                "--identity", str(self.identity),
                "--work-directory", str(self.work)]), 1)
        self.assertRegex(output.getvalue(), r"^status: [a-z_]+\n$")

    @unittest.skipUnless(REAL_AGE.is_file() and REAL_AGE_KEYGEN.is_file(),
                         "Debian packaged age tools are unavailable")
    def test_real_age_round_trip_and_tamper(self) -> None:
        real_identity = self.root / f"{KEY_ID}.age-identity.txt"
        real_identity.unlink()
        subprocess.run([str(REAL_AGE_KEYGEN), "-o", str(real_identity)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        real_identity.chmod(0o600)
        recipient = subprocess.check_output(
            [str(REAL_AGE_KEYGEN), "-y", str(real_identity)], text=True).strip()
        subprocess.run([str(REAL_AGE), "--encrypt", "--recipient", recipient,
                        "--output", str(self.ciphertext), str(self.archive)], check=True)
        self.write_receipt()
        self.assertEqual(self.invoke(age=REAL_AGE, identity=real_identity).status,
                         MODULE.InspectionStatus.inspected)
        if REAL_AGE == Path("/usr/bin/age"):
            self.assertEqual(MODULE.inspect_production(
                ciphertext=self.ciphertext, receipt_path=self.receipt,
                identity=real_identity, work_directory=self.work).status,
                MODULE.InspectionStatus.inspected)
        data = bytearray(self.ciphertext.read_bytes())
        data[len(data) // 2] ^= 1
        self.ciphertext.write_bytes(data)
        self.write_receipt()
        self.assertEqual(self.invoke(age=REAL_AGE, identity=real_identity).status,
                         MODULE.InspectionStatus.decrypt_failed)
        self.assertEqual(list(self.work.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
