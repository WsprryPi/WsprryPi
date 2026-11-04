#!/usr/bin/env python3
"""Read-only inventory of an immutable Git revision for the CI review.

This measures file envelopes and lexical signals, not executed coverage.
Only --output writes a file; measurement never runs Make or test targets.
"""
from __future__ import annotations
import argparse
import ast
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
CODE = {".py", ".sh", ".js", ".cjs", ".mjs", ".php", ".cpp", ".cc", ".c", ".hpp", ".h", ".cmake", ".mk", ".yml", ".yaml"}
INACTIVE = {
    "scripts/tests/apache_privileged_network_policy_renderer_test.py": "Unscheduled renderer behavior; integrate before retiring overlap.",
    "scripts/tests/apache_proxy_config_test.sh": "Unscheduled Apache source-policy checks.",
    "scripts/tests/collect-support-bundle_test.sh": "Unscheduled collector behavior; unique evidence must be mapped.",
    "scripts/tests/php2shell_mitigate_test.py": "Standalone maintainer security tool, not ordinary CI.",
    "scripts/tests/pico_usb_bundle_test.py": "Only reached through unscheduled pico-usb-installer-test.",
    "scripts/tests/pico_usb_deploy_test.py": "Only reached through unscheduled pico-usb-installer-test.",
    "scripts/tests/pico_usb_bundle_fixture.py": "Imported by the two unscheduled Pico installer tests.",
    "scripts/tests/support_bundle_runtime_install_test.sh": "Unscheduled support-bundle-runtime-installer-test.",
    "WsprryPi-UI/tests/log_stream_disconnect_integration_test.sh": "Installed Apache qualification; cleanup can restart Apache.",
    "WsprryPi-UI/tests/log_stream_resume_integration_test.py": "Installed journal qualification; requires retained records.",
    "WsprryPi-UI/tests/native_host_fixture.js": "Opt-in WSPRRYPI_NATIVE_HOST_FIXTURE router consumer, not ordinary CI.",
    "WsprryPi-UI/tests/si5351_ui_integration_test.js": "Manual/unscheduled browser entry; map before consolidation.",
    "WsprryPi-UI/tests/wtp_firmware_ui_integration_test.js": "Manual/unscheduled browser entry; map before consolidation.",
}
EXTRA = {"src/Makefile", "WsprryPi-UI/package.json", "src/tests/ui_source_regression_test.cpp", "src/WSPR-Reference/tests/run_major_regressions.sh"}
WAIT = re.compile(r"\b(?:wait|waitpid|select|readline|join|acquire|sleep|sleep_for|sleep_until|nanosleep|setTimeout|setInterval|waitForTimeout|wait_for|wait_until|condition_variable|deadline|timeout|wait_ready|waitFor|until)\b", re.I)
BROWSER = {"conditional_transmit_gpio_integration_test.js", "cw_duration_latch_integration_test.js", "dependency_browser_test.js", "integrated_review_browser.js", "linux_i2c_identity_browser_test.js", "page_warning_browser.js", "rp1_pps_browser_test.js", "support_intake_ui_integration_test.js", "test_tone_lifecycle_integration_test.js", "udp_logging_logs_browser_test.js", "udp_logging_ui_integration_test.js", "usb_logging_browser_test.js", "wtp_network_ui_integration_test.js", "wtp_pi_fleet_ui_integration_test.js", "wtp_recovery_ui_integration_test.js", "wtp_usb_ui_integration_test.js"}


def git(*args: str) -> bytes:
    return subprocess.check_output(["git", "-C", str(ROOT), *args])


def family(path: str) -> str:
    name = Path(path).name
    if path.startswith(".github/") or path.startswith("scripts/ci/"): return "admission-orchestration"
    if path.startswith("WsprryPi-UI/tests/"):
        if name in BROWSER: return "browser"
        if "pair_cases" in name or "interop_test" in name or name in {"wtp_usb_pair_test.js", "control_pair_request.js"}: return "carrier-pair"
        return "ui-unit-fixture"
    if path.startswith("scripts/tests/"):
        if name.startswith(("rp1_", "runtime_reconcile", "route_application")): return "rp1-installer"
        if name.startswith(("support_bundle", "collect-support")): return "support-bundle"
        if name.startswith(("backend_", "build_metadata", "object_dependency", "semantics_make", "si5351_transport", "wtp_server_make")): return "build-policy"
        if name.startswith(("installer_", "precompiled_", "picotool_", "pico_usb_", "ini_upgrade", "service_install", "build_resource")): return "installer"
        return "script-boundaries"
    if path == "src/tests/ui_source_regression_test.cpp": return "source-policy"
    if "/tests/network/" in path: return "network-production-fixture"
    if path.startswith("src/tests/"): return "parent-behavior"
    if path.startswith("docs/development/wtp-reply-routing-results/"): return "routing-rollback"
    return "component-build-test"


def in_scope(path: str) -> bool:
    if path.startswith((".github/workflows/", "scripts/ci/", "scripts/tests/", "WsprryPi-UI/tests/", "src/tests/", "docs/development/wtp-reply-routing-results/")): return True
    if path in EXTRA or path.endswith(".mk"): return True
    if path.startswith("src/") and "/vendor/" not in path and "/include/nlohmann/" not in path:
        return "/tests/" in path or Path(path).name in {"Makefile", "CMakeLists.txt", "test.cpp", "scheduling_rp1_test_support.hpp", "qualification_gpio_test_mode.hpp", "si5351_backend_harness.cpp", "rp1_gpclk_admin_probe.cpp"} or (path.startswith("src/WSPR-Transmitter/src/") and "qualification" in Path(path).name) or "/test/" in path or "/test_vectors/" in path or bool(re.search(r"(?:_test|_fixture)(?:\.[^.]+)$", Path(path).name))
    return False


def cohort(path: str) -> str:
    if path in INACTIVE: return "inactive-uncredited"
    if path == "scripts/ci/review_inventory.py": return "phase1-overhead"
    if path in EXTRA: return "eligible-mixed-envelope"
    if path.startswith((".github/workflows/", "scripts/ci/", "scripts/tests/", "WsprryPi-UI/tests/")) and Path(path).suffix in CODE: return "eligible-mixed-envelope"
    return "protected-context"


def disposition(path: str) -> tuple[str, int]:
    if path in INACTIVE or path in {"src/Singleton/src/test.cpp", "src/PPM-Manager/src/test.cpp", "src/Signal-Handler/src/test.cpp"}: return "HOLD", 5
    if path in {"scripts/ci/assert-no-transmitter-hardware.sh", "scripts/ci/verify-simulated-wspr.sh"}: return "RETAIN", 7
    if path in {"src/tests/ui_source_regression_test.cpp", "scripts/tests/support_bundle_contract_reconciliation_test.py", "scripts/tests/rp1_gpclk_diagnostics_test.py", "scripts/tests/support_bundle_production_encryption_metadata_test.py"}: return "DELETE_AFTER_REPLACEMENT", 3
    f = family(path)
    if f == "browser": return "REWRITE", 2
    if f in {"installer", "rp1-installer", "support-bundle"} and path.startswith("scripts/tests/"): return "REWRITE", 4
    if path == "src/Makefile" or f == "admission-orchestration" or path == "WsprryPi-UI/package.json": return "REWRITE", 5
    if f == "build-policy" or Path(path).name in {"Makefile", "CMakeLists.txt"} or path.endswith(".mk"): return "REWRITE", 6
    return "RETAIN", 7


def snapshot(revision: str) -> dict:
    revision = git("rev-parse", "--verify", "--end-of-options", revision + "^{commit}").decode().strip()
    entries = []
    for record in git("ls-tree", "-r", "-z", revision).split(b"\0"):
        if not record: continue
        header, raw_path = record.split(b"\t", 1)
        mode, kind, blob = header.decode().split()
        path = raw_path.decode()
        if kind != "blob" or not in_scope(path): continue
        data = git("cat-file", "blob", blob)
        code = Path(path).suffix in CODE or Path(path).name in {"Makefile", "CMakeLists.txt", "package.json"}
        try: text = data.decode("utf-8")
        except UnicodeError:
            if code: raise SystemExit(f"Unmeasurable non-UTF-8 code blob: {path} ({blob})")
            text = ""
        group = cohort(path)
        if not code and path not in EXTRA: group = "protected-data"
        action, phase = disposition(path)
        methods = 0
        if path.endswith(".py"):
            methods = sum(isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef)) and n.name.startswith("test_") for n in ast.walk(ast.parse(text)))
        lines = text.splitlines()
        waits = [i for i, line in enumerate(lines, 1) if WAIT.search(line)]
        entries.append({"path": path, "mode": mode, "blob": blob, "bytes": len(data), "lines": len(lines), "cohort": group, "family": family(path), "disposition": action, "phase": phase, "python_test_methods": methods, "js_assert_calls_lexical": len(re.findall(r"\bassert(?:\.\w+)?\s*\(", text)) if path.endswith((".js", ".cjs", ".mjs")) else 0, "wait_signal_lines": waits})
    totals = {}
    for key in sorted({e["cohort"] for e in entries}):
        selected = [e for e in entries if e["cohort"] == key]
        totals[key] = {"files": len(selected), "lines": sum(e["lines"] for e in selected), "python_test_methods": sum(e["python_test_methods"] for e in selected), "js_assert_calls_lexical": sum(e["js_assert_calls_lexical"] for e in selected), "wait_signal_lines": sum(len(e["wait_signal_lines"]) for e in selected)}
    return {"schema": 1, "source_revision": revision, "workflow_sha256": hashlib.sha256(git("show", revision + ":.github/workflows/debian-non-hardware.yml")).hexdigest(), "method": "immutable Git blobs; UTF-8 splitlines; lexical signals are not coverage; full files are mixed envelopes", "totals": totals, "inactive_reasons": INACTIVE, "files": entries}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", default="HEAD")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check", type=Path, help="Recompute a frozen ledger at its recorded revision; never check current HEAD against old counts")
    args = parser.parse_args()
    if args.check and args.output: parser.error("--check and --output are mutually exclusive")
    if args.check:
        expected = json.loads(args.check.read_text())
        actual = snapshot(expected["source_revision"])
        if actual != expected: raise SystemExit("Frozen CI inventory mismatch")
        print("Frozen CI inventory verified against immutable source blobs")
    else:
        result = snapshot(args.revision)
        if args.output:
            # One record per line keeps the data inspectable without inflating code metrics.
            head = {k: v for k, v in result.items() if k != "files"}
            body = json.dumps(head, sort_keys=True)[:-1] + ', "files": [\n'
            body += ',\n'.join(json.dumps(e, sort_keys=True) for e in result["files"]) + '\n]}\n'
            args.output.write_text(body)
        print(json.dumps({k: v for k, v in result.items() if k != "files"}, sort_keys=True, indent=2))


if __name__ == "__main__":
    main()
