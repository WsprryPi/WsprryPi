#!/usr/bin/env python3
"""Hardware-free archive integrity/extraction boundary checks."""
import hashlib
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    "provider_build", Path(__file__).resolve().parents[1] / "build_pairing_provider.py")
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)


class ProviderBuildTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def archive(self, entries):
        result = self.root / "source.tar.gz"
        with tarfile.open(result, "w:gz") as archive:
            for name, data, target in entries:
                entry = tarfile.TarInfo(name)
                if target is not None:
                    entry.type = tarfile.SYMTYPE
                    entry.linkname = target
                    archive.addfile(entry)
                else:
                    entry.size = len(data)
                    archive.addfile(entry, io.BytesIO(data))
        return result

    def test_checksum_is_verified_for_cached_archives(self):
        expected = hashlib.sha256(b"approved").hexdigest()
        (self.root / (expected + ".tar.gz")).write_bytes(b"changed")
        with patch.object(BUILD.urllib.request, "urlopen") as network:
            with self.assertRaisesRegex(ValueError, "checksum"):
                BUILD.verified_archive(self.root, {"sha256": expected})
            network.assert_not_called()

    def test_macos_deployment_targets_have_distinct_provider_builds(self):
        with patch.object(BUILD.platform, "system", return_value="Darwin"), \
             patch.object(BUILD.subprocess, "check_output", return_value="clang fixture"):
            with patch.dict(os.environ, {"MACOSX_DEPLOYMENT_TARGET": "26.0"}):
                tahoe = BUILD.build_key({}, "clang++", "arm64")
            with patch.dict(os.environ, {"MACOSX_DEPLOYMENT_TARGET": "27.0"}):
                newer = BUILD.build_key({}, "clang++", "arm64")
        self.assertNotEqual(tahoe, newer)

    def test_provider_cannot_inherit_parent_git_revision(self):
        # A real nested archive tree must not discover the enclosing repository.
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        source = self.root / "build/source"
        source.mkdir(parents=True)
        with patch.dict(os.environ, {"GIT_DIR": str(self.root / ".git"),
                                     "GIT_WORK_TREE": str(self.root)}):
            result = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=source,
                                    env=BUILD.vendor_environment(source), capture_output=True)
        self.assertNotEqual(result.returncode, 0)

    def test_archive_path_cannot_escape(self):
        archive = self.archive([("botan/../escaped", b"data", None)])
        with self.assertRaisesRegex(ValueError, "unsafe"):
            BUILD.unpack(archive, self.root / "output")
        self.assertFalse((self.root / "escaped").exists())

    def test_external_and_chained_links_are_refused(self):
        for target in ("../../outside", "/tmp/outside", "other"):
            archive = self.archive([("botan/config", b"", target),
                                    ("botan/other", b"", "regular"),
                                    ("botan/regular", b"data", None)])
            with self.subTest(target=target), self.assertRaises(ValueError):
                BUILD.unpack(archive, self.root / target.replace("/", "_"))

    def test_internal_upstream_link_is_materialized_without_symlinks(self):
        archive = self.archive([("botan/.github/codecov.yml", b"", "../src/codecov.yml"),
                                ("botan/src/codecov.yml", b"approved", None)])
        output = self.root / "output"
        BUILD.unpack(archive, output)
        result = output / ".github/codecov.yml"
        self.assertFalse(result.is_symlink())
        self.assertEqual(result.read_bytes(), b"approved")

    def test_duplicate_paths_and_oversized_source_are_refused(self):
        archive = self.archive([("botan/file", b"data", None)] * 2)
        with self.assertRaisesRegex(ValueError, "duplicate"):
            BUILD.unpack(archive, self.root / "duplicates")
        archive = self.archive([("botan/file", b"data", None)])
        with patch.object(BUILD, "MAX_SOURCE", 2), self.assertRaisesRegex(ValueError, "bound"):
            BUILD.unpack(archive, self.root / "oversized")


if __name__ == "__main__":
    unittest.main()
