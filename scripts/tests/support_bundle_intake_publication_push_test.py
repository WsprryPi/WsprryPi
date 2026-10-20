#!/usr/bin/env python3

from __future__ import annotations

import contextlib
import fcntl
import io
import os
from pathlib import Path
import subprocess
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/maintainer")); sys.path.insert(0, str(ROOT / "scripts/tests"))
import push_support_bundle_intake_publication as push
import support_bundle_intake_publication_commit_test as fixture_module


class Transport:
    def __init__(self, responses, push_result=True, push_error=False, on_push=None):
        self.responses = list(responses); self.push_result = push_result; self.push_error = push_error
        self.on_push = on_push; self.pushes = []
    def query_main(self):
        value = self.responses.pop(0)
        if isinstance(value, Exception): raise value
        return value
    def push(self, parent, candidate):
        self.pushes.append((parent, candidate))
        if self.on_push: self.on_push()
        if self.push_error: raise RuntimeError("rejected")
        return self.push_result


class PushTest(unittest.TestCase):
    def setUp(self):
        self.fixture = fixture_module.PublicationTest(methodName="runTest"); self.fixture.setUp()
        committed = self.fixture.publish(True)
        self.candidate = committed.publication_commit; self.parent = committed.previous_commit
    def tearDown(self): self.fixture.tearDown()
    def line(self, oid): return f"{oid}\trefs/heads/main\n".encode()
    def resolve(self, transport, approve=False, helper="osxkeychain"):
        f = self.fixture
        return push.resolve_for_test(approve=approve, git_path=f.git, repository=f.repository,
            openssl=f.openssl, staging_root=f.staging, signing_metadata_path=f.signing,
            credential_helper=helper, transport=transport)

    def test_proposed_does_not_push_and_exact_publish_confirms(self):
        proposed_transport = Transport([self.line(self.parent)])
        self.assertEqual(self.resolve(proposed_transport).status, push.PushStatus.proposed)
        self.assertEqual(proposed_transport.pushes, [])
        transport = Transport([self.line(self.parent), self.line(self.candidate)])
        result = self.resolve(transport, True)
        self.assertEqual(result.status, push.PushStatus.published)
        self.assertEqual(transport.pushes, [(self.parent, self.candidate)])

    def test_already_published_is_idempotent_without_push(self):
        transport = Transport([self.line(self.candidate)])
        self.assertEqual(self.resolve(transport, True).status, push.PushStatus.already_published)
        self.assertFalse(transport.pushes)

    def test_conflict_absent_multiple_and_malformed_fail_closed(self):
        other = "a" * len(self.parent)
        self.assertEqual(self.resolve(Transport([self.line(other)]), True).status, push.PushStatus.remote_conflict)
        for response in (b"", self.line(self.parent) + self.line(self.candidate),
                         b"bad\trefs/heads/main\n", b"a" * 41 + b"\trefs/heads/main\n"):
            with self.subTest(response=response):
                self.assertEqual(self.resolve(Transport([response]), True).status, push.PushStatus.remote_invalid)
        self.assertEqual(self.resolve(Transport([RuntimeError("query failed")]), True).status,
                         push.PushStatus.query_failed)

    def test_rejection_and_confirmation_failures_are_truthful(self):
        self.assertEqual(self.resolve(Transport([self.line(self.parent)], push_result=False), True).status,
                         push.PushStatus.push_rejected)
        self.assertEqual(self.resolve(Transport([self.line(self.parent)], push_error=True), True).status,
                         push.PushStatus.push_rejected)
        self.assertEqual(self.resolve(Transport([self.line(self.parent), RuntimeError("down")]), True).status,
                         push.PushStatus.pushed_confirmation_uncertain)
        self.assertEqual(self.resolve(Transport([self.line(self.parent), self.line(self.parent)]), True).status,
                         push.PushStatus.pushed_confirmation_uncertain)

    def test_source_lock_is_held_during_push(self):
        observed = False
        def probe():
            nonlocal observed
            descriptor = os.open(self.fixture.staging, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
            try:
                with self.assertRaises(BlockingIOError): fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
                observed = True
            finally: os.close(descriptor)
        result = self.resolve(Transport([self.line(self.parent), self.line(self.candidate)], on_push=probe), True)
        self.assertEqual(result.status, push.PushStatus.published); self.assertTrue(observed)

    def test_invalid_helper_and_source_mutation_never_query_or_push(self):
        transport = Transport([self.line(self.parent)])
        self.assertEqual(self.resolve(transport, True, "shell-helper").status, push.PushStatus.failed)
        manifest = self.fixture.staging / "generation-1/intake.json"
        manifest.write_bytes(manifest.read_bytes().replace(b"publication-test", b"evil-publication"))
        result = self.resolve(transport, True)
        self.assertEqual(result.status, push.PushStatus.failed); self.assertFalse(transport.pushes)

    def test_production_push_argv_has_exact_lease_and_helper(self):
        completed = subprocess.CompletedProcess([], 0, stdout=b"ok\n", stderr=b"")
        transport = push.GitRemoteTransport(self.fixture.git, self.fixture.repository, "osxkeychain")
        with mock.patch.object(push.subprocess, "run", return_value=completed) as invoked:
            self.assertTrue(transport.push(self.parent, self.candidate))
        command = invoked.call_args.args[0]
        self.assertIn("credential.helper=osxkeychain", command)
        self.assertIn(f"--force-with-lease=refs/heads/main:{self.parent}", command)
        self.assertEqual(command[-2:], ["origin", f"{self.candidate}:refs/heads/main"])

    def test_cli_summary_omits_sensitive_fields(self):
        result = push.PushResult(push.PushStatus.proposed, 1, "active", "signing-id", "bundle-id",
                                 "a" * 64, self.parent, self.candidate)
        argv = ["--git", "/usr/bin/git", "--repository", "/tmp/repo", "--openssl", "/tmp/openssl",
                "--staging-root", "/tmp/staging", "--signing-metadata", "/tmp/signing",
                "--credential-helper", "osxkeychain"]
        output = io.StringIO()
        with mock.patch.object(push, "resolve", return_value=result), contextlib.redirect_stdout(output):
            self.assertEqual(push.main(argv), 0)
        self.assertNotIn("dropbox", output.getvalue().lower()); self.assertNotIn("signature", output.getvalue().lower())


if __name__ == "__main__": unittest.main()
