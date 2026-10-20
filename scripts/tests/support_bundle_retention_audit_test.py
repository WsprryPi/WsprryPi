#!/usr/bin/env python3

from __future__ import annotations

from contextlib import redirect_stdout
import fcntl
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "scripts/maintainer/audit_support_bundle_retention.py"
SPEC = importlib.util.spec_from_file_location("retention_audit", MODULE_PATH)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)

CASE = "A7K3-M9QF-X2DP"
ARTIFACT = "0123456789abcdef0123456789abcdef"


class RetentionAuditTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="wsprrypi-retention-")
        self.root = Path(self.temporary.name)
        self.processed = self.root / "Processed"
        self.processed.mkdir(mode=0o700)

    def tearDown(self):
        self.temporary.cleanup()

    def add_case(self, case_id=CASE, artifact_id=ARTIFACT,
                 retention_class="uncorrelated", review="2026-09-03T12:00:00Z"):
        case = self.processed / f"case-{case_id}-{artifact_id}"
        case.mkdir(mode=0o700)
        record = {
            "schema_version": 1, "project_id": "wsprrypi", "case_id": case_id,
            "artifact_id": artifact_id, "bundle_encryption_key_id": "wsprrypi-bundle-2099-01",
            "issue_url": None, "encrypted_size": 3,
            "encrypted_sha256": hashlib.sha256(b"age").hexdigest(),
            "received_filename": None, "processed_at_utc": "2026-08-20T12:00:00Z",
            "lifecycle_state": "processed", "retention_class": retention_class,
            "retention_review_at_utc": review,
        }
        receipt = {
            "schema_version": 1, "project_id": "wsprrypi", "case_id": case_id,
            "artifact_id": artifact_id, "created_at_utc": "2026-08-20T11:00:00Z",
            "archive_filename": "wsprrypi-support.tar.gz", "archive_size": 1,
            "archive_sha256": "2" * 64,
            "encrypted_filename": f"wsprrypi-support-{case_id}-{artifact_id}.tar.gz.age",
            "encrypted_size": 3, "encrypted_sha256": hashlib.sha256(b"age").hexdigest(),
            "bundle_encryption_key_id": "wsprrypi-bundle-2099-01", "issue_url": None,
            "upload_state": "encrypted_artifact_downloaded",
        }
        files = {"encrypted-support-bundle.age": b"age",
                 "support-bundle.receipt.json": (json.dumps(receipt) + "\n").encode(),
                 "processing-record.json": (json.dumps(record) + "\n").encode()}
        for name, content in files.items():
            path = case / name
            path.write_bytes(content)
            path.chmod(0o600)
        return case

    def invoke(self, now="2026-09-03T12:00:00Z"):
        return MODULE.audit_retention(self.processed, now, ROOT)

    def test_boundaries_active_and_stable_order(self):
        self.add_case()
        second = "11111111111111111111111111111111"
        self.add_case(artifact_id=second, retention_class="active_case", review=None)
        before = self.invoke("2026-09-03T11:59:59Z")
        self.assertEqual([entry.eligibility for entry in before.entries],
                         [MODULE.Eligibility.retained, MODULE.Eligibility.retained])
        at = self.invoke()
        self.assertEqual(at.status, MODULE.AuditStatus.complete)
        self.assertEqual([entry.artifact_id for entry in at.entries], [ARTIFACT, second])
        self.assertEqual([entry.eligibility for entry in at.entries],
                         [MODULE.Eligibility.due, MODULE.Eligibility.retained])

    def test_invalid_time_and_processed_directory(self):
        self.assertEqual(self.invoke("2026-09-03T12:00:00+00:00").status,
                         MODULE.AuditStatus.invalid_time)
        self.processed.chmod(0o755)
        self.assertEqual(self.invoke().status, MODULE.AuditStatus.unsafe_processed_directory)

    def test_inventory_metadata_and_name_correlation_fail_closed(self):
        case = self.add_case()
        (case / "unexpected").write_text("private")
        self.assertEqual(self.invoke().status, MODULE.AuditStatus.unsafe_case)
        (case / "unexpected").unlink()
        (case / "encrypted-support-bundle.age").chmod(0o644)
        self.assertEqual(self.invoke().status, MODULE.AuditStatus.unsafe_case)

    def test_retention_policy_and_record_time_are_revalidated(self):
        case = self.add_case(review="2026-09-04T12:00:00Z")
        self.assertEqual(self.invoke().status, MODULE.AuditStatus.unsafe_case)

    def test_resolved_retention_requires_integral_30_to_90_days(self):
        case = self.add_case(retention_class="resolved_case", review="2026-09-19T12:00:00Z")
        self.assertEqual(self.invoke().status, MODULE.AuditStatus.complete)
        record_path = case / "processing-record.json"
        record = json.loads(record_path.read_text())
        record["retention_review_at_utc"] = "2026-09-19T12:00:01Z"
        record_path.write_text(json.dumps(record), encoding="utf-8")
        record_path.chmod(0o600)
        self.assertEqual(self.invoke().status, MODULE.AuditStatus.unsafe_case)
        record_path = case / "processing-record.json"
        record = json.loads(record_path.read_text())
        record["retention_review_at_utc"] = "2026-09-03T12:00:00Z"
        record["processed_at_utc"] = "2026-02-30T12:00:00Z"
        record_path.write_text(json.dumps(record), encoding="utf-8")
        record_path.chmod(0o600)
        self.assertEqual(self.invoke().status, MODULE.AuditStatus.unsafe_case)

    def test_ciphertext_and_receipt_correlation_are_revalidated(self):
        case = self.add_case()
        (case / "encrypted-support-bundle.age").write_bytes(b"bad")
        (case / "encrypted-support-bundle.age").chmod(0o600)
        self.assertEqual(self.invoke().status, MODULE.AuditStatus.unsafe_case)

    def test_receipt_mismatch_and_case_bound_fail_closed(self):
        case = self.add_case()
        receipt_path = case / "support-bundle.receipt.json"
        receipt = json.loads(receipt_path.read_text())
        receipt["issue_url"] = "https://github.com/WsprryPi/WsprryPi/issues/414"
        receipt_path.write_text(json.dumps(receipt), encoding="utf-8")
        receipt_path.chmod(0o600)
        self.assertEqual(self.invoke().status, MODULE.AuditStatus.unsafe_case)
        original = MODULE.MAXIMUM_CASES
        MODULE.MAXIMUM_CASES = 0
        try:
            self.assertEqual(self.invoke().status, MODULE.AuditStatus.unsafe_case)
        finally:
            MODULE.MAXIMUM_CASES = original

    def test_bad_names_links_and_records_fail_closed(self):
        case = self.add_case()
        record = case / "processing-record.json"
        record.write_text('{"schema_version":1,"schema_version":1}')
        self.assertEqual(self.invoke().status, MODULE.AuditStatus.unsafe_case)
        case.rename(self.processed / "not-a-case")
        self.assertEqual(self.invoke().status, MODULE.AuditStatus.unsafe_case)

    def test_shared_lock_and_zero_mutation(self):
        case = self.add_case()
        before = {path: path.stat().st_mtime_ns for path in [self.processed, case, *case.iterdir()]}
        observed = False
        def prove_writer_blocked(_audit_fd):
            nonlocal observed
            writer = os.open(self.processed, os.O_RDONLY | os.O_DIRECTORY)
            try:
                with self.assertRaises(BlockingIOError):
                    fcntl.flock(writer, fcntl.LOCK_EX | fcntl.LOCK_NB)
                observed = True
            finally:
                os.close(writer)
        result = MODULE.audit_retention_for_test(
            self.processed, "2026-09-03T12:00:00Z", ROOT, prove_writer_blocked)
        self.assertEqual(result.status, MODULE.AuditStatus.complete)
        self.assertTrue(observed)
        after = {path: path.stat().st_mtime_ns for path in before}
        self.assertEqual(before, after)

    def test_cli_output_is_limited(self):
        self.add_case()
        output = io.StringIO()
        with redirect_stdout(output):
            self.assertEqual(MODULE.main([
                "--processed", str(self.processed), "--now-utc", "2026-09-03T12:00:00Z"]), 0)
        rendered = output.getvalue()
        self.assertIn(f"due: {CASE} {ARTIFACT}\n", rendered)
        for forbidden in (str(self.root), "github.com", "wsprrypi-bundle", "111111"):
            self.assertNotIn(forbidden, rendered)


if __name__ == "__main__":
    unittest.main()
