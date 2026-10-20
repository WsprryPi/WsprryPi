#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import tempfile
import unittest
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = REPOSITORY_ROOT / "scripts/maintainer/provision_support_bundle_age_key.py"
SPEC = importlib.util.spec_from_file_location("support_bundle_key_provisioning", MODULE_PATH)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)

KEY_ID = "wsprrypi-bundle-2026-01"
SECRET = "AGE-SECRET-KEY-1" + "Q" * 58


def encode_recipient(payload: bytes) -> str:
    accumulator = 0
    bits = 0
    values = []
    for byte in payload:
        accumulator = (accumulator << 8) | byte
        bits += 8
        while bits >= 5:
            bits -= 5
            values.append((accumulator >> bits) & 31)
    if bits:
        values.append((accumulator << (5 - bits)) & 31)
    expanded = [ord(character) >> 5 for character in "age"] + [0]
    expanded += [ord(character) & 31 for character in "age"]
    polymod = MODULE.bech32_polymod(expanded + values + [0] * 6) ^ 1
    checksum = [(polymod >> (5 * (5 - index))) & 31 for index in range(6)]
    return "age1" + "".join(MODULE.BECH32_CHARSET[value] for value in values + checksum)


RECIPIENT = encode_recipient(bytes(range(32)))


def make_generator(directory: Path, mode: str = "success") -> Path:
    path = directory / f"age-keygen-{mode}"
    if mode == "success":
        source = f'''#!/usr/bin/env python3
import pathlib, sys
if sys.argv[1] == "-o":
    pathlib.Path(sys.argv[2]).write_text("{SECRET}\\n", encoding="ascii")
    print("{SECRET}", file=sys.stderr)
    raise SystemExit(0)
if sys.argv[1] == "-y":
    print("{RECIPIENT}")
    raise SystemExit(0)
raise SystemExit(64)
'''
    elif mode.startswith("invalid-recipient"):
        invalid = {
            "invalid-recipient": "not-an-age-recipient\n",
            "invalid-recipient-length": RECIPIENT[:-1] + "\n",
            "invalid-recipient-checksum": RECIPIENT[:-1] + ("q" if RECIPIENT[-1] != "q" else "p") + "\n",
            "invalid-recipient-whitespace": RECIPIENT + " \n",
            "invalid-recipient-extra-line": RECIPIENT + "\nextra\n",
        }[mode]
        source = '''#!/usr/bin/env python3
import pathlib, sys
if sys.argv[1] == "-o":
    pathlib.Path(sys.argv[2]).write_text("private\\n", encoding="ascii")
    raise SystemExit(0)
sys.stdout.write(%r)
''' % invalid
    else:
        source = '''#!/usr/bin/env python3
import pathlib, sys
if len(sys.argv) > 2:
    pathlib.Path(sys.argv[2]).write_text("partial-secret\\n", encoding="ascii")
raise SystemExit(23)
'''
    path.write_text(source, encoding="utf-8")
    path.chmod(0o755)
    return path


class ProvisioningTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="wsprrypi-key-provisioning-")
        self.root = Path(self.temporary.name)
        self.private = self.root / "private"
        self.private.mkdir(mode=0o700)
        self.public = self.root / "public.json"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def provision(self, generator: Path | None = None):
        return MODULE.provision(
            generator or make_generator(self.root),
            self.private,
            self.public,
            KEY_ID,
            "2026-08-16T18:30:00Z",
        )

    def test_success_is_private_and_public_metadata_contains_no_secret(self) -> None:
        identity, public = self.provision()
        self.assertEqual(stat.S_IMODE(identity.stat().st_mode), 0o400)
        self.assertEqual(stat.S_IMODE(public.stat().st_mode), 0o600)
        self.assertEqual(identity.read_text(encoding="ascii"), SECRET + "\n")
        encoded = public.read_text(encoding="utf-8")
        self.assertNotIn("AGE-SECRET-KEY-", encoded)
        metadata = json.loads(encoded)
        self.assertEqual(metadata["key_id"], KEY_ID)
        self.assertEqual(metadata["recipient"], RECIPIENT)
        self.assertEqual(metadata["created_at_utc"], "2026-08-16T18:30:00Z")
        self.assertEqual(
            metadata["fingerprint"],
            {"algorithm": "sha256", "value": hashlib.sha256(RECIPIENT.encode("ascii")).hexdigest()},
        )

    def test_refuses_collisions_without_changing_outputs(self) -> None:
        identity, public = self.provision()
        before_identity = identity.read_bytes()
        before_public = public.read_bytes()
        with self.assertRaises(MODULE.ProvisioningError):
            self.provision()
        self.assertEqual(identity.read_bytes(), before_identity)
        self.assertEqual(public.read_bytes(), before_public)

    def test_rejects_unsafe_directory_and_invalid_id(self) -> None:
        self.private.chmod(0o755)
        with self.assertRaises(MODULE.ProvisioningError):
            self.provision()
        self.private.chmod(0o700)
        with self.assertRaises(MODULE.ProvisioningError):
            MODULE.provision(
                make_generator(self.root), self.private, self.public,
                "wsprrypi-intake-2026-01", "2026-08-16T18:30:00Z"
            )

    def test_invalid_recipient_and_generator_failure_remove_partials(self) -> None:
        for mode in (
            "invalid-recipient",
            "invalid-recipient-length",
            "invalid-recipient-checksum",
            "invalid-recipient-whitespace",
            "invalid-recipient-extra-line",
            "failure",
        ):
            with self.subTest(mode=mode):
                with self.assertRaises(MODULE.ProvisioningError):
                    self.provision(make_generator(self.root, mode))
                self.assertFalse(any(self.private.iterdir()))
                self.assertFalse(self.public.exists())
                self.assertFalse(any(path.name.endswith(".partial") for path in self.root.rglob("*")))

    def test_publication_failures_roll_back_both_final_outputs(self) -> None:
        real_link = MODULE.os.link
        real_unlink = Path.unlink
        identity = self.private / f"{KEY_ID}.age-identity.txt"
        identity_partial_name = f".{KEY_ID}.age-identity.txt.partial"

        def fail_identity_link(source, destination):
            if Path(destination).name == identity.name:
                raise OSError("injected identity publication failure")
            return real_link(source, destination)

        with mock.patch.object(MODULE.os, "link", side_effect=fail_identity_link):
            with self.assertRaises(OSError):
                self.provision()
        self.assertFalse(identity.exists())
        self.assertFalse(self.public.exists())
        self.assertFalse(any(path.name.endswith(".partial") for path in self.root.rglob("*")))

        failed_identity_unlink = False

        def fail_identity_partial_unlink(path, *args, **kwargs):
            nonlocal failed_identity_unlink
            if Path(path).name == identity_partial_name and not failed_identity_unlink:
                failed_identity_unlink = True
                raise OSError("injected post-identity-publication failure")
            return real_unlink(path, *args, **kwargs)

        with mock.patch.object(Path, "unlink", new=fail_identity_partial_unlink):
            with self.assertRaises(OSError):
                self.provision()
        self.assertFalse(identity.exists())
        self.assertFalse(self.public.exists())
        self.assertFalse(any(path.name.endswith(".partial") for path in self.root.rglob("*")))

        def fail_second_link(source, destination):
            if Path(destination).name == self.public.name:
                raise OSError("injected public publication failure")
            return real_link(source, destination)

        with mock.patch.object(MODULE.os, "link", side_effect=fail_second_link):
            with self.assertRaises(OSError):
                self.provision()
        self.assertFalse(identity.exists())
        self.assertFalse(self.public.exists())
        self.assertFalse(any(path.name.endswith(".partial") for path in self.root.rglob("*")))

        entering_rollback = False
        public_cleanup_attempted = False

        def fail_operation_and_one_cleanup(path, *args, **kwargs):
            nonlocal entering_rollback, public_cleanup_attempted
            candidate = Path(path)
            if candidate.name == f".{self.public.name}.partial" and not entering_rollback:
                entering_rollback = True
                raise OSError("injected operation failure")
            if candidate.name == self.public.name and entering_rollback:
                public_cleanup_attempted = True
                raise OSError("injected rollback cleanup failure")
            return real_unlink(path, *args, **kwargs)

        with mock.patch.object(Path, "unlink", new=fail_operation_and_one_cleanup):
            with self.assertRaises(OSError):
                self.provision()
        self.assertTrue(public_cleanup_attempted)
        self.assertTrue(self.public.exists())
        self.assertFalse(identity.exists(), "identity cleanup must continue after public cleanup fails")
        self.public.unlink()
        self.assertFalse(any(path.name.endswith(".partial") for path in self.root.rglob("*")))

        failed_once = False

        def fail_public_partial_unlink(path, *args, **kwargs):
            nonlocal failed_once
            if Path(path).name == f".{self.public.name}.partial" and not failed_once:
                failed_once = True
                raise OSError("injected post-publication failure")
            return real_unlink(path, *args, **kwargs)

        with mock.patch.object(Path, "unlink", new=fail_public_partial_unlink):
            with self.assertRaises(OSError):
                self.provision()
        self.assertFalse(identity.exists())
        self.assertFalse(self.public.exists())
        self.assertFalse(any(path.name.endswith(".partial") for path in self.root.rglob("*")))

    def test_rejects_symlink_executable(self) -> None:
        generator = make_generator(self.root)
        link = self.root / "age-keygen-link"
        link.symlink_to(generator)
        with self.assertRaises(MODULE.ProvisioningError):
            self.provision(link)

    @unittest.skipUnless(os.environ.get("WSPRRYPI_REAL_AGE_KEYGEN"), "real age-keygen not requested")
    def test_real_age_keygen_produces_matching_public_metadata(self) -> None:
        identity, public = self.provision(Path(os.environ["WSPRRYPI_REAL_AGE_KEYGEN"]))
        metadata = json.loads(public.read_text(encoding="utf-8"))
        recipient = metadata["recipient"]
        self.assertTrue(recipient.startswith("age1"))
        self.assertEqual(stat.S_IMODE(identity.stat().st_mode), 0o400)
        self.assertEqual(
            metadata["fingerprint"]["value"],
            hashlib.sha256(recipient.encode("ascii")).hexdigest(),
        )
        self.assertNotIn("AGE-SECRET-KEY-", public.read_text(encoding="utf-8"))
        self.assertIn("AGE-SECRET-KEY-", identity.read_text(encoding="ascii"))


if __name__ == "__main__":
    unittest.main()
