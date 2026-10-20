#!/usr/bin/env python3
"""Standalone regression coverage for generated Git build metadata."""

from __future__ import annotations

import importlib.util
import subprocess
import sys
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "scripts" / "generate_build_metadata.py"
MODULE_SPEC = importlib.util.spec_from_file_location("build_metadata", GENERATOR)
assert MODULE_SPEC is not None and MODULE_SPEC.loader is not None
BUILD_METADATA = importlib.util.module_from_spec(MODULE_SPEC)
MODULE_SPEC.loader.exec_module(BUILD_METADATA)


def run(*args: str, cwd: Path | None = None, expect: int = 0) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(args, cwd=cwd, check=False, capture_output=True, text=True)
    if result.returncode != expect:
        raise AssertionError(
            f"{' '.join(args)} returned {result.returncode}, expected {expect}; "
            f"stdout={result.stdout!r}, stderr={result.stderr!r}"
        )
    return result


def generate(repo: Path, output: Path, expect: int = 0) -> subprocess.CompletedProcess[str]:
    return run(
        sys.executable, str(GENERATOR), "--repo-root", str(repo), "--output", str(output),
        "--project", "WsprryPi", "--executable", "wsprrypi", expect=expect,
    )


def generated_content(repo: Path) -> str:
    return BUILD_METADATA.metadata(repo, "WsprryPi", "wsprrypi")


def assert_complete_header(content: str) -> None:
    lines = content.splitlines()
    assert lines[:2] == ["#pragma once", ""]
    assert [line.split(" ", 2)[1] for line in lines[2:]] == [
        "MAKE_TAG", "MAKE_BRH", "MAKE_RAW_BRH", "MAKE_BRANCH_STATE",
        "MAKE_COMMIT", "MAKE_DIRTY", "MAKE_EXE", "MAKE_PRJ",
    ]
    assert all(line.startswith("#define ") and line.endswith('"') for line in lines[2:])


def assert_no_temporary_candidates(output: Path) -> None:
    lock_path = output.with_name(f".{output.name}.lock")
    candidates = [
        path for path in output.parent.glob(f".{output.name}.*")
        if path != lock_path
    ]
    assert not candidates, candidates


def compile_and_round_trip(value: str, directory: Path) -> None:
    source = directory / "escape_round_trip.cpp"
    binary = directory / "escape_round_trip"
    source.write_text(
        "#include <iostream>\n"
        f'#define VALUE "{BUILD_METADATA.c_string(value)}"\n'
        "int main() { std::cout << VALUE; }\n",
        encoding="utf-8",
    )
    run("g++", "-std=c++20", str(source), "-o", str(binary))
    result = subprocess.run([str(binary)], check=False, capture_output=True)
    assert result.returncode == 0
    assert result.stdout == value.encode("utf-8")


def concurrent_generate(repo: Path, output: Path) -> None:
    command = [
        sys.executable, str(GENERATOR), "--repo-root", str(repo), "--output", str(output),
        "--project", "WsprryPi", "--executable", "wsprrypi",
    ]
    processes = [subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE) for _ in range(6)]
    for process in processes:
        stdout, stderr = process.communicate(timeout=10)
        assert process.returncode == 0, (stdout, stderr)
    content = output.read_text(encoding="utf-8")
    assert content == generated_content(repo)
    assert_complete_header(content)
    assert_no_temporary_candidates(output)


def commit(repo: Path, message: str) -> str:
    run("git", "add", "tracked.txt", cwd=repo)
    run("git", "commit", "-m", message, cwd=repo)
    return run("git", "rev-parse", "HEAD", cwd=repo).stdout.strip()


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="wsprrypi-build-metadata-") as temporary:
        repo = Path(temporary) / "repo"
        repo.mkdir()
        run("git", "init", "-q", cwd=repo)
        run("git", "config", "user.email", "metadata-test@example.invalid", cwd=repo)
        run("git", "config", "user.name", "Build Metadata Test", cwd=repo)
        (repo / "tracked.txt").write_text("A\n", encoding="utf-8")
        commit_a = commit(repo, "A")
        run("git", "tag", "v3.2.0", cwd=repo)
        run("git", "checkout", "-q", "-b", "devel", cwd=repo)
        (repo / "tracked.txt").write_text("A2\n", encoding="utf-8")
        commit_a = commit(repo, "A after tag")
        output = repo / "build" / "generated" / "build_metadata.hpp"

        generate(repo, output)
        first_content = output.read_text(encoding="utf-8")
        first_mtime = output.stat().st_mtime_ns
        assert f'#define MAKE_COMMIT "{commit_a}"' in first_content
        assert f"3.2.0-devel+{commit_a[:7]}" in first_content
        assert '#define MAKE_DIRTY "false"' in first_content

        time.sleep(0.02)
        generate(repo, output)
        assert output.read_text(encoding="utf-8") == first_content
        assert output.stat().st_mtime_ns == first_mtime

        (repo / "tracked.txt").write_text("B\n", encoding="utf-8")
        commit_b = commit(repo, "B")
        generate(repo, output)
        second_content = output.read_text(encoding="utf-8")
        assert commit_b in second_content and commit_a not in second_content
        assert f"3.2.0-devel+{commit_b[:7]}" in second_content
        assert output.stat().st_mtime_ns > first_mtime

        # Tracked changes must become dirty, refresh in place, and then remain
        # stable until the tracked state changes again.
        clean_content = second_content
        clean_mtime = output.stat().st_mtime_ns
        (repo / "tracked.txt").write_text("B dirty\n", encoding="utf-8")
        dirty_content = generated_content(repo)
        assert '#define MAKE_DIRTY "true"' in dirty_content

        original_replace = BUILD_METADATA.os.replace
        def fail_replace(source: str, destination: str) -> None:
            raise OSError("simulated atomic replacement failure")
        BUILD_METADATA.os.replace = fail_replace
        try:
            try:
                BUILD_METADATA.replace_if_changed(output, dirty_content)
            except OSError as error:
                assert "simulated atomic replacement failure" in str(error)
            else:
                raise AssertionError("atomic replacement failure was not propagated")
        finally:
            BUILD_METADATA.os.replace = original_replace
        assert output.read_text(encoding="utf-8") == clean_content
        assert output.stat().st_mtime_ns == clean_mtime
        assert_no_temporary_candidates(output)

        generate(repo, output)
        assert output.read_text(encoding="utf-8") == dirty_content
        dirty_mtime = output.stat().st_mtime_ns
        time.sleep(0.02)
        generate(repo, output)
        assert output.stat().st_mtime_ns == dirty_mtime

        run("git", "checkout", "--", "tracked.txt", cwd=repo)
        status = run("git", "status", "--porcelain", "--untracked-files=no", cwd=repo)
        assert not status.stdout, status.stdout
        generate(repo, output)
        assert '#define MAKE_DIRTY "false"' in output.read_text(encoding="utf-8")
        restored_mtime = output.stat().st_mtime_ns
        (repo / "untracked.txt").write_text("ignored by dirty contract\n", encoding="utf-8")
        time.sleep(0.02)
        generate(repo, output)
        assert '#define MAKE_DIRTY "false"' in output.read_text(encoding="utf-8")
        assert output.stat().st_mtime_ns == restored_mtime

        unusual_tag = "v4.5.6-rc.1"
        run("git", "check-ref-format", f"refs/tags/{unusual_tag}", cwd=repo)
        run("git", "tag", unusual_tag, cwd=repo)
        unusual_branch = "topic/metadata.v1+unicode-µ"
        run("git", "check-ref-format", "--branch", unusual_branch, cwd=repo)
        run("git", "checkout", "-q", "-b", unusual_branch, cwd=repo)
        generate(repo, output)
        unusual_content = output.read_text(encoding="utf-8")
        assert '#define MAKE_TAG "4.5.6-rc.1-topic-metadata-v1-unicode"' in unusual_content
        assert f'#define MAKE_RAW_BRH "{unusual_branch}"' in unusual_content
        assert '#define MAKE_BRH "topic-metadata-v1-unicode"' in unusual_content

        escape_value = 'quote" slash\\ tab\t carriage\r newline\nµ'
        compile_and_round_trip(escape_value, Path(temporary))

        concurrent_generate(repo, output)
        concurrent_mtime = output.stat().st_mtime_ns
        concurrent_hash = output.read_bytes()
        time.sleep(0.02)
        generate(repo, output)
        assert output.stat().st_mtime_ns == concurrent_mtime
        assert output.read_bytes() == concurrent_hash

        run("git", "checkout", "-q", "--detach", commit_b, cwd=repo)
        generate(repo, output)
        detached_content = output.read_text(encoding="utf-8")
        assert '#define MAKE_BRANCH_STATE "detached"' in detached_content
        assert f'#define MAKE_COMMIT "{commit_b}"' in detached_content

        run("git", "pack-refs", "--all", "--prune", cwd=repo)
        generate(repo, output)
        packed_content = output.read_text(encoding="utf-8")
        assert f'#define MAKE_COMMIT "{commit_b}"' in packed_content

        candidate = BUILD_METADATA.metadata(repo, "WsprryPi", "wsprrypi", "4.0.0-beta.1")
        short = run("git", "rev-parse", "--short", "HEAD", cwd=repo).stdout.strip()
        assert f'#define MAKE_TAG "4.0.0-beta.1+{short}"' in candidate
        assert f'#define MAKE_COMMIT "{commit_b}"' in candidate
        assert '#define MAKE_BRANCH_STATE "detached"' in candidate
        for invalid in ("v4.0.0-beta.1", "4.0", "04.0.0", "4.0.0-beta.01", "4.0.0+fake", "4.0.0\n", "$(id)"):
            try:
                BUILD_METADATA.metadata(repo, "WsprryPi", "wsprrypi", invalid)
            except BUILD_METADATA.MetadataError:
                pass
            else:
                raise AssertionError(f"accepted invalid candidate version {invalid!r}")

        outside = Path(temporary) / "not-a-repository"
        outside.mkdir()
        unavailable = generate(outside, outside / "metadata.hpp", expect=1)
        assert "build metadata generation failed:" in unavailable.stderr

    print("build metadata regression test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
