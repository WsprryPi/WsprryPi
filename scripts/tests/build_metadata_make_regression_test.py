#!/usr/bin/env python3
"""Integration coverage for production Make build-metadata rules."""

from __future__ import annotations

import os
import json
import shutil
import signal
import subprocess
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "scripts" / "generate_build_metadata.py"
RULES = ROOT / "scripts" / "build_metadata_rules.mk"
DEFAULT_TIMEOUT = 20
BUILD_TIMEOUT = 90
INTROSPECTION_CASES = (
    ("short dry-run", ("-n",), "dry", True),
    ("long dry-run", ("--dry-run",), "dry", True),
    ("just-print", ("--just-print",), "dry", True),
    ("recon", ("--recon",), "dry", True),
    ("short query", ("-q",), "query", False),
    ("long query", ("--question",), "query", False),
    ("grouped dry/query", ("-nq",), "query", False),
    ("grouped short options", ("-skn",), "dry", False),
    ("silent parallel output-sync", ("-s", "-n", "-j3", "--output-sync=target"), "dry", False),
)


def failure(command: tuple[str, ...], cwd: Path, timeout: int, stdout: str, stderr: str) -> AssertionError:
    return AssertionError(
        f"timed out after {timeout}s: {' '.join(command)} (cwd={cwd}); "
        f"stdout={stdout!r}, stderr={stderr!r}"
    )


def terminate(process: subprocess.Popen[str]) -> None:
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        process.communicate(timeout=2)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.communicate(timeout=2)


def run(*args: str, cwd: Path, expect: int = 0, timeout: int = DEFAULT_TIMEOUT) -> subprocess.CompletedProcess[str]:
    command = tuple(args)
    process = subprocess.Popen(
        command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, start_new_session=True,
    )
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired as error:
        terminate(process)
        raise failure(command, cwd, timeout, error.stdout or "", error.stderr or "") from error
    result = subprocess.CompletedProcess(command, process.returncode, stdout, stderr)
    if result.returncode != expect:
        raise AssertionError(
            f"{' '.join(command)} returned {result.returncode}, expected {expect}; "
            f"cwd={cwd}, stdout={stdout!r}, stderr={stderr!r}"
        )
    return result


def collect(processes: list[tuple[str, subprocess.Popen[str]]], cwd: Path, timeout: int = BUILD_TIMEOUT) -> None:
    deadline = time.monotonic() + timeout
    try:
        for name, process in processes:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise subprocess.TimeoutExpired(process.args, timeout)
            stdout, stderr = process.communicate(timeout=remaining)
            if process.returncode != 0:
                raise AssertionError(f"{name} failed: stdout={stdout!r}, stderr={stderr!r}")
    except (AssertionError, subprocess.TimeoutExpired) as error:
        for _, sibling in processes:
            if sibling.poll() is None:
                terminate(sibling)
        if isinstance(error, subprocess.TimeoutExpired):
            raise failure(tuple(str(item) for item in error.cmd), cwd, timeout, "", "") from error
        raise


def commit(repo: Path, message: str) -> str:
    run("git", "add", "marker.txt", cwd=repo)
    run("git", "commit", "-m", message, cwd=repo)
    return run("git", "rev-parse", "HEAD", cwd=repo).stdout.strip()


def snapshot(repo: Path, invocation_marker: Path, include_marker: bool = False) -> dict[str, tuple[int, int, int, bytes]]:
    paths = list((repo / "build").rglob("*")) if (repo / "build").exists() else []
    if include_marker:
        paths.append(invocation_marker)
    captured: dict[str, tuple[int, int, int, bytes]] = {}
    for path in paths:
        if not path.is_file():
            continue
        stat = path.stat()
        captured[str(path.relative_to(repo))] = (stat.st_ino, stat.st_mtime_ns, stat.st_size, path.read_bytes())
    return captured


def reset_marker(marker: Path) -> None:
    marker.unlink(missing_ok=True)


def invocation_records(marker: Path) -> list[dict[str, object]]:
    if not marker.exists():
        return []
    return [json.loads(line) for line in marker.read_text(encoding="utf-8").splitlines()]


def assert_version(repo: Path, binary: str, commit_id: str, dirty: bool) -> None:
    output = run(f"./build/bin/{binary}", cwd=repo).stdout.strip()
    assert output == f"{commit_id};{'true' if dirty else 'false'}", output


def make(repo: Path, *arguments: str, expect: int = 0, timeout: int = BUILD_TIMEOUT) -> subprocess.CompletedProcess[str]:
    return run("make", "--output-sync=target", "-j3", *arguments, cwd=repo, expect=expect, timeout=timeout)


def makefile(wrapper: Path) -> str:
    return (
        ".DEFAULT_GOAL := release\n"
        "BUILD_METADATA_HEADER := build/generated/build_metadata.hpp\n"
        f"BUILD_METADATA_GENERATOR := {wrapper}\n"
        "BUILD_METADATA_REPO_ROOT := .\n"
        "BUILD_METADATA_PROJECT := WsprryPi\n"
        "BUILD_METADATA_EXECUTABLE := version_fixture\n"
        "VERSION_RELEASE_OBJECT := build/obj/version.o\n"
        "VERSION_DEBUG_OBJECT := build/obj/version-debug.o\n"
        f"include {RULES}\n"
        "CXXFLAGS := -std=c++20 -Ibuild/generated\n"
        "build/obj/version.o: version.cpp\n"
        "\t@mkdir -p $(@D)\n\t@echo Compiling-release\n\t$(CXX) $(CXXFLAGS) -c $< -o $@\n"
        "build/obj/version-debug.o: version.cpp\n"
        "\t@mkdir -p $(@D)\n\t@echo Compiling-debug\n\t$(CXX) $(CXXFLAGS) -c $< -o $@\n"
        "build/bin/version_fixture: build/obj/version.o\n"
        "\t@mkdir -p $(@D)\n\t@echo Linking-release\n\t$(CXX) $< -o $@\n"
        "build/bin/version_debug_fixture: build/obj/version-debug.o\n"
        "\t@mkdir -p $(@D)\n\t@echo Linking-debug\n\t$(CXX) $< -o $@\n"
        ".PHONY: clean debug\n"
        "clean:\n\t@rm -rf build\n"
        "release: build/bin/version_fixture\n"
        "debug: build/bin/version_debug_fixture\n"
    )


def write_wrapper(repo: Path, marker: Path) -> Path:
    wrapper = repo / "generator_wrapper.py"
    wrapper.write_text(
        "#!/usr/bin/env python3\n"
        "import json\nimport os\nimport sys\nfrom pathlib import Path\n"
        f"marker = Path({str(marker)!r})\n"
        f"generator = {str(GENERATOR)!r}\n"
        "record = {'mode': 'check' if '--check' in sys.argv[1:] else 'generate', 'args': sys.argv[1:]}\n"
        "payload = (json.dumps(record, sort_keys=True) + '\\n').encode('utf-8')\n"
        "descriptor = os.open(marker, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)\n"
        "try:\n"
        "    os.write(descriptor, payload)\n"
        "    os.fsync(descriptor)\n"
        "finally:\n"
        "    os.close(descriptor)\n"
        "os.execv(sys.executable, [sys.executable, generator, *sys.argv[1:]])\n",
        encoding="utf-8",
    )
    wrapper.chmod(0o755)
    return wrapper


def assert_no_candidates(repo: Path) -> None:
    generated = repo / "build/generated"
    candidates = list(generated.glob(".build_metadata.hpp.*")) if generated.exists() else []
    assert candidates == [generated / ".build_metadata.hpp.lock"] or candidates == [], candidates


def run_introspection_matrix(repo: Path, marker: Path, state: str) -> None:
    for label, flags, kind, plans_work in INTROSPECTION_CASES:
        reset_marker(marker)
        before = snapshot(repo, marker)
        expected = 0 if kind == "dry" or state == "current" else 1
        result = make(repo, *flags, "release", expect=expected)
        detail = f"{label}/{state}: stdout={result.stdout!r}, stderr={result.stderr!r}"
        assert snapshot(repo, marker) == before, detail
        records = invocation_records(marker)
        assert len(records) == 1 and records[0]["mode"] == "check", (detail, records)
        assert "--check" in records[0]["args"], (detail, records)
        assert "build metadata updated" not in result.stdout, detail
        assert "Cleaning" not in result.stdout and "Installing" not in result.stdout, detail
        if state == "current":
            assert "echo Compiling-release" not in result.stdout, detail
            assert "echo Linking-release" not in result.stdout, detail
        if kind == "dry" and state != "current" and plans_work:
            assert "generator_wrapper.py" in result.stdout, detail
            assert "echo Compiling-release" in result.stdout, detail
            assert "echo Linking-release" in result.stdout, detail
        if kind == "query":
            assert "generator_wrapper.py" not in result.stdout, detail


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="wsprrypi-make-metadata-") as temporary:
        repo = Path(temporary) / "repo"
        repo.mkdir()
        marker = repo / "generator-invoked"
        wrapper = write_wrapper(repo, marker)
        run("git", "init", "-q", cwd=repo)
        run("git", "config", "user.email", "metadata-test@example.invalid", cwd=repo)
        run("git", "config", "user.name", "Build Metadata Test", cwd=repo)
        (repo / "marker.txt").write_text("A\n", encoding="utf-8")
        (repo / "version.cpp").write_text(
            '#include "build_metadata.hpp"\n#include <iostream>\n'
            'int main() { std::cout << MAKE_COMMIT << ";" << MAKE_DIRTY; }\n', encoding="utf-8",
        )
        (repo / "Makefile").write_text(makefile(wrapper), encoding="utf-8")
        commit_a = commit(repo, "A")

        first = make(repo, "release")
        first_records = invocation_records(marker)
        assert first_records and all(record["mode"] == "generate" for record in first_records), "normal release did not invoke the configured generator wrapper"
        assert "Compiling-release" in first.stdout and "Linking-release" in first.stdout
        assert_version(repo, "version_fixture", commit_a, False)
        first_snapshot = snapshot(repo, marker)
        same_a = make(repo, "release")
        assert "Compiling-release" not in same_a.stdout and "Linking-release" not in same_a.stdout
        assert snapshot(repo, marker) == first_snapshot

        run_introspection_matrix(repo, marker, "current")

        (repo / "marker.txt").write_text("B\n", encoding="utf-8")
        commit_b = commit(repo, "B")
        run_introspection_matrix(repo, marker, "stale")

        reset_marker(marker)
        moved = make(repo, "release", "debug")
        assert moved.stdout.count("build metadata updated") == 1
        moved_records = invocation_records(marker)
        assert moved_records and all(record["mode"] == "generate" for record in moved_records), "stale normal release did not invoke the wrapper"
        assert "Compiling-release" in moved.stdout and "Compiling-debug" in moved.stdout
        assert_version(repo, "version_fixture", commit_b, False)
        assert_version(repo, "version_debug_fixture", commit_b, False)
        assert_no_candidates(repo)

        second_snapshot = snapshot(repo, marker)
        same_b = make(repo, "release", "debug")
        assert "Compiling-" not in same_b.stdout and "Linking-" not in same_b.stdout
        assert snapshot(repo, marker) == second_snapshot

        (repo / "marker.txt").write_text("B dirty\n", encoding="utf-8")
        dirty = make(repo, "release", "debug")
        assert "Compiling-release" in dirty.stdout and "Compiling-debug" in dirty.stdout
        assert_version(repo, "version_fixture", commit_b, True)
        assert_version(repo, "version_debug_fixture", commit_b, True)
        run("git", "checkout", "--", "marker.txt", cwd=repo)
        clean = make(repo, "release", "debug")
        assert "Compiling-release" in clean.stdout and "Compiling-debug" in clean.stdout
        assert_version(repo, "version_fixture", commit_b, False)

        shutil.rmtree(repo / "build/generated")
        run_introspection_matrix(repo, marker, "missing")
        assert not (repo / "build/generated").exists()
        reset_marker(marker)
        make(repo, "release")
        missing_records = invocation_records(marker)
        assert missing_records and all(record["mode"] == "generate" for record in missing_records), "release after missing metadata did not invoke the wrapper"
        assert_version(repo, "version_fixture", commit_b, False)

        # The guard must reject before it can invoke the configured wrapper.
        shutil.rmtree(repo / "build/generated")
        reset_marker(marker)
        (repo / "build/sentinel").write_text("preserve\n", encoding="utf-8")
        guarded_snapshot = snapshot(repo, marker)
        for goals in (("clean", "release"), ("release", "clean")):
            rejected = make(repo, *goals, expect=2)
            detail = f"{goals}: stdout={rejected.stdout!r}, stderr={rejected.stderr!r}"
            assert "clean is a standalone goal" in rejected.stderr, detail
            assert snapshot(repo, marker) == guarded_snapshot, detail
            assert invocation_records(marker) == [], detail
            assert all(token not in rejected.stdout for token in ("Cleaning", "generator_wrapper", "Compiling", "Linking", "Release build")), detail
        make(repo, "clean")
        assert not (repo / "build").exists()
        reset_marker(marker)
        make(repo, "release")
        separate_records = invocation_records(marker)
        assert separate_records and all(record["mode"] == "generate" for record in separate_records), "separate release did not invoke the wrapper"
        assert_version(repo, "version_fixture", commit_b, False)

        (repo / "marker.txt").write_text("C\n", encoding="utf-8")
        commit_c = commit(repo, "C")
        commands = [
            ("release", subprocess.Popen(["make", "--output-sync=target", "-j3", "release"], cwd=repo, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)),
            ("debug", subprocess.Popen(["make", "--output-sync=target", "-j3", "debug"], cwd=repo, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)),
        ]
        collect(commands, repo)
        assert_version(repo, "version_fixture", commit_c, False)
        assert_version(repo, "version_debug_fixture", commit_c, False)
        assert_no_candidates(repo)

        valid_snapshot = snapshot(repo, marker)
        failure_commands = [
            ("failure", subprocess.Popen(["make", "--output-sync=target", "-j3", "BUILD_METADATA_GENERATOR=/definitely/missing-generator", "release"], cwd=repo, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)),
            ("debug", subprocess.Popen(["make", "--output-sync=target", "-j3", "debug"], cwd=repo, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)),
        ]
        try:
            collect(failure_commands, repo)
        except AssertionError as error:
            assert "missing-generator" in str(error)
        else:
            raise AssertionError("missing generator unexpectedly succeeded")
        assert snapshot(repo, marker) == valid_snapshot
        assert_version(repo, "version_fixture", commit_c, False)

        tagged = make(repo, "release", "BUILD_METADATA_VERSION=4.0.0-beta.1")
        assert "Compiling-release" in tagged.stdout
        candidate_header = (repo / "build/generated/build_metadata.hpp").read_text()
        assert '#define MAKE_TAG "4.0.0-beta.1+' in candidate_header
        candidate_snapshot = snapshot(repo, marker)
        make(repo, "-n", "release", "BUILD_METADATA_VERSION=4.0.0-beta.1")
        assert snapshot(repo, marker) == candidate_snapshot
        same_candidate = make(repo, "release", "BUILD_METADATA_VERSION=4.0.0-beta.1")
        assert "Compiling-release" not in same_candidate.stdout
        restored = make(repo, "release")
        assert "Compiling-release" in restored.stdout
        assert_version(repo, "version_fixture", commit_c, False)

    print("build metadata Make regression test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
