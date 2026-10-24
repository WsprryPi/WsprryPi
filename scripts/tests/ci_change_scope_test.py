#!/usr/bin/env python3
"""Exercise CI admission against real Git histories and unsafe file changes."""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("change_scope", ROOT / "scripts/ci/change_scope.py")
scope = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(scope)


class PolicyTests(unittest.TestCase):
    def test_only_known_documentation_locations_skip(self):
        for path in ["README.md", "docs/reference.md", "docs/nested/guide.rst",
                     "release_tools/4.0-Beta-1-Release-Notes.md",
                     "docs/image.png", "docs/nested/figure.pdf"]:
            with self.subTest(path=path):
                self.assertTrue(scope.documentation_path(path))

    def test_checked_markdown_and_unknown_paths_require_ci(self):
        for path in ["AGENTS.md", "docs/AGENTS.md", "docs/support-bundle-private-intake.md",
                     "docs/licenses/notice.md", "docs/development/wtp-reply-routing-results/fixture.md",
                     "release_tools/rp1-qualification/README.md", "LICENSE.md",
                     "release_tools/NOTICE.md", "docs/COPYING.rst", "docs/PROVENANCE.md",
                     "src/tests/fixture.md", "WsprryPi-UI/tests/fixture.rst", "scripts/README.md",
                     "unknown/readme.md", "images/icon.png", "WsprryPi-UI/images/icon.svg",
                     "docs/helper.py", "docs/data.csv", "docs/manifest.json",
                     "release_tools/rp1-qualification-lock.json", "config/wsprrypi.ini",
                     ".github/workflows/debian-non-hardware.yml", "docs/../src/fixture.md",
                     "/docs/guide.md", "./docs/guide.md"]:
            with self.subTest(path=path):
                self.assertFalse(scope.documentation_path(path))

    def test_nonregular_documentation_and_type_changes_require_ci(self):
        for old, new, status in [("100644", "100755", "M"), ("000000", "100755", "A"),
                                 ("000000", "120000", "A"), ("120000", "000000", "D"),
                                 ("100644", "120000", "T"), ("160000", "160000", "M")]:
            with self.subTest(old=old, new=new, status=status):
                self.assertFalse(scope.documentation_change(scope.Change(old, new, status, "docs/input.md")))

    def test_raw_diff_preserves_whitespace_in_filenames(self):
        path = "docs/a space\ta tab\nand newline.md"
        raw = f":100644 100644 {'1'*40} {'2'*40} M\0{path}\0".encode()
        self.assertEqual(scope.parse_changes(raw), [scope.Change("100644", "100644", "M", path)])

    def test_malformed_diff_never_proves_a_documentation_change(self):
        samples = [b"no terminator", b"header\0", b"header\0docs/a.md\0", b"\0\0",
                   f":100644 100644 {'1'*40} {'2'*40} R100\0docs/a.md\0".encode(),
                   f":100644 100644 {'1'*40} {'2'*40} M\0\0".encode(),
                   f":100644 100644 {'1'*40} {'2'*40} M\0".encode() + b"docs/\xff.md\0"]
        for raw in samples:
            with self.subTest(raw=raw), self.assertRaises((ValueError, UnicodeError)):
                scope.parse_changes(raw)

    def test_comparison_errors_default_to_full_ci(self):
        event = {"before": "1"*40, "after": "2"*40}
        for failure in [OSError("missing Git"), subprocess.CalledProcessError(128, "git"),
                        subprocess.TimeoutExpired("git", 60)]:
            with self.subTest(failure=failure), patch.object(scope.subprocess, "check_output", side_effect=failure):
                self.assertTrue(scope.full_ci_required(ROOT, "push", event)[0])
        for raw in [b"", b"malformed"]:
            with self.subTest(raw=raw), patch.object(scope.subprocess, "check_output", return_value=raw):
                self.assertTrue(scope.full_ci_required(ROOT, "push", event)[0])

    def test_workflow_cannot_bypass_scope_failure_or_skip_by_event_paths(self):
        text = (ROOT / ".github/workflows/debian-non-hardware.yml").read_text()
        self.assertNotIn("paths-ignore:", text)
        self.assertNotIn("\n    paths:", text)
        sections = re.split(r"(?m)^  ([a-z][a-z0-9_-]*):\n", text.split("jobs:\n", 1)[1])
        jobs = dict(zip(sections[1::2], sections[2::2]))
        gate = jobs.pop("change_scope")
        self.assertIn("fetch-depth: 0", gate)
        self.assertIn("persist-credentials: false", gate)
        self.assertIn("make -C src ci-change-scope-test", gate)
        self.assertIn("run: python3 -B scripts/ci/change_scope.py", gate)
        self.assertIn("run_full_ci: ${{ steps.classify.outputs.run_full_ci }}", gate)
        self.assertTrue(jobs)
        guard = ("if: ${{ !cancelled() && (needs.change_scope.result != 'success' || "
                 "needs.change_scope.outputs.run_full_ci != 'false') }}")
        for name, body in jobs.items():
            with self.subTest(job=name):
                self.assertIn("    needs: change_scope\n", body)
                self.assertIn(guard, body)

    def test_documentation_successor_cannot_cancel_an_older_source_run(self):
        text = (ROOT / ".github/workflows/debian-non-hardware.yml").read_text()
        group = next(line for line in text.splitlines() if line.startswith("  group:"))
        self.assertIn("${{ github.sha }}", group)
        self.assertNotEqual(group.replace("${{ github.sha }}", "1"*40),
                            group.replace("${{ github.sha }}", "2"*40))


class GitHistoryTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="ci-scope-")
        self.addCleanup(temporary.cleanup)
        self.repo = Path(temporary.name)
        self.env = {**os.environ, "GIT_CONFIG_NOSYSTEM": "1", "GIT_CONFIG_GLOBAL": os.devnull}
        self.git("init", "-q", "-b", "fixture")
        self.git("config", "user.name", "CI Scope Test")
        self.git("config", "user.email", "ci-scope@example.invalid")
        self.git("config", "core.hooksPath", os.devnull)
        self.git("config", "core.fileMode", "true")
        self.write("README.md", "baseline\n")
        self.base = self.commit()

    def git(self, *arguments):
        return subprocess.check_output(["git", "-C", str(self.repo), *arguments],
                                       env=self.env, stderr=subprocess.PIPE, text=True).strip()

    def write(self, path, text="ordinary documentation\n"):
        target = self.repo / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text)

    def commit(self):
        self.git("add", "--all")
        self.git("commit", "-qm", "fixture change")
        return self.git("rev-parse", "HEAD")

    def push_required(self, head, **extra):
        with patch.dict(os.environ, self.env):
            return scope.full_ci_required(self.repo, "push", {"before": self.base, "after": head, **extra})[0]

    def test_regular_documentation_add_edit_delete(self):
        self.write("docs/reference.md")
        self.assertFalse(self.push_required(self.commit()))
        self.base = self.git("rev-parse", "HEAD")
        self.write("docs/reference.md", "updated\n")
        self.assertFalse(self.push_required(self.commit()))
        self.base = self.git("rev-parse", "HEAD")
        (self.repo / "docs/reference.md").unlink()
        self.assertFalse(self.push_required(self.commit()))

    def test_real_checked_markdown_changes_run(self):
        for path in ["AGENTS.md", "docs/support-bundle-private-intake.md", "src/tests/input.md"]:
            self.write(path)
            self.assertTrue(self.push_required(self.commit()), path)

    def test_mixed_documentation_and_source_runs(self):
        self.write("docs/reference.md")
        self.write("src/main.cpp", "int main() {}\n")
        self.assertTrue(self.push_required(self.commit()))

    def test_deleted_source_still_runs_with_documentation_changes(self):
        self.write("src/input.md")
        self.base = self.commit()
        (self.repo / "src/input.md").unlink()
        self.write("docs/reference.md")
        self.assertTrue(self.push_required(self.commit()))

    def test_rename_across_documentation_boundary_runs(self):
        (self.repo / "src").mkdir()
        self.git("mv", "README.md", "src/input.md")
        self.assertTrue(self.push_required(self.commit()))
        self.base = self.git("rev-parse", "HEAD")
        self.git("mv", "src/input.md", "README.md")
        self.assertTrue(self.push_required(self.commit()))

    def test_ordinary_documentation_rename_skips(self):
        self.write("docs/old.md")
        self.base = self.commit()
        self.git("mv", "docs/old.md", "docs/new.md")
        self.assertFalse(self.push_required(self.commit()))

    def test_executable_bit_change_runs(self):
        self.write("docs/reference.md")
        self.base = self.commit()
        (self.repo / "docs/reference.md").chmod(0o755)
        self.assertTrue(self.push_required(self.commit()))

    def test_symlink_and_type_change_run(self):
        self.write("docs/reference.md")
        self.base = self.commit()
        path = self.repo / "docs/reference.md"
        path.unlink()
        path.symlink_to("../README.md")
        self.assertTrue(self.push_required(self.commit()))
        self.base = self.git("rev-parse", "HEAD")
        path.unlink()
        self.write("docs/reference.md")
        self.assertTrue(self.push_required(self.commit()))

    def test_unusual_filenames_are_not_split(self):
        self.write("docs/space tab\tnewline\n.md")
        self.assertFalse(self.push_required(self.commit()))
        self.write("src/space tab\tnewline\n.md")
        self.assertTrue(self.push_required(self.commit()))

    def test_large_diff_has_no_hosted_path_limit(self):
        for index in range(3101):
            self.write(f"docs/guide-{index:04}.md")
        self.assertFalse(self.push_required(self.commit()))
        self.write("src/zz-late-code.cpp", "int changed;\n")
        self.assertTrue(self.push_required(self.commit()))

    def test_force_push_and_new_branch_run(self):
        self.write("docs/reference.md")
        head = self.commit()
        self.assertTrue(self.push_required(head, forced=True))
        self.assertTrue(scope.full_ci_required(self.repo, "push", {"before": "0"*40, "after": head})[0])

    def test_missing_history_and_empty_comparison_run(self):
        self.assertTrue(scope.full_ci_required(self.repo, "push", {"before": "1"*40, "after": self.base})[0])
        self.assertTrue(self.push_required(self.base))
        event = {"pull_request": {"base": {"sha": "1"*40}, "head": {"sha": self.base}}}
        self.assertTrue(scope.full_ci_required(self.repo, "pull_request", event)[0])

    def test_pr_uses_merge_base_not_a_two_dot_diff(self):
        self.git("checkout", "-qb", "base")
        self.write("src/base-only.cpp", "int base_only;\n")
        base_tip = self.commit()
        self.git("checkout", "-qb", "topic", self.base)
        self.write("docs/pr-only.md")
        head = self.commit()
        event = {"pull_request": {"base": {"sha": base_tip}, "head": {"sha": head}}}
        self.assertFalse(scope.full_ci_required(self.repo, "pull_request", event)[0])
        self.write("src/pr-only.cpp", "int pr_only;\n")
        event["pull_request"]["head"]["sha"] = self.commit()
        self.assertTrue(scope.full_ci_required(self.repo, "pull_request", event)[0])

    def test_unsupported_or_malformed_events_run(self):
        for event_name, event in [("schedule", {}), ("push", {}), ("push", []),
                                  ("pull_request", {"pull_request": None}),
                                  ("push", {"before": "--help", "after": self.base})]:
            with self.subTest(event_name=event_name, event=event):
                self.assertTrue(scope.full_ci_required(self.repo, event_name, event)[0])

    def test_cli_outputs_only_a_boolean_and_handles_bad_event_json(self):
        self.write("docs/reference.md")
        head = self.commit()
        event_path, output = self.repo / "event.json", self.repo / "output"
        event_path.write_text(json.dumps({"before": self.base, "after": head}))
        environment = {**self.env, "GITHUB_EVENT_NAME": "push", "GITHUB_EVENT_PATH": str(event_path),
                       "GITHUB_OUTPUT": str(output)}
        with patch.object(scope, "ROOT", self.repo), patch.dict(os.environ, environment):
            self.assertEqual(scope.main(), 0)
        self.assertEqual(output.read_text(), "run_full_ci=false\n")
        event_path.write_text("not JSON")
        with patch.object(scope, "ROOT", self.repo), patch.dict(os.environ, environment):
            self.assertEqual(scope.main(), 0)
        self.assertEqual(output.read_text().splitlines()[-1], "run_full_ci=true")


if __name__ == "__main__":
    unittest.main()
