#!/usr/bin/env python3

from __future__ import annotations

import base64
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/maintainer"))
import commit_support_bundle_intake_publication as publication
import prepare_support_bundle_intake_manifest as preparation


RAW_PUBLIC = bytes(range(32)); SIGNATURE = bytes(range(64))
SIGNING_ID = "wsprrypi-intake-2099-01"; BUNDLE_ID = "wsprrypi-bundle-2099-01"


def encode_recipient(payload: bytes) -> str:
    accumulator = bits = 0; values = []
    for byte in payload:
        accumulator = (accumulator << 8) | byte; bits += 8
        while bits >= 5: bits -= 5; values.append((accumulator >> bits) & 31)
    if bits: values.append((accumulator << (5 - bits)) & 31)
    expanded = [ord(c) >> 5 for c in "age"] + [0] + [ord(c) & 31 for c in "age"]
    polymod = preparation.bech32_polymod(expanded + values + [0] * 6) ^ 1
    checksum = [(polymod >> (5 * (5 - i))) & 31 for i in range(6)]
    return "age1" + "".join(preparation.BECH32_CHARSET[v] for v in values + checksum)


RECIPIENT = encode_recipient(bytes(reversed(range(32))))


def fake_openssl(root: Path) -> Path:
    path = root / "openssl"
    path.write_text(f'''#!/usr/bin/env python3
import sys
if sys.argv[1] == "pkey": sys.stdout.buffer.write({preparation.ED25519_SPKI_PREFIX + RAW_PUBLIC!r}); raise SystemExit(0)
if sys.argv[1] == "pkeyutl" and "-sign" in sys.argv: sys.stdout.buffer.write({SIGNATURE!r}); raise SystemExit(0)
if sys.argv[1] == "pkeyutl" and "-verify" in sys.argv: raise SystemExit(0)
raise SystemExit(64)
'''); path.chmod(0o755); return path


class PublicationTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="wsprrypi-publication-")
        self.root = Path(self.temporary.name)
        self.git = Path("/usr/bin/git")
        if not self.git.exists(): self.git = Path(shutil.which("git") or "")
        self.openssl = fake_openssl(self.root)
        self.staging = self.root / "staging"; self.staging.mkdir(mode=0o700)
        self.private = self.root / "private.pem"; self.private.write_text("test private\n"); self.private.chmod(0o400)
        self.signing = self.root / "signing.json"; self.bundle = self.root / "bundle.json"
        public = base64.urlsafe_b64encode(RAW_PUBLIC).decode().rstrip("=")
        self.signing.write_text(json.dumps({"schema_version": 1, "project_id": "wsprrypi",
            "purpose": "support_intake_manifest_signing", "algorithm": "Ed25519",
            "key_id": SIGNING_ID, "public_key": {"encoding": "base64url", "value": public},
            "created_at_utc": "2099-01-01T00:00:00Z",
            "fingerprint": {"algorithm": "sha256", "value": hashlib.sha256(RAW_PUBLIC).hexdigest()}}) + "\n")
        self.bundle.write_text(json.dumps({"schema_version": 1, "project_id": "wsprrypi",
            "purpose": "support_bundle_encryption", "algorithm": "age-x25519", "key_id": BUNDLE_ID,
            "recipient": RECIPIENT, "created_at_utc": "2099-01-01T00:00:00Z",
            "fingerprint": {"algorithm": "sha256", "value": hashlib.sha256(RECIPIENT.encode()).hexdigest()}}) + "\n")
        preparation.prepare(openssl=self.openssl, private_key=self.private,
            signing_metadata_path=self.signing, bundle_metadata_path=self.bundle,
            staging_directory=self.staging, generation=1, published_at="2099-01-01T01:00:00Z",
            expires_at="2099-04-01T01:00:00Z", status="active", minimum_client_protocol=1,
            minimum_upload_version="1.3.0", request_url="https://www.dropbox.com/request/publication-test",
            release_url="https://github.com/WsprryPi/WsprryPi/releases/latest", user_message=None)
        self.repository = self.root / "support-intake.git"
        self.run_git(["init", "--bare", "--initial-branch=main", str(self.repository)], git_dir=False)
        self.repository.chmod(0o700)
        self.run_git(["remote", "add", "origin", publication.EXPECTED_REMOTE])
        blob = self.run_git(["hash-object", "-w", "--stdin"], input_bytes=b"support intake\n").strip().decode()
        tree = self.run_git(["mktree"], input_bytes=f"100644 blob {blob}\tREADME.md\n".encode()).strip().decode()
        commit = self.run_git(["commit-tree", tree], input_bytes=b"Initialize support intake\n").strip().decode()
        self.run_git(["update-ref", "refs/heads/main", commit])

    def tearDown(self) -> None: self.temporary.cleanup()

    def env(self):
        value = os.environ.copy(); value.update(GIT_AUTHOR_NAME="Test", GIT_AUTHOR_EMAIL="test@invalid",
            GIT_COMMITTER_NAME="Test", GIT_COMMITTER_EMAIL="test@invalid")
        return value

    def run_git(self, args, input_bytes=None, git_dir=True):
        command = [str(self.git)] + (["--git-dir", str(self.repository)] if git_dir else []) + args
        return subprocess.run(command, input=input_bytes, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                              env=self.env(), check=True).stdout

    def head(self): return self.run_git(["rev-parse", "refs/heads/main"]).strip().decode()

    def publish(self, approve=False, **changes):
        values = dict(approve=approve, git_path=self.git, repository=self.repository,
            openssl=self.openssl, staging_root=self.staging, signing_metadata_path=self.signing)
        values.update(changes); return publication.commit_publication(**values)

    def test_dry_run_creates_no_objects_or_ref_changes(self) -> None:
        old = self.head(); objects = self.run_git(["count-objects", "-v"])
        result = self.publish()
        self.assertEqual(result.status, publication.PublicationStatus.proposed)
        self.assertEqual(self.head(), old); self.assertEqual(self.run_git(["count-objects", "-v"]), objects)

    def test_commit_contains_exact_pair_only_and_preserves_parent(self) -> None:
        old = self.head(); result = self.publish(True); new = self.head()
        self.assertEqual(result.publication_commit, new)
        self.assertEqual(self.run_git(["rev-list", "--parents", "-n", "1", new]).decode().split(), [new, old])
        changed = self.run_git(["diff-tree", "--no-commit-id", "--name-only", "-r", old, new]).decode().splitlines()
        self.assertEqual(changed, list(publication.TARGETS))
        self.assertEqual(self.run_git(["show", f"{new}:{publication.TARGETS[0]}"]),
                         (self.staging / "generation-1/intake.json").read_bytes())
        self.assertEqual(self.run_git(["show", f"{new}:{publication.TARGETS[1]}"]),
                         (self.staging / "generation-1/intake.json.sig").read_bytes())

    def test_repository_policy_rejects_wrong_remote_branch_and_nonbare(self) -> None:
        self.run_git(["remote", "set-url", "origin", "https://example.invalid/repo.git"])
        with self.assertRaises(publication.PublicationError): self.publish()
        self.run_git(["remote", "set-url", "origin", publication.EXPECTED_REMOTE])
        self.run_git(["remote", "set-url", "--add", "--push", "origin", "https://example.invalid/push.git"])
        with self.assertRaises(publication.PublicationError): self.publish()
        self.run_git(["config", "--unset-all", "remote.origin.pushurl"])
        self.run_git(["symbolic-ref", "HEAD", "refs/heads/devel"])
        with self.assertRaises(publication.PublicationError): self.publish()
        nonbare = self.root / "ordinary"; self.run_git(["init", str(nonbare)], git_dir=False); nonbare.chmod(0o700)
        with self.assertRaises(publication.PublicationError): self.publish(repository=nonbare)

    def test_competing_ref_update_wins_without_overwrite(self) -> None:
        old = self.head(); competing = None
        def advance():
            nonlocal competing
            tree = self.run_git(["show", "-s", "--format=%T", old]).strip().decode()
            competing = self.run_git(["commit-tree", tree, "-p", old], input_bytes=b"Competing commit\n").strip().decode()
            self.run_git(["update-ref", "refs/heads/main", competing, old])
        with self.assertRaises(publication.PublicationError): self.publish(True, _before_update=advance)
        self.assertEqual(self.head(), competing)

    def test_source_shared_lock_is_held_through_ref_update(self) -> None:
        observed = False
        def probe_lock():
            nonlocal observed
            descriptor = os.open(self.staging, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
            try:
                with self.assertRaises(BlockingIOError):
                    fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
                observed = True
            finally:
                os.close(descriptor)
        result = self.publish(True, _before_update=probe_lock)
        self.assertTrue(observed)
        self.assertEqual(result.status, publication.PublicationStatus.committed)

    def test_post_update_verification_failure_rolls_ref_back(self) -> None:
        old = self.head()
        with mock.patch.object(publication, "verify_commit", side_effect=publication.PublicationError("injected")):
            with self.assertRaises(publication.PublicationError): self.publish(True)
        self.assertEqual(self.head(), old)

    def test_rollback_failure_is_distinct_and_does_not_claim_success(self) -> None:
        real_git = publication.git; updates = 0
        def fail_second_update(git_path, repository, arguments, **kwargs):
            nonlocal updates
            if arguments[:2] == ["update-ref", "refs/heads/main"]:
                updates += 1
                if updates == 2: raise publication.PublicationError("rollback failed")
            return real_git(git_path, repository, arguments, **kwargs)
        with mock.patch.object(publication, "verify_commit", side_effect=publication.PublicationError("verify")), \
             mock.patch.object(publication, "git", side_effect=fail_second_update):
            with self.assertRaises(publication.PublicationRollbackError): self.publish(True)

    def test_cli_output_is_non_disclosing(self) -> None:
        tool = ROOT / "scripts/maintainer/commit_support_bundle_intake_publication.py"
        completed = subprocess.run(["python3", str(tool), "--git", str(self.git),
            "--repository", str(self.repository), "--openssl", str(self.openssl),
            "--staging-root", str(self.staging), "--signing-metadata", str(self.signing)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False)
        self.assertEqual(completed.returncode, 0)
        disclosed = completed.stdout + completed.stderr
        self.assertNotIn("publication-test", disclosed)
        self.assertNotIn(base64.urlsafe_b64encode(SIGNATURE).decode().rstrip("="), disclosed)
        self.assertNotIn(publication.EXPECTED_REMOTE, disclosed)


if __name__ == "__main__": unittest.main()
