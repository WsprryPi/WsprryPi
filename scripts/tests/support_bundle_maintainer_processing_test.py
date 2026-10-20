#!/usr/bin/env python3

from __future__ import annotations

from contextlib import redirect_stdout
from datetime import datetime, timezone
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import fcntl
import stat
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "scripts/maintainer/process_received_support_bundle.py"
SPEC = importlib.util.spec_from_file_location("maintainer_processing", MODULE_PATH)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)

CASE_ID = "A7K3-M9QF-X2DP"
ARTIFACT_ID = "0123456789abcdef0123456789abcdef"
KEY_ID = "wsprrypi-bundle-2099-01"


class FaultOps(MODULE.FileOps):
    def __init__(self, *, fail_write=False, fail_fsync_at=0, fail_close=False,
                 fail_rename=False, fail_unlink=False, substitute_partial=False):
        self.fail_write = fail_write
        self.fail_fsync_at = fail_fsync_at
        self.fail_close = fail_close
        self.fail_rename = fail_rename
        self.fail_unlink = fail_unlink
        self.substitute_partial = substitute_partial
        self.fsync_count = 0

    def write(self, fd, data):
        if self.fail_write:
            raise OSError("private")
        return super().write(fd, data)

    def fsync(self, fd):
        self.fsync_count += 1
        if self.fsync_count == self.fail_fsync_at:
            raise OSError("private")
        return super().fsync(fd)

    def close(self, fd):
        if self.fail_close:
            self.fail_close = False
            os.close(fd)
            raise OSError("private")
        return super().close(fd)

    def before_partial_revalidate(self, name, *, dir_fd):
        if self.substitute_partial:
            os.rename(name, name + "-original", src_dir_fd=dir_fd, dst_dir_fd=dir_fd)
            os.mkdir(name, 0o700, dir_fd=dir_fd)

    def rename(self, source, destination, *, src_dir_fd, dst_dir_fd):
        if self.fail_rename:
            raise OSError("private")
        return super().rename(source, destination, src_dir_fd=src_dir_fd, dst_dir_fd=dst_dir_fd)

    def unlink(self, name, *, dir_fd):
        if self.fail_unlink and not name.startswith("."):
            raise OSError("private")
        return super().unlink(name, dir_fd=dir_fd)


class MaintainerProcessingTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="wsprrypi-processing-")
        self.root = Path(self.temporary.name)
        self.incoming = self.root / "Incoming"
        self.processed = self.root / "Processed"
        self.work = self.root / "work"
        for path in (self.incoming, self.processed, self.work):
            path.mkdir(mode=0o700)
        self.ciphertext = self.incoming / "Dropbox renamed bundle.age"
        self.ciphertext.write_bytes(b"encrypted diagnostic bytes")
        self.receipt = self.incoming / "downloaded-receipt.json"
        self.identity = self.root / f"{KEY_ID}.age-identity.txt"
        self.identity.write_text("private identity", encoding="ascii")
        self.identity.chmod(0o600)
        self.write_receipt()
        self.now = datetime(2026, 8, 20, 12, 0, 0, tzinfo=timezone.utc)

    def tearDown(self):
        self.temporary.cleanup()

    def write_receipt(self, extra=None):
        cipher = self.ciphertext.read_bytes()
        value = {
            "schema_version": 1, "project_id": "wsprrypi", "case_id": CASE_ID,
            "artifact_id": ARTIFACT_ID, "created_at_utc": "2026-08-17T12:00:00Z",
            "archive_filename": "wsprrypi-support.tar.gz", "archive_size": 10,
            "archive_sha256": "1" * 64,
            "encrypted_filename": f"wsprrypi-support-{CASE_ID}-{ARTIFACT_ID}.tar.gz.age",
            "encrypted_size": len(cipher), "encrypted_sha256": hashlib.sha256(cipher).hexdigest(),
            "bundle_encryption_key_id": KEY_ID,
            "issue_url": "https://github.com/WsprryPi/WsprryPi/issues/414",
            "upload_state": "encrypted_artifact_downloaded",
        }
        value.update(extra or {})
        self.receipt.write_text(json.dumps(value), encoding="utf-8")

    def inspected(self, **kwargs):
        return MODULE.INSPECTOR.InspectionResult(
            MODULE.INSPECTOR.InspectionStatus.inspected, CASE_ID, ARTIFACT_ID, KEY_ID,
            "https://github.com/WsprryPi/WsprryPi/issues/414")

    def invoke(self, **overrides):
        values = dict(incoming=self.incoming, processed=self.processed,
                      ciphertext=self.ciphertext, receipt_path=self.receipt,
                      identity=self.identity, work_directory=self.work,
                      retention_class="active_case", resolved_retention_days=60,
                      repository=ROOT, now_provider=lambda: self.now,
                      inspector=self.inspected, ops=MODULE.FileOps())
        values.update(overrides)
        return MODULE.process_with_test_seam(**values)

    def case_path(self):
        return self.processed / f"case-{CASE_ID}-{ARTIFACT_ID}"

    def test_success_is_atomic_canonical_and_non_disclosing(self):
        result = self.invoke()
        self.assertEqual(result.status, MODULE.ProcessingStatus.processed)
        self.assertFalse(self.ciphertext.exists())
        self.assertFalse(self.receipt.exists())
        case = self.case_path()
        self.assertEqual({p.name for p in case.iterdir()}, {
            "encrypted-support-bundle.age", "support-bundle.receipt.json", "processing-record.json"})
        record = json.loads((case / "processing-record.json").read_text())
        self.assertEqual(record["lifecycle_state"], "processed")
        self.assertEqual(record["retention_review_at_utc"], None)
        rendered = json.dumps(record)
        for forbidden in (str(self.root), "private identity", "diagnostic", "Dropbox names"):
            self.assertNotIn(forbidden, rendered)
        self.assertFalse(any("partial" in item.name for item in self.processed.iterdir()))

    def test_directory_and_input_safety(self):
        self.incoming.chmod(0o755)
        self.assertEqual(self.invoke().status, MODULE.ProcessingStatus.unsafe_directory)
        self.incoming.chmod(0o700)
        linked = self.incoming / "linked.age"
        os.link(self.ciphertext, linked)
        self.assertEqual(self.invoke().status, MODULE.ProcessingStatus.unsafe_input)
        linked.unlink()
        repository_dir = ROOT / ".slice38-test-directory"
        repository_dir.mkdir(mode=0o700)
        try:
            self.assertEqual(self.invoke(processed=repository_dir).status,
                             MODULE.ProcessingStatus.unsafe_directory)
        finally:
            repository_dir.rmdir()

    def test_inspection_failure_and_replacement_fail_closed(self):
        failed = lambda **kwargs: MODULE.INSPECTOR.InspectionResult(
            MODULE.INSPECTOR.InspectionStatus.decrypt_failed)
        self.assertEqual(self.invoke(inspector=failed).status, MODULE.ProcessingStatus.inspection_failed)

        def replace(**kwargs):
            old = self.ciphertext.with_suffix(".old")
            self.ciphertext.rename(old)
            self.ciphertext.write_bytes(b"replacement")
            return self.inspected()
        self.assertEqual(self.invoke(inspector=replace).status, MODULE.ProcessingStatus.unsafe_input)
        self.assertEqual(list(self.processed.iterdir()), [])

    def test_collision_and_mutation_fail_closed(self):
        self.case_path().mkdir(mode=0o700)
        self.assertEqual(self.invoke().status, MODULE.ProcessingStatus.output_collision)

    def test_writer_lock_is_held_while_transition_is_validated(self):
        observed = False

        def inspect_under_lock(**kwargs):
            nonlocal observed
            fd = os.open(self.processed, os.O_RDONLY | os.O_DIRECTORY)
            try:
                with self.assertRaises(BlockingIOError):
                    fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                observed = True
            finally:
                os.close(fd)
            return self.inspected()

        self.assertEqual(self.invoke(inspector=inspect_under_lock).status,
                         MODULE.ProcessingStatus.processed)
        self.assertTrue(observed)

    def test_idempotent_retry_and_cleanup_pending(self):
        self.assertEqual(self.invoke(ops=FaultOps(fail_unlink=True)).status,
                         MODULE.ProcessingStatus.processed_cleanup_pending)
        self.now = datetime(2026, 8, 21, 12, 0, 0, tzinfo=timezone.utc)
        def must_not_inspect(**kwargs):
            raise AssertionError("an exact processed retry must not decrypt")
        self.assertEqual(self.invoke(inspector=must_not_inspect).status,
                         MODULE.ProcessingStatus.unchanged)
        self.assertFalse(self.ciphertext.exists())
        self.assertFalse(self.receipt.exists())

    def test_retention_classes_and_strict_time(self):
        self.assertEqual(self.invoke(retention_class="bad").status,
                         MODULE.ProcessingStatus.processing_record_invalid)
        self.assertEqual(self.invoke(retention_class="resolved_case", resolved_retention_days=29).status,
                         MODULE.ProcessingStatus.processing_record_invalid)
        result = self.invoke(retention_class="uncorrelated")
        self.assertEqual(result.status, MODULE.ProcessingStatus.processed)
        record = json.loads((self.case_path() / "processing-record.json").read_text())
        self.assertEqual(record["retention_review_at_utc"], "2026-09-03T12:00:00Z")

    def test_record_duplicate_unknown_and_same_case_mutation(self):
        self.assertEqual(self.invoke(ops=FaultOps(fail_unlink=True)).status,
                         MODULE.ProcessingStatus.processed_cleanup_pending)
        record_path = self.case_path() / "processing-record.json"
        record_path.write_text('{"schema_version":1,"schema_version":1}', encoding="ascii")
        self.assertEqual(self.invoke().status, MODULE.ProcessingStatus.processing_record_invalid)
        record_path.write_text(json.dumps({"unexpected": True}), encoding="ascii")
        self.assertEqual(self.invoke().status, MODULE.ProcessingStatus.processing_record_invalid)

    def test_faults_leave_no_partial_and_preserve_incoming(self):
        cases = [
            (FaultOps(fail_write=True), MODULE.ProcessingStatus.copy_failed),
            (FaultOps(fail_fsync_at=1), MODULE.ProcessingStatus.copy_failed),
            (FaultOps(fail_close=True), MODULE.ProcessingStatus.copy_failed),
            (FaultOps(fail_rename=True), MODULE.ProcessingStatus.publication_failed),
        ]
        for ops, expected in cases:
            with self.subTest(expected=expected):
                result = self.invoke(ops=ops)
                self.assertEqual(result.status, expected)
                self.assertTrue(self.ciphertext.exists())
                self.assertTrue(self.receipt.exists())
                self.assertEqual(list(self.processed.iterdir()), [])

    def test_post_commit_sync_uncertain_preserves_incoming(self):
        # file x3, partial directory x1, Processed directory x1
        result = self.invoke(ops=FaultOps(fail_fsync_at=5))
        self.assertEqual(result.status, MODULE.ProcessingStatus.committed_sync_uncertain)
        self.assertTrue(self.case_path().is_dir())
        self.assertTrue(self.ciphertext.exists())

    def test_temporary_path_substitution_does_not_publish_replacement(self):
        result = self.invoke(ops=FaultOps(substitute_partial=True))
        self.assertEqual(result.status, MODULE.ProcessingStatus.publication_failed)
        self.assertFalse(self.case_path().exists())
        self.assertTrue(self.ciphertext.exists())
        # The processor cleans the original inode even after its name is replaced.
        self.assertFalse(any(item.name.endswith("-original") for item in self.processed.iterdir()))

    def test_cli_output_is_safe(self):
        output = io.StringIO()
        original = MODULE.process_production
        try:
            MODULE.process_production = lambda **kwargs: MODULE.ProcessingResult(
                MODULE.ProcessingStatus.processed, CASE_ID, ARTIFACT_ID)
            with redirect_stdout(output):
                code = MODULE.main([
                    "--incoming", str(self.incoming), "--processed", str(self.processed),
                    "--ciphertext", str(self.ciphertext), "--receipt", str(self.receipt),
                    "--identity", str(self.identity), "--work-directory", str(self.work),
                    "--retention-class", "active_case"])
            self.assertEqual(code, 0)
        finally:
            MODULE.process_production = original
        rendered = output.getvalue()
        self.assertRegex(rendered, r"^status: processed\ncase ID: [A-Z0-9-]+\nartifact ID: [a-f0-9]+\n$")
        self.assertNotIn(str(self.root), rendered)

    def test_production_forwards_the_explicit_age_executable(self):
        chosen = self.root / "age"
        captured = {}

        def fake_inspect_production(**kwargs):
            captured.update(kwargs)
            return self.inspected()

        def fake_process(**kwargs):
            kwargs["inspector"](sentinel=True)
            return MODULE.ProcessingResult(MODULE.ProcessingStatus.processed)

        with mock.patch.object(MODULE.INSPECTOR, "inspect_production",
                               side_effect=fake_inspect_production), \
                mock.patch.object(MODULE, "process_with_test_seam", side_effect=fake_process):
            self.assertEqual(MODULE.process_production(age=chosen).status,
                             MODULE.ProcessingStatus.processed)
        self.assertEqual(captured, {"age": chosen, "sentinel": True})


if __name__ == "__main__":
    unittest.main()
