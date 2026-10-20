#!/usr/bin/env python3
"""Exercise the UI-owned PHP identity routes and rendered page metadata."""

from __future__ import annotations

import json
import re
import shutil
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def free_port() -> int:
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def fetch(url: str) -> tuple[int, dict[str, str], bytes]:
    try:
        response = urllib.request.urlopen(url, timeout=5)
    except urllib.error.HTTPError as error:
        response = error
    with response:
        return response.status, dict(response.headers.items()), response.read()


with tempfile.TemporaryDirectory() as temporary:
    web_root = Path(temporary) / "data"
    shutil.copytree(ROOT / "data", web_root)
    subprocess.run(
        [
            "python3", str(ROOT / "scripts" / "generate_ui_manifest.py"),
            "--ui-root", str(web_root),
            "--output", str(web_root / "ui-manifest.json"),
            "--source-commit", "d" * 40,
            "--application-version", "1.2.3",
        ],
        check=True,
        capture_output=True,
        text=True,
    )

    port = free_port()
    server = subprocess.Popen(
        ["php", "-S", f"127.0.0.1:{port}", "-t", str(web_root)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        base = f"http://127.0.0.1:{port}"
        for _ in range(50):
            try:
                status, _, _ = fetch(f"{base}/ui-version.php")
                if status == 200:
                    break
            except OSError:
                pass
            time.sleep(0.05)
        else:
            raise AssertionError("PHP test server did not become ready")

        status, headers, body = fetch(f"{base}/ui-version.php")
        assert status == 200
        assert "no-store" in headers.get("Cache-Control", "")
        identity = json.loads(body)
        assert identity["installed_state"] == "packaged"
        assert identity["installed_ui_build_id"] == identity["packaged_ui_build_id"]
        assert re.fullmatch(r"sha256:[0-9a-f]{64}", identity["installed_ui_build_id"])

        status, page_headers, page = fetch(f"{base}/index.php")
        assert status == 200
        assert "no-cache" in page_headers.get("Cache-Control", "")
        html = page.decode("utf-8")
        match = re.search(
            r'window\.WSPRRYPI_INSTALLED_UI_BUILD_ID = "(sha256:[0-9a-f]{64})";',
            html,
        )
        assert match and match.group(1) == identity["installed_ui_build_id"]
        encoded_identity = identity["installed_ui_build_id"].replace(":", "%3A")
        assert f"site.css?v={encoded_identity}" in html
        assert f"site.js?v={encoded_identity}" in html
        assert f"site.webmanifest?v={encoded_identity}" in html

        status, manifest_headers, manifest_body = fetch(f"{base}/ui-manifest.php")
        assert status == 200
        assert "no-store" in manifest_headers.get("Cache-Control", "")
        exposed_manifest = json.loads(manifest_body)
        assert exposed_manifest["packaged_ui_build_id"] == identity["packaged_ui_build_id"]

        with (web_root / "site.css").open("a", encoding="utf-8") as stylesheet:
            stylesheet.write("\n/* local edit */\n")
        status, _, edited_body = fetch(f"{base}/ui-version.php")
        assert status == 200
        edited_identity = json.loads(edited_body)
        assert edited_identity["installed_state"] == "locally_modified"
        assert edited_identity["modified_files"] == ["site.css"]
        assert edited_identity["installed_ui_build_id"] != identity["installed_ui_build_id"]
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait(timeout=5)
        stdout, stderr = server.communicate()
        if server.returncode not in (0, -15):
            raise RuntimeError(f"PHP server failed ({server.returncode})\n{stdout}\n{stderr}")

print("ui_identity_http_integration_test passed")
