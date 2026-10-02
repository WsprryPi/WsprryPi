#!/usr/bin/env python3
"""Hardware-free regression tests for the container export boundary."""
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    "container_build", Path(__file__).resolve().parents[1] / "container_build.py")
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


class ContainerBuildTest(unittest.TestCase):
    def exercise(self, target, *, failure=None, existing=False):
        with tempfile.TemporaryDirectory() as root:
            output = Path(root) / "artifact with spaces"
            if existing:
                output.mkdir()
            calls = []
            image = "sha256:" + "a" * 64

            def docker(*args, **kwargs):
                calls.append(args)
                if args[1] == "buildx":
                    self.assertIn(module.TARGETS[target], args)
                    Path(args[args.index("--iidfile") + 1]).write_text(image)
                elif args[1] == "create":
                    self.assertEqual(args[-1], image)
                    self.assertTrue(args[args.index("--name") + 1].startswith("wsprrypi-export-"))
                    if failure == "create-interrupt":
                        raise KeyboardInterrupt
                    return subprocess.CompletedProcess(args, 0, "test-container\n")
                elif args[1] == "cp":
                    stage = Path(args[-1])
                    for name in module.EVIDENCE:
                        (stage / name).write_text("evidence\n")
                    if failure in ("evidence", "avahi-evidence"):
                        (stage / ("elf-dynamic.txt" if failure == "avahi-evidence" else "elf-header.txt")).unlink()
                    (stage / "wsprrypi").write_bytes(b"binary")
                    if failure == "copy":
                        raise subprocess.CalledProcessError(1, args)
                    if failure == "copy-interrupt":
                        raise KeyboardInterrupt
                    digest = hashlib.sha256(b"binary").hexdigest()
                    if failure == "checksum":
                        digest = "0" * 64
                    (stage / "SHA256SUMS").write_text(f"{digest}  wsprrypi\n")
                    cpu, release = target.split("-")
                    arch = "armhf" if cpu == "armv6" else "arm64"
                    if failure == "target":
                        release = "wrong"
                    (stage / "target.txt").write_text(
                        f"cpu={cpu}\nrelease={release}\npackage_architecture={arch}\n")
                    if failure == "collision":
                        output.mkdir(exist_ok=True)
                        (output / "keep").write_text("other export")
                return subprocess.CompletedProcess(args, 0, "")

            with patch.object(module, "run", side_effect=docker), \
                    patch.object(module.subprocess, "run") as cleanup:
                if failure:
                    with self.assertRaises((ValueError, subprocess.CalledProcessError,
                                            KeyboardInterrupt)):
                        module.build(target, output)
                    self.assertFalse((output / "wsprrypi").exists())
                    if failure == "collision":
                        self.assertEqual((output / "keep").read_text(), "other export")
                    else:
                        cleanup.assert_called_once()
                else:
                    module.build(target, output)
                    self.assertEqual((output / "build-image-id.txt").read_text(), image + "\n")
                    cleanup.assert_not_called()
                self.assertFalse(list(Path(root).glob(".wsprrypi-export-*")))

    def test_four_targets(self):
        for target in module.TARGETS:
            with self.subTest(target=target):
                self.exercise(target)

    def test_empty_destination(self):
        self.exercise("armv6-bookworm", existing=True)

    def test_failed_exports(self):
        for failure in ("copy", "checksum", "target", "collision", "evidence",
                        "create-interrupt", "copy-interrupt", "avahi-evidence"):
            with self.subTest(failure=failure):
                self.exercise("armv6-trixie", failure=failure)

    def test_reject_destinations_before_build(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            occupied = root / "occupied"
            occupied.mkdir()
            (occupied / "keep").touch()
            plain = root / "file"
            plain.touch()
            link = root / "symlink"
            link.symlink_to(root / "missing")
            for output in (occupied, plain, link):
                with self.subTest(output=output), patch.object(module, "run") as run:
                    with self.assertRaises(ValueError):
                        module.build("aarch64-trixie", output)
                    run.assert_not_called()

    def test_entry_points_and_removed_target(self):
        scripts = module.REPO / "scripts"
        wrappers = {p.name for p in scripts.glob("build-*-container.sh")}
        self.assertEqual(wrappers, {f"build-{t}-container.sh" for t in module.TARGETS})
        self.assertFalse((module.REPO / "containers/armv6-bullseye/Dockerfile").exists())
        result = subprocess.run(
            ["python3", str(scripts / "container_build.py"), "armv6-bullseye"],
            capture_output=True)
        self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
