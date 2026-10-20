#!/usr/bin/env python3

from __future__ import annotations

import base64
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "config/support-bundle-intake"
SIGNING = CONFIG / "wsprrypi-intake-2026-01.public.json"
BUNDLE = CONFIG / "wsprrypi-bundle-2026-01.public.json"
HEADER = ROOT / "src/support_bundle_intake_compiled_trust.hpp"
MODULE_PATH = ROOT / "scripts/maintainer/compile_support_bundle_intake_trust.py"
sys.path.insert(0, str(MODULE_PATH.parent))
SPEC = importlib.util.spec_from_file_location("production_trust_compilation", MODULE_PATH)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)

SIGNING_ID = "wsprrypi-intake-2026-01"
BUNDLE_ID = "wsprrypi-bundle-2026-01"
CREATED_AT = "2026-08-17T12:47:58Z"
SIGNING_FINGERPRINT = "688b5769d2b763481bad938fe8a9963693950c5e80bcf6d47d71db75711843ac"
BUNDLE_FINGERPRINT = "61289289afbd0f7813eb59b54e60d514f3cd8dbdf05e9c6b2d405b101b5b0fc4"


class ProductionTrustTest(unittest.TestCase):
    def test_public_metadata_is_exact_and_self_consistent(self) -> None:
        signing = json.loads(SIGNING.read_text(encoding="utf-8"))
        bundle = json.loads(BUNDLE.read_text(encoding="utf-8"))
        self.assertEqual(signing, {
            "schema_version": 1,
            "project_id": "wsprrypi",
            "purpose": "support_intake_manifest_signing",
            "algorithm": "Ed25519",
            "key_id": SIGNING_ID,
            "public_key": {"encoding": "base64url",
                           "value": "wRJvj70F1YTXv0lFahS5OeoS5rwzRum5WeJv-g1pcD4"},
            "created_at_utc": CREATED_AT,
            "fingerprint": {"algorithm": "sha256", "value": SIGNING_FINGERPRINT},
        })
        self.assertEqual(bundle, {
            "schema_version": 1,
            "project_id": "wsprrypi",
            "purpose": "support_bundle_encryption",
            "algorithm": "age-x25519",
            "key_id": BUNDLE_ID,
            "recipient": "age1s23dd6mfm5c2prurxreyz4vyclzg8sqn0lu84m98k4m00gtcmg3q8jjqpq",
            "created_at_utc": CREATED_AT,
            "fingerprint": {"algorithm": "sha256", "value": BUNDLE_FINGERPRINT},
        })
        raw = base64.urlsafe_b64decode(signing["public_key"]["value"] + "=")
        self.assertEqual(hashlib.sha256(raw).hexdigest(), SIGNING_FINGERPRINT)
        self.assertEqual(hashlib.sha256(bundle["recipient"].encode("ascii")).hexdigest(),
                         BUNDLE_FINGERPRINT)

    def test_checked_in_header_is_exact_compiler_output_and_usable(self) -> None:
        with tempfile.TemporaryDirectory(prefix="wsprrypi-production-trust-") as temporary:
            root = Path(temporary)
            generated = root / "generated.hpp"
            MODULE.compile_trust([SIGNING], [BUNDLE], generated)
            self.assertEqual(generated.read_bytes(), HEADER.read_bytes())

            compiler = shutil.which("c++")
            if compiler is None:
                self.skipTest("C++ compiler is unavailable")
            source = root / "consumer.cpp"
            source.write_text(
                '#include "support_bundle_intake_compiled_trust.hpp"\n'
                '#include <array>\n#include <cstdint>\n#include <string>\n'
                'int main() {\n'
                '  const auto trust = support_bundle_intake_compiled_trust();\n'
                '  const std::array<std::uint8_t, 32> expected = {'
                '0xc1,0x12,0x6f,0x8f,0xbd,0x05,0xd5,0x84,0xd7,0xbf,0x49,0x45,'
                '0x6a,0x14,0xb9,0x39,0xea,0x12,0xe6,0xbc,0x33,0x46,0xe9,0xb9,'
                '0x59,0xe2,0x6f,0xfa,0x0d,0x69,0x70,0x3e};\n'
                '  return trust.signing_keys.size() == 1 && '
                'trust.signing_keys[0].key_id == "wsprrypi-intake-2026-01" && '
                'trust.signing_keys[0].public_key == expected && '
                'trust.recognized_bundle_key_ids == '
                'std::vector<std::string>{"wsprrypi-bundle-2026-01"} ? 0 : 1;\n}\n',
                encoding="ascii")
            binary = root / "consumer"
            completed = subprocess.run(
                [compiler, "-std=c++20", "-I", str(ROOT / "src"), str(source),
                 "-o", str(binary)], check=False, timeout=30,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertEqual(completed.returncode, 0, completed.stderr.decode())
            executed = subprocess.run([binary], check=False, timeout=10)
            self.assertEqual(executed.returncode, 0)

    def test_versioned_artifacts_are_public_only_and_runtime_remains_inactive(self) -> None:
        artifacts = [SIGNING, BUNDLE, HEADER]
        combined = b"\n".join(path.read_bytes() for path in artifacts)
        for forbidden in (b"PRIVATE KEY", b"AGE-SECRET-KEY-", b"request_url",
                          b"dropbox.com", b"github.com", b"/Users/", b"credential",
                          b"password"):
            self.assertNotIn(forbidden, combined)
        header = HEADER.read_text(encoding="ascii")
        for omitted in ("age1", "fingerprint", "created_at_utc", "recipient"):
            self.assertNotIn(omitted, header)

        production_sources = [path for path in (ROOT / "src").glob("*.cpp")
                              if not path.name.endswith("_test.cpp")]
        adapter = ROOT / "src/support_bundle_intake_production.cpp"
        adapter_text = adapter.read_text(encoding="utf-8")
        self.assertEqual(adapter_text.count("support_bundle_intake_compiled_trust()"), 1)
        self.assertEqual(adapter_text.count("resolve_support_bundle_intake_runtime("), 1)
        web_server = ROOT / "src/web_server.cpp"
        self.assertEqual(
            web_server.read_text(encoding="utf-8").count(
                "resolve_support_bundle_intake_production"), 1)
        for path in production_sources:
            text = path.read_text(encoding="utf-8")
            if path != adapter:
                self.assertNotIn("support_bundle_intake_compiled_trust", text, path)
            if path.name not in {"support_bundle_intake_runtime.cpp",
                                 "support_bundle_intake_production.cpp"}:
                self.assertNotIn("resolve_support_bundle_intake_runtime(", text, path)
            if path not in {adapter, web_server}:
                self.assertNotIn("resolve_support_bundle_intake_production", text, path)


if __name__ == "__main__":
    unittest.main()
