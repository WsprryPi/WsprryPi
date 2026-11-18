#!/usr/bin/env python3
"""Verify attribution publication precedes privileged executable replacement."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("copy_exe", Path(__file__).resolve().parents[1] / "copy_exe.py")
COPY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COPY)


class CopyNoticeTest(unittest.TestCase):
    def test_missing_notice_refuses_before_any_privileged_command(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = root / "src/build/bin/wsprrypi"
            binary.parent.mkdir(parents=True)
            binary.write_text("fixture never executed")
            binary.chmod(0o755)
            with patch.object(COPY, "get_git_root", return_value=root), patch.object(COPY, "run") as run:
                with self.assertRaisesRegex(RuntimeError, "license notice"):
                    COPY.main()
                run.assert_not_called()

    def test_notice_is_published_before_binary_without_service_restart(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = root / "src/build/bin/wsprrypi"
            binary.parent.mkdir(parents=True)
            binary.write_text("fixture never executed")
            binary.chmod(0o755)
            notice = root / "docs/licenses/botan/BSD-2-Clause.txt"
            notice.parent.mkdir(parents=True)
            notice.write_text("upstream fixture attribution")
            events = []
            with patch.object(COPY, "get_git_root", return_value=root), \
                    patch.object(COPY, "service_exists", return_value=False), \
                    patch.object(COPY, "run", side_effect=lambda command, **kwargs: events.append(command)), \
                    patch.object(COPY, "install_file", side_effect=lambda source, destination: events.append(["publish", str(source)])):
                COPY.main()
            notice_at = next(i for i, command in enumerate(events) if str(notice) in command)
            binary_at = next(i for i, command in enumerate(events) if command == ["publish", str(binary)])
            self.assertLess(notice_at, binary_at)
            self.assertFalse(any("systemctl" in command for command in events))


if __name__ == "__main__":
    unittest.main()
