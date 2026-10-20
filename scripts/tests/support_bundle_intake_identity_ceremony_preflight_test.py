#!/usr/bin/env python3

from __future__ import annotations

from contextlib import redirect_stdout
import importlib.util
import io
import os
from pathlib import Path
import stat
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "scripts/maintainer/preflight_support_bundle_intake_identity_ceremony.py"
sys.path.insert(0, str(MODULE_PATH.parent))
SPEC = importlib.util.spec_from_file_location("identity_ceremony_preflight", MODULE_PATH)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)

SIGNING_ID = "wsprrypi-intake-2099-01"
BUNDLE_ID = "wsprrypi-bundle-2099-01"


class CeremonyPreflightTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="wsprrypi-identity-preflight-")
        self.root = Path(self.temporary.name)
        self.signing_private = self.directory("signing-private", 0o700)
        self.bundle_private = self.directory("bundle-private", 0o700)
        self.public = self.directory("public", 0o700)
        self.signing_output = self.public / "signing.json"
        self.bundle_output = self.public / "bundle.json"
        self.age = self.executable("age-keygen")
        self.openssl = self.executable("openssl")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def directory(self, name: str, mode: int) -> Path:
        path = self.root / name
        path.mkdir(mode=mode)
        path.chmod(mode)
        return path

    def executable(self, name: str) -> Path:
        path = self.root / name
        path.write_text("#!/bin/sh\nexit 99\n", encoding="ascii")
        path.chmod(0o755)
        return path

    def invoke(self, **overrides) -> MODULE.PreflightResult:
        values = {
            "age_keygen": self.age, "openssl": self.openssl,
            "bundle_private_directory": self.bundle_private,
            "signing_private_directory": self.signing_private,
            "bundle_public_output": self.bundle_output,
            "signing_public_output": self.signing_output,
            "bundle_key_id": BUNDLE_ID, "signing_key_id": SIGNING_ID,
        }
        values.update(overrides)
        return MODULE.preflight(**values)

    def snapshot(self) -> tuple:
        result = []
        for path in sorted(self.root.rglob("*")):
            info = path.lstat()
            result.append((str(path.relative_to(self.root)), stat.S_IFMT(info.st_mode),
                           stat.S_IMODE(info.st_mode),
                           path.read_bytes() if stat.S_ISREG(info.st_mode) else b""))
        return tuple(result)

    def test_ready_is_idempotent_read_only_and_does_not_execute_tools(self) -> None:
        before = self.snapshot()
        for _ in range(2):
            self.assertEqual(self.invoke(), MODULE.PreflightResult(
                MODULE.PreflightStatus.ready, SIGNING_ID, BUNDLE_ID))
        self.assertEqual(self.snapshot(), before)

    def test_each_typed_preflight_failure(self) -> None:
        self.assertEqual(self.invoke(signing_key_id="wrong").status,
                         MODULE.PreflightStatus.invalid_key_ids)
        self.age.chmod(0o777)
        self.assertEqual(self.invoke().status, MODULE.PreflightStatus.unsafe_executable)
        self.age.chmod(0o755)
        self.signing_private.chmod(0o755)
        self.assertEqual(self.invoke().status, MODULE.PreflightStatus.unsafe_private_storage)
        self.signing_private.chmod(0o700)
        self.public.chmod(0o722)
        self.assertEqual(self.invoke().status, MODULE.PreflightStatus.unsafe_public_output)
        self.public.chmod(0o700)
        self.signing_output.write_text("collision", encoding="ascii")
        self.assertEqual(self.invoke().status, MODULE.PreflightStatus.output_collision)
        self.signing_output.unlink()
        with mock.patch.object(MODULE, "collision_paths", side_effect=OSError("injected")):
            self.assertEqual(self.invoke(), MODULE.PreflightResult(
                MODULE.PreflightStatus.preflight_failed))

    def test_rejects_symlinks_repository_overlap_and_private_public_overlap(self) -> None:
        linked_tool = self.root / "linked-tool"
        linked_tool.symlink_to(self.age)
        self.assertEqual(self.invoke(age_keygen=linked_tool).status,
                         MODULE.PreflightStatus.unsafe_executable)
        linked_private = self.root / "linked-private"
        linked_private.symlink_to(self.signing_private, target_is_directory=True)
        self.assertEqual(self.invoke(signing_private_directory=linked_private).status,
                         MODULE.PreflightStatus.unsafe_private_storage)
        self.assertEqual(self.invoke(signing_private_directory=ROOT).status,
                         MODULE.PreflightStatus.unsafe_private_storage)
        linked_public = self.root / "linked-public"
        linked_public.symlink_to(self.public, target_is_directory=True)
        self.assertEqual(self.invoke(signing_public_output=linked_public / "signing.json").status,
                         MODULE.PreflightStatus.unsafe_public_output)
        self.assertEqual(self.invoke(signing_public_output=self.signing_private / "public.json").status,
                         MODULE.PreflightStatus.unsafe_public_output)
        self.assertEqual(self.invoke(signing_public_output=self.bundle_output).status,
                         MODULE.PreflightStatus.unsafe_public_output)

    def test_every_provisioner_collision_path_is_rejected(self) -> None:
        candidates = MODULE.collision_paths(
            self.signing_private, self.bundle_private, self.signing_output,
            self.bundle_output, SIGNING_ID, BUNDLE_ID)
        for index, candidate in enumerate(candidates):
            with self.subTest(candidate=candidate.name):
                candidate.write_text("collision", encoding="ascii")
                if index == 0:
                    hard_link = candidate.with_name(candidate.name + ".link")
                    os.link(candidate, hard_link)
                self.assertEqual(self.invoke().status, MODULE.PreflightStatus.output_collision)
                if index == 0:
                    hard_link.unlink()
                candidate.unlink()
        self.signing_output.symlink_to(self.age)
        self.assertEqual(self.invoke().status, MODULE.PreflightStatus.output_collision)
        self.signing_output.unlink()

    def test_cli_result_does_not_disclose_paths_or_exceptions(self) -> None:
        arguments = ["--age-keygen", str(self.age), "--openssl", str(self.openssl),
                     "--bundle-private-directory", str(self.bundle_private),
                     "--signing-private-directory", str(self.signing_private),
                     "--bundle-public-output", str(self.bundle_output),
                     "--signing-public-output", str(self.signing_output),
                     "--bundle-key-id", BUNDLE_ID, "--signing-key-id", SIGNING_ID]
        self.public.chmod(0o722)
        output = io.StringIO()
        with redirect_stdout(output):
            self.assertEqual(MODULE.main(arguments), 1)
        rendered = output.getvalue()
        self.assertIn("status: unsafe_public_output", rendered)
        for forbidden in (str(self.root), "unsafe public output directory", "collision"):
            self.assertNotIn(forbidden, rendered)


if __name__ == "__main__":
    unittest.main()
