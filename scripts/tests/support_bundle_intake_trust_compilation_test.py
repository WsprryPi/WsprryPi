#!/usr/bin/env python3

from __future__ import annotations

import base64
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "scripts/maintainer/compile_support_bundle_intake_trust.py"
sys.path.insert(0, str(MODULE_PATH.parent))
SPEC = importlib.util.spec_from_file_location("intake_trust_compilation", MODULE_PATH)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def recipient(payload: bytes) -> str:
    charset = MODULE.age_provisioning.BECH32_CHARSET
    accumulator = bits = 0
    values = []
    for byte in payload:
        accumulator = (accumulator << 8) | byte
        bits += 8
        while bits >= 5:
            bits -= 5
            values.append((accumulator >> bits) & 31)
    if bits:
        values.append((accumulator << (5 - bits)) & 31)
    expanded = [ord(c) >> 5 for c in "age"] + [0] + [ord(c) & 31 for c in "age"]
    polymod = MODULE.age_provisioning.bech32_polymod(expanded + values + [0] * 6) ^ 1
    values += [(polymod >> (5 * (5 - index))) & 31 for index in range(6)]
    return "age1" + "".join(charset[value] for value in values)


class TrustCompilationTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="wsprrypi-trust-compilation-")
        self.root = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def signing(self, suffix: str, raw: bytes) -> Path:
        encoded = base64.urlsafe_b64encode(raw).decode("ascii").rstrip("=")
        value = {
            "schema_version": 1, "project_id": "wsprrypi",
            "purpose": "support_intake_manifest_signing", "algorithm": "Ed25519",
            "key_id": f"wsprrypi-intake-2099-{suffix}",
            "public_key": {"encoding": "base64url", "value": encoded},
            "created_at_utc": "2099-01-01T00:00:00Z",
            "fingerprint": {"algorithm": "sha256", "value": hashlib.sha256(raw).hexdigest()},
        }
        return self.write(f"signing-{suffix}.json", value)

    def bundle(self, suffix: str, raw: bytes) -> Path:
        public = recipient(raw)
        value = {
            "schema_version": 1, "project_id": "wsprrypi",
            "purpose": "support_bundle_encryption", "algorithm": "age-x25519",
            "key_id": f"wsprrypi-bundle-2099-{suffix}", "recipient": public,
            "created_at_utc": "2099-01-01T00:00:00Z",
            "fingerprint": {"algorithm": "sha256",
                            "value": hashlib.sha256(public.encode("ascii")).hexdigest()},
        }
        return self.write(f"bundle-{suffix}.json", value)

    def write(self, name: str, value: object) -> Path:
        path = self.root / name
        path.write_text(json.dumps(value) + "\n", encoding="utf-8")
        path.chmod(0o600)
        return path

    def test_deterministic_minimal_header_compiles(self) -> None:
        signing_1 = self.signing("01", bytes(range(32)))
        signing_2 = self.signing("02", bytes(reversed(range(32))))
        bundle_1 = self.bundle("01", bytes(range(32)))
        bundle_2 = self.bundle("02", bytes(reversed(range(32))))
        first = self.root / "first.hpp"
        second = self.root / "second.hpp"
        MODULE.compile_trust([signing_2, signing_1], [bundle_2, bundle_1], first)
        MODULE.compile_trust([signing_1, signing_2], [bundle_1, bundle_2], second)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        text = first.read_text(encoding="ascii")
        self.assertLess(text.index("wsprrypi-intake-2099-01"),
                        text.index("wsprrypi-intake-2099-02"))
        self.assertLess(text.index("wsprrypi-bundle-2099-01"),
                        text.index("wsprrypi-bundle-2099-02"))
        for forbidden in (recipient(bytes(range(32))), "fingerprint", str(signing_1),
                          "created_at_utc", "request_url", "private"):
            self.assertNotIn(forbidden, text)
        compiler = shutil.which("c++")
        if compiler is None:
            self.skipTest("C++ compiler is unavailable")
        source = self.root / "check.cpp"
        source.write_text(f'#include "{first}"\nint main() {{ return support_bundle_intake_compiled_trust().signing_keys.size() == 2 ? 0 : 1; }}\n')
        completed = subprocess.run([compiler, "-std=c++20", "-fsyntax-only", "-I", str(ROOT / "src"),
                                    str(source)], check=False, timeout=30,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(completed.returncode, 0, completed.stderr.decode())

    def test_rejects_duplicate_ids_and_json_keys(self) -> None:
        signing = self.signing("01", bytes(range(32)))
        bundle = self.bundle("01", bytes(range(32)))
        with self.assertRaises(MODULE.CompilationError):
            MODULE.compile_trust([signing, signing], [bundle], self.root / "duplicate.hpp")
        duplicate = self.root / "nested-duplicate.json"
        duplicate.write_text(signing.read_text().replace('"encoding": "base64url"',
                             '"encoding": "base64url", "encoding": "base64url"'))
        duplicate.chmod(0o600)
        with self.assertRaises(MODULE.CompilationError):
            MODULE.compile_trust([duplicate], [bundle], self.root / "nested.hpp")
        top_duplicate = self.root / "top-duplicate.json"
        top_duplicate.write_text(signing.read_text().replace('"schema_version": 1',
                                 '"schema_version": 1, "schema_version": 1'))
        top_duplicate.chmod(0o600)
        with self.assertRaises(MODULE.CompilationError):
            MODULE.compile_trust([top_duplicate], [bundle], self.root / "top.hpp")

    def test_rejects_schema_encoding_fingerprint_and_recipient_mutations(self) -> None:
        signing = self.signing("01", bytes(range(32)))
        bundle = self.bundle("01", bytes(range(32)))
        signing_value = json.loads(signing.read_text())
        bundle_value = json.loads(bundle.read_text())
        mutations = []
        for field, value in (("schema_version", True), ("project_id", "other"),
                             ("created_at_utc", "2099-02-30T00:00:00Z"),
                             ("unexpected", "field")):
            changed = dict(signing_value); changed[field] = value; mutations.append(changed)
        changed = json.loads(signing.read_text()); changed["public_key"]["value"] = "A" * 43
        mutations.append(changed)
        changed = json.loads(signing.read_text()); changed["fingerprint"]["value"] = "0" * 64
        mutations.append(changed)
        for index, changed in enumerate(mutations):
            path = self.write(f"bad-signing-{index}.json", changed)
            with self.assertRaises(MODULE.CompilationError):
                MODULE.compile_trust([path], [bundle], self.root / f"bad-{index}.hpp")
        bundle_value["recipient"] = bundle_value["recipient"][:-1]
        bad_bundle = self.write("bad-bundle.json", bundle_value)
        with self.assertRaises(MODULE.CompilationError):
            MODULE.compile_trust([signing], [bad_bundle], self.root / "bad-bundle.hpp")
        with self.assertRaises(MODULE.CompilationError):
            MODULE.compile_trust([signing] * 17, [bundle], self.root / "too-many.hpp")

    def test_rejects_unsafe_input_and_preserves_prior_output(self) -> None:
        signing = self.signing("01", bytes(range(32)))
        bundle = self.bundle("01", bytes(range(32)))
        signing.chmod(0o622)
        with self.assertRaises(MODULE.CompilationError):
            MODULE.compile_trust([signing], [bundle], self.root / "unsafe.hpp")
        signing.chmod(0o600)
        linked = self.root / "linked.json"
        os.link(signing, linked)
        with self.assertRaises(MODULE.CompilationError):
            MODULE.compile_trust([signing], [bundle], self.root / "linked.hpp")
        linked.unlink()
        symlink = self.root / "symlink.json"
        symlink.symlink_to(signing)
        with self.assertRaises(OSError):
            MODULE.compile_trust([symlink], [bundle], self.root / "symlink.hpp")
        symlink.unlink()
        self.root.chmod(0o722)
        with self.assertRaises(MODULE.CompilationError):
            MODULE.compile_trust([signing], [bundle], self.root / "unsafe-parent.hpp")
        self.root.chmod(0o700)
        output = self.root / "trust.hpp"
        output.write_bytes(b"prior\n")
        with mock.patch.object(MODULE.os, "replace", side_effect=OSError("injected")):
            with self.assertRaises(OSError):
                MODULE.compile_trust([signing], [bundle], output)
        self.assertEqual(output.read_bytes(), b"prior\n")
        self.assertFalse((self.root / ".trust.hpp.partial").exists())


if __name__ == "__main__":
    unittest.main()
