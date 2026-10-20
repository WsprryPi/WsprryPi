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
MODULE_PATH = ROOT / "scripts/maintainer/delete_expired_support_bundle.py"
SPEC = importlib.util.spec_from_file_location("retention_deletion", MODULE_PATH)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)

CASE = "A7K3-M9QF-X2DP"
ARTIFACT = "0123456789abcdef0123456789abcdef"
OTHER = "11111111111111111111111111111111"
NOW = "2026-09-03T12:00:00Z"


class FaultOps(MODULE.FileOps):
    def __init__(self, *, fail_rename=False, fail_fsync_at=0, fail_unlink="",
                 fail_rmdir=False, substitute="", substitute_tombstone=False,
                 prove_lock=None):
        self.fail_rename = fail_rename
        self.fail_fsync_at = fail_fsync_at
        self.fail_unlink = fail_unlink
        self.fail_rmdir = fail_rmdir
        self.substitute = substitute
        self.substitute_tombstone = substitute_tombstone
        self.prove_lock = prove_lock
        self.fsync_count = 0

    def rename(self, source, destination, *, directory_fd):
        if self.prove_lock:
            self.prove_lock()
        if self.fail_rename:
            raise OSError("private")
        return super().rename(source, destination, directory_fd=directory_fd)

    def fsync(self, fd):
        self.fsync_count += 1
        if self.fsync_count == self.fail_fsync_at:
            raise OSError("private")
        return super().fsync(fd)

    def unlink(self, name, *, directory_fd):
        if name == self.fail_unlink:
            self.fail_unlink = ""
            raise OSError("private")
        return super().unlink(name, directory_fd=directory_fd)

    def rmdir(self, name, *, directory_fd):
        if self.fail_rmdir:
            self.fail_rmdir = False
            raise OSError("private")
        return super().rmdir(name, directory_fd=directory_fd)

    def before_unlink(self, name, *, directory_fd):
        if name == self.substitute:
            self.substitute = ""
            os.rename(name, name + ".old", src_dir_fd=directory_fd, dst_dir_fd=directory_fd)
            fd = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600,
                         dir_fd=directory_fd)
            try:
                os.write(fd, b"replacement")
            finally:
                os.close(fd)

    def before_tombstone_revalidate(self, name, *, directory_fd):
        if self.substitute_tombstone:
            self.substitute_tombstone = False
            os.rename(name, name + ".moved", src_dir_fd=directory_fd, dst_dir_fd=directory_fd)
            os.mkdir(name, 0o700, dir_fd=directory_fd)


class RetentionDeletionTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="wsprrypi-retention-delete-")
        self.root = Path(self.temporary.name)
        self.processed = self.root / "Processed"
        self.processed.mkdir(mode=0o700)
        self.add_case()

    def tearDown(self):
        self.temporary.cleanup()

    def add_case(self, case_id=CASE, artifact_id=ARTIFACT,
                 retention_class="uncorrelated", review=NOW):
        case = self.processed / f"case-{case_id}-{artifact_id}"
        case.mkdir(mode=0o700)
        digest = hashlib.sha256(b"age").hexdigest()
        record = {
            "schema_version": 1, "project_id": "wsprrypi", "case_id": case_id,
            "artifact_id": artifact_id, "bundle_encryption_key_id": "wsprrypi-bundle-2099-01",
            "issue_url": None, "encrypted_size": 3, "encrypted_sha256": digest,
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
            "encrypted_size": 3, "encrypted_sha256": digest,
            "bundle_encryption_key_id": "wsprrypi-bundle-2099-01", "issue_url": None,
            "upload_state": "encrypted_artifact_downloaded",
        }
        values = {"encrypted-support-bundle.age": b"age",
                  "support-bundle.receipt.json": (json.dumps(receipt) + "\n").encode(),
                  "processing-record.json": (json.dumps(record) + "\n").encode()}
        for name, value in values.items():
            path = case / name
            path.write_bytes(value)
            path.chmod(0o600)
        return case

    def invoke(self, **overrides):
        values = dict(processed=self.processed, case_id=CASE, artifact_id=ARTIFACT,
                      now_utc=NOW, confirmation=f"DELETE {CASE} {ARTIFACT}",
                      repository=ROOT, ops=MODULE.FileOps())
        values.update(overrides)
        return MODULE.delete_expired_support_bundle_for_test(**values)

    def source(self):
        return self.processed / f"case-{CASE}-{ARTIFACT}"

    def tombstone(self):
        return self.processed / f".retiring-{CASE}-{ARTIFACT}"

    def test_success_and_absent_retry_preserve_unrelated_case(self):
        other = self.add_case(artifact_id=OTHER)
        result = self.invoke()
        self.assertEqual(result.status, MODULE.DeleteStatus.deleted)
        self.assertFalse(self.source().exists())
        self.assertFalse(self.tombstone().exists())
        self.assertTrue(other.exists())
        self.assertEqual(self.invoke().status, MODULE.DeleteStatus.absent)

    def test_confirmation_request_and_retention_fail_closed(self):
        self.assertEqual(self.invoke(confirmation="DELETE").status,
                         MODULE.DeleteStatus.confirmation_failed)
        self.assertEqual(self.invoke(now_utc="bad").status, MODULE.DeleteStatus.invalid_request)
        self.assertEqual(self.invoke(case_id=None).status, MODULE.DeleteStatus.invalid_request)
        self.assertEqual(self.invoke(now_utc="2026-09-03T11:59:59Z").status,
                         MODULE.DeleteStatus.retained)
        self.assertTrue(self.source().exists())

    def test_active_case_is_never_deleted(self):
        self.source().rename(self.processed / "old")
        self.add_case(retention_class="active_case", review=None)
        self.assertEqual(self.invoke().status, MODULE.DeleteStatus.retained)

    def test_exclusive_lock_is_held_before_rename(self):
        observed = False
        def prove():
            nonlocal observed
            fd = os.open(self.processed, os.O_RDONLY | os.O_DIRECTORY)
            try:
                with self.assertRaises(BlockingIOError):
                    fcntl.flock(fd, fcntl.LOCK_SH | fcntl.LOCK_NB)
                observed = True
            finally:
                os.close(fd)
        self.assertEqual(self.invoke(ops=FaultOps(prove_lock=prove)).status,
                         MODULE.DeleteStatus.deleted)
        self.assertTrue(observed)

    def test_rename_collision_and_sync_uncertainty_preserve_tombstone(self):
        self.assertEqual(self.invoke(ops=FaultOps(fail_rename=True)).status,
                         MODULE.DeleteStatus.publication_failed)
        self.assertTrue(self.source().exists())
        result = self.invoke(ops=FaultOps(fail_fsync_at=1))
        self.assertEqual(result.status, MODULE.DeleteStatus.committed_sync_uncertain)
        self.assertFalse(self.source().exists())
        self.assertTrue(self.tombstone().exists())
        self.assertEqual(self.invoke().status, MODULE.DeleteStatus.resumed_deleted)

    def test_existing_source_and_tombstone_is_collision(self):
        self.tombstone().mkdir(mode=0o700)
        self.assertEqual(self.invoke().status, MODULE.DeleteStatus.collision)
        self.assertTrue(self.source().exists())

    def test_payload_and_record_partial_cleanup_resume(self):
        result = self.invoke(ops=FaultOps(fail_unlink="support-bundle.receipt.json"))
        self.assertEqual(result.status, MODULE.DeleteStatus.cleanup_pending)
        self.assertEqual({path.name for path in self.tombstone().iterdir()},
                         {"support-bundle.receipt.json", "processing-record.json"})
        self.assertEqual(self.invoke().status, MODULE.DeleteStatus.resumed_deleted)

        self.add_case()
        result = self.invoke(ops=FaultOps(fail_unlink="processing-record.json"))
        self.assertEqual(result.status, MODULE.DeleteStatus.cleanup_pending)
        self.assertEqual({path.name for path in self.tombstone().iterdir()},
                         {"processing-record.json"})
        self.assertEqual(self.invoke().status, MODULE.DeleteStatus.resumed_deleted)

    def test_empty_tombstone_resume_after_rmdir_failure(self):
        self.assertEqual(self.invoke(ops=FaultOps(fail_rmdir=True)).status,
                         MODULE.DeleteStatus.cleanup_pending)
        self.assertEqual(list(self.tombstone().iterdir()), [])
        self.assertEqual(self.invoke().status, MODULE.DeleteStatus.resumed_deleted)

    def test_intermediate_sync_failures_are_resumable(self):
        self.assertEqual(self.invoke(ops=FaultOps(fail_fsync_at=2)).status,
                         MODULE.DeleteStatus.cleanup_pending)
        self.assertTrue((self.tombstone() / "processing-record.json").exists())
        self.assertEqual(self.invoke().status, MODULE.DeleteStatus.resumed_deleted)

        self.add_case()
        self.assertEqual(self.invoke(ops=FaultOps(fail_fsync_at=3)).status,
                         MODULE.DeleteStatus.cleanup_pending)
        self.assertEqual(list(self.tombstone().iterdir()), [])
        self.assertEqual(self.invoke().status, MODULE.DeleteStatus.resumed_deleted)

    def test_final_directory_sync_uncertainty_is_truthful(self):
        result = self.invoke(ops=FaultOps(fail_fsync_at=4))
        self.assertEqual(result.status, MODULE.DeleteStatus.committed_sync_uncertain)
        self.assertFalse(self.source().exists())
        self.assertFalse(self.tombstone().exists())
        self.assertEqual(self.invoke().status, MODULE.DeleteStatus.absent)

    def test_unsafe_resumed_file_is_typed_and_preserved(self):
        self.assertEqual(self.invoke(ops=FaultOps(fail_unlink="encrypted-support-bundle.age")).status,
                         MODULE.DeleteStatus.cleanup_pending)
        receipt = self.tombstone() / "support-bundle.receipt.json"
        receipt.chmod(0o644)
        self.assertEqual(self.invoke().status, MODULE.DeleteStatus.unsafe)
        self.assertTrue(receipt.exists())

    def test_mutation_unexpected_inventory_and_substitution_fail_closed(self):
        (self.source() / "encrypted-support-bundle.age").write_bytes(b"bad")
        self.assertEqual(self.invoke().status, MODULE.DeleteStatus.unsafe)
        self.assertTrue(self.source().exists())

        self.source().rename(self.processed / "old")
        case = self.add_case()
        extra = case / "unexpected"
        extra.write_text("private")
        extra.chmod(0o600)
        self.assertEqual(self.invoke().status, MODULE.DeleteStatus.unsafe)

    def test_path_substitution_does_not_delete_replacement(self):
        result = self.invoke(ops=FaultOps(substitute="encrypted-support-bundle.age"))
        self.assertEqual(result.status, MODULE.DeleteStatus.unsafe)
        self.assertTrue((self.tombstone() / "encrypted-support-bundle.age").exists())

    def test_tombstone_path_substitution_does_not_remove_replacement(self):
        result = self.invoke(ops=FaultOps(substitute_tombstone=True))
        self.assertEqual(result.status, MODULE.DeleteStatus.cleanup_pending)
        self.assertTrue(self.tombstone().is_dir())
        self.assertTrue((self.processed / (self.tombstone().name + ".moved")).is_dir())

    def test_cli_output_is_non_disclosing(self):
        self.assertNotIn("--now-utc", Path(MODULE_PATH).read_text())
        output = io.StringIO()
        original = MODULE.delete_expired_support_bundle
        try:
            MODULE.delete_expired_support_bundle = lambda **kwargs: MODULE.DeleteResult(
                MODULE.DeleteStatus.deleted, CASE, ARTIFACT)
            with redirect_stdout(output):
                code = MODULE.main(["--processed", str(self.processed), "--case-id", CASE,
                                    "--artifact-id", ARTIFACT,
                                    "--confirm", f"DELETE {CASE} {ARTIFACT}"])
            self.assertEqual(code, 0)
        finally:
            MODULE.delete_expired_support_bundle = original
        rendered = output.getvalue()
        self.assertRegex(rendered, rf"^status: deleted\ncase ID: {CASE}\nartifact ID: {ARTIFACT}\n$")
        for forbidden in (str(self.root), "github.com", "sha256", "wsprrypi-bundle"):
            self.assertNotIn(forbidden, rendered)


if __name__ == "__main__":
    unittest.main()
