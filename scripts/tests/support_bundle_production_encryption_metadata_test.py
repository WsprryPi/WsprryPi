#!/usr/bin/env python3
import json
import pathlib
import re

root = pathlib.Path(__file__).resolve().parents[2]
metadata = json.loads((root / "config/support-bundle-intake/wsprrypi-bundle-2026-01.public.json").read_text())
header = (root / "src/support_bundle_encryption_production.hpp").read_text()
values = re.findall(r'"([^"]+)"', header)
assert metadata["key_id"] in values
assert metadata["recipient"] in values
assert "AGE-SECRET-KEY" not in header
print("support_bundle_production_encryption_metadata_test passed")
