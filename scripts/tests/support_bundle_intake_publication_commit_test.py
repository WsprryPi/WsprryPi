#!/usr/bin/env python3
import base64
import fcntl
import os
import subprocess
import unittest
from unittest import mock
from support_intake_fixture import assert_no_undeclared_crypto, PublicationFixture, ROOT, SIGNATURE, publication


class PublicationTest(unittest.TestCase):
    def setUp(self):
        self.fixture = PublicationFixture()
        self.addCleanup(self.fixture.close)
        self.addCleanup(assert_no_undeclared_crypto, self.fixture.root)

    def test_dry_run_creates_no_objects_or_ref_changes(self) -> None:
        old = self.fixture.head(); objects = self.fixture.run_git(["count-objects", "-v"])
        result = self.fixture.publish()
        self.assertEqual(result.status, publication.PublicationStatus.proposed)
        self.assertEqual(self.fixture.head(), old); self.assertEqual(self.fixture.run_git(["count-objects", "-v"]), objects)

    def test_commit_contains_exact_pair_only_and_preserves_parent(self) -> None:
        old = self.fixture.head(); result = self.fixture.publish(True); new = self.fixture.head()
        self.assertEqual(result.publication_commit, new)
        self.assertEqual(self.fixture.run_git(["rev-list", "--parents", "-n", "1", new]).decode().split(), [new, old])
        changed = self.fixture.run_git(["diff-tree", "--no-commit-id", "--name-only", "-r", old, new]).decode().splitlines()
        self.assertEqual(changed, list(publication.TARGETS))
        self.assertEqual(self.fixture.run_git(["show", f"{new}:{publication.TARGETS[0]}"]),
                         (self.fixture.staging / "generation-1/intake.json").read_bytes())
        self.assertEqual(self.fixture.run_git(["show", f"{new}:{publication.TARGETS[1]}"]),
                         (self.fixture.staging / "generation-1/intake.json.sig").read_bytes())

    def test_repository_policy_rejects_wrong_remote_branch_and_nonbare(self) -> None:
        self.fixture.run_git(["remote", "set-url", "origin", "https://example.invalid/repo.git"])
        with self.assertRaises(publication.PublicationError): self.fixture.publish()
        self.fixture.run_git(["remote", "set-url", "origin", publication.EXPECTED_REMOTE])
        self.fixture.run_git(["remote", "set-url", "--add", "--push", "origin", "https://example.invalid/push.git"])
        with self.assertRaises(publication.PublicationError): self.fixture.publish()
        self.fixture.run_git(["config", "--unset-all", "remote.origin.pushurl"])
        self.fixture.run_git(["symbolic-ref", "HEAD", "refs/heads/devel"])
        with self.assertRaises(publication.PublicationError): self.fixture.publish()
        nonbare = self.fixture.root / "ordinary"; self.fixture.run_git(["init", str(nonbare)], git_dir=False); nonbare.chmod(0o700)
        with self.assertRaises(publication.PublicationError): self.fixture.publish(repository=nonbare)

    def test_competing_ref_update_wins_without_overwrite(self) -> None:
        old = self.fixture.head(); competing = None
        def advance():
            nonlocal competing
            tree = self.fixture.run_git(["show", "-s", "--format=%T", old]).strip().decode()
            competing = self.fixture.run_git(["commit-tree", tree, "-p", old], input_bytes=b"Competing commit\n").strip().decode()
            self.fixture.run_git(["update-ref", "refs/heads/main", competing, old])
        with self.assertRaises(publication.PublicationError): self.fixture.publish(True, _before_update=advance)
        self.assertEqual(self.fixture.head(), competing)

    def test_source_shared_lock_is_held_through_ref_update(self) -> None:
        observed = False
        def probe_lock():
            nonlocal observed
            descriptor = os.open(self.fixture.staging, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
            try:
                with self.assertRaises(BlockingIOError):
                    fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
                observed = True
            finally:
                os.close(descriptor)
        result = self.fixture.publish(True, _before_update=probe_lock)
        self.assertTrue(observed)
        self.assertEqual(result.status, publication.PublicationStatus.committed)

    def test_post_update_verification_failure_rolls_ref_back(self) -> None:
        old = self.fixture.head()
        with mock.patch.object(publication, "verify_commit", side_effect=publication.PublicationError("injected")):
            with self.assertRaises(publication.PublicationError): self.fixture.publish(True)
        self.assertEqual(self.fixture.head(), old)

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
            with self.assertRaises(publication.PublicationRollbackError): self.fixture.publish(True)

    def test_cli_output_is_non_disclosing(self) -> None:
        tool = ROOT / "scripts/maintainer/commit_support_bundle_intake_publication.py"
        completed = subprocess.run(["python3", str(tool), "--git", str(self.fixture.git),
            "--repository", str(self.fixture.repository), "--openssl", str(self.fixture.openssl),
            "--staging-root", str(self.fixture.staging), "--signing-metadata", str(self.fixture.signing)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False)
        self.assertEqual(completed.returncode, 0)
        disclosed = completed.stdout + completed.stderr
        self.assertNotIn("publication-test", disclosed)
        self.assertNotIn(base64.urlsafe_b64encode(SIGNATURE).decode().rstrip("="), disclosed)
        self.assertNotIn(publication.EXPECTED_REMOTE, disclosed)


if __name__ == "__main__": unittest.main()
