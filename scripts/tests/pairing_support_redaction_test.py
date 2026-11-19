#!/usr/bin/env python3
"""Exercise the collector's actual redactor without collecting host data."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "scripts/collect-support-bundle.sh").read_text()
REDACTOR = SOURCE[SOURCE.index("redact_file_in_place() {"):SOURCE.index("\nredact_tree() {")]


class PairingRedactionTest(unittest.TestCase):
    def test_private_pairing_fields_are_removed_and_public_evidence_remains(self):
        secrets = {name: "artificial-private-" + str(index) for index, name in enumerate(
            ("tls_psk", "retirement_verifier", "private_key", "pairing_code", "commit_key"))}
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "fixture.json"
            path.write_text(json.dumps({**secrets, "operation_id": "a" * 32,
                                        "state": "unknown", "error": {"code": "operation_unknown"}}))
            subprocess.run(["bash", "-c", REDACTOR + '\nredact_file_in_place "$1"', "redaction-fixture", str(path)], check=True)
            document = json.loads(path.read_text())
            self.assertTrue(all(document[name] == "[REDACTED]" for name in secrets))
            self.assertEqual(document["state"], "unknown")
            self.assertEqual(document["operation_id"], "a" * 32)
            self.assertEqual(document["error"]["code"], "operation_unknown")
            self.assertFalse(any(value in path.read_text() for value in secrets.values()))

    def test_plain_log_fields_are_redacted_without_losing_the_observation(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "fixture.log"
            path.write_text("tls_psk=artificial-tls retirement_verifier: artificial-verifier private_key=artificial-key pairing_code: 12345678 state=unknown\n")
            subprocess.run(["bash", "-c", REDACTOR + '\nredact_file_in_place "$1"', "redaction-fixture", str(path)], check=True)
            text = path.read_text()
            self.assertNotIn("artificial-", text)
            self.assertNotIn("12345678", text)
            self.assertIn("state=unknown", text)


if __name__ == "__main__":
    unittest.main()
