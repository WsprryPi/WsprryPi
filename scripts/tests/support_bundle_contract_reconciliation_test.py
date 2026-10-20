#!/usr/bin/env python3

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
CONTRACT = (ROOT / "docs/support-bundle-private-intake.md").read_text()
COLLECTOR = (ROOT / "scripts/collect-support-bundle.sh").read_text()
PRIVATE = (ROOT / "src/support_bundle_private_artifact.cpp").read_text()
VALIDATOR = (ROOT / "src/support_bundle_intake_validation.cpp").read_text()


def require(value: str, source: str, message: str) -> None:
    if value not in source:
        raise AssertionError(message)


def documented_fields(marker: str) -> list[str]:
    match = re.search(re.escape(marker) + r".*?```text\n(.*?)\n```", CONTRACT, re.DOTALL)
    if match is None:
        raise AssertionError(f"missing documented field block after {marker}")
    return [line.split(",", 1)[0].split(" =", 1)[0].strip()
            for line in match.group(1).splitlines()]


require("Support Bundle Intake Maintainer Runbook", CONTRACT,
        "contract must link the durable maintainer runbook")
require("exactly these top-level fields", CONTRACT,
        "readable v1 manifest must retain its exact top-level contract")

readable_fields = ["schema_version", "contract_version", "project_id", "project_version",
                   "case_id", "created_at_utc", "collection_options", "privacy_categories",
                   "support_context", "collection_warnings", "files"]
if documented_fields("exactly these top-level fields:") != readable_fields:
    raise AssertionError("readable manifest documentation must list the exact v1 fields")
for field in readable_fields:
    require(f'"{field}"', COLLECTOR, f"collector manifest is missing {field}")
    require(field, CONTRACT, f"contract manifest list is missing {field}")
if '"project_version": "unknown"' in COLLECTOR:
    raise AssertionError("private readable manifests must not publish an unknown project version")

receipt_fields = ["schema_version", "project_id", "case_id", "artifact_id", "created_at_utc",
                  "archive_filename", "archive_size", "archive_sha256", "encrypted_filename",
                  "encrypted_size", "encrypted_sha256", "bundle_encryption_key_id", "issue_url",
                  "upload_state"]
if documented_fields("receipt containing exactly the v1 fields:") != receipt_fields:
    raise AssertionError("receipt documentation must list the exact v1 fields")
for field in receipt_fields:
    require(f'"{field}"', PRIVATE, f"receipt implementation is missing {field}")
    require(field, CONTRACT, f"contract receipt list is missing {field}")

intake_fields = ["schema_version", "project_id", "generation", "published_at", "expires_at",
                 "minimum_upload_version", "minimum_client_protocol", "request_url", "release_url",
                 "status", "user_message", "bundle_encryption_key_id"]
if documented_fields("The version-1 manifest SHALL contain exactly:") != intake_fields:
    raise AssertionError("signed intake documentation must list the exact v1 fields")
for field in intake_fields:
    require(f'"{field}"', VALIDATOR, f"intake validator is missing {field}")
    require(field, CONTRACT, f"contract intake list is missing {field}")

require("wsprrypi-support-<case-id>-<artifact-id>.tar.gz.age", CONTRACT,
        "encrypted filename must match production")
require("The signing key ID is carried by the detached signature envelope", CONTRACT,
        "signing key placement must remain explicit")
require("The version-1 manifest SHALL contain exactly", CONTRACT,
        "signed intake v1 must retain its exact-field contract")
require("The version-1 downloaded receipt is immutable", CONTRACT,
        "receipt immutability must remain explicit")
require("into the canonical processing record", CONTRACT,
        "Dropbox filename privacy boundary must remain explicit")
require("The maintainer has not yet confirmed receipt.", CONTRACT,
        "user-reported completion must not claim maintainer confirmation")
require("Operator instructions live in the separate `Wsprry_Pi_Docs` repository", CONTRACT,
        "operator documentation ownership must remain explicit")

print("support_bundle_contract_reconciliation_test: PASS")
