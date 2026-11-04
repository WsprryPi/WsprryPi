"""Dedicated, isolated support-intake fixtures; no unittest suites are imported."""
import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from types import MappingProxyType

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/maintainer"))
import commit_support_bundle_intake_publication as publication
import prepare_support_bundle_intake_manifest as preparation

RAW_PUBLIC = bytes(range(32))
SIGNATURE = bytes(range(64))
SIGNING_ID = "wsprrypi-intake-2099-01"
BUNDLE_ID = "wsprrypi-bundle-2099-01"
SPKI = bytes.fromhex("302a300506032b6570032100")
# Fixed public vectors, not expectations computed with production's encoder.
RECIPIENTS = MappingProxyType({
    bytes(range(32)): "age1qqqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0savhh7m",
    bytes(reversed(range(32))): "age1ru0p68qmrgv3s9ckz52pxys3zq8surgvpv9qjzq8qczsgqczqyqqmtk0cd",
})
RECIPIENT = RECIPIENTS[bytes(reversed(range(32)))]


def encode_recipient(payload):
    return RECIPIENTS[payload]


def signing_metadata(raw_public=RAW_PUBLIC, key_id=SIGNING_ID):
    return {"schema_version": 1, "project_id": "wsprrypi",
        "purpose": "support_intake_manifest_signing", "algorithm": "Ed25519",
        "key_id": key_id, "public_key": {"encoding": "base64url", "value": base64.urlsafe_b64encode(raw_public).decode().rstrip("=")},
        "created_at_utc": "2099-01-01T00:00:00Z",
        "fingerprint": {"algorithm": "sha256", "value": hashlib.sha256(raw_public).hexdigest()}}


def bundle_metadata(recipient=RECIPIENT, key_id=BUNDLE_ID):
    return {"schema_version": 1, "project_id": "wsprrypi",
        "purpose": "support_bundle_encryption", "algorithm": "age-x25519",
        "key_id": key_id, "recipient": recipient, "created_at_utc": "2099-01-01T00:00:00Z",
        "fingerprint": {"algorithm": "sha256", "value": hashlib.sha256(recipient.encode()).hexdigest()}}


def write_key_metadata(signing, bundle, raw_public=RAW_PUBLIC):
    signing.write_text(json.dumps(signing_metadata(raw_public)) + "\n", encoding="utf-8")
    bundle.write_text(json.dumps(bundle_metadata()) + "\n", encoding="utf-8")


def assert_no_undeclared_crypto(root):
    assert not os.path.lexists(root / "openssl-undeclared"), "an undeclared crypto command was caught or ignored"


def make_fake_openssl(root, mode="success", *, verify_failure=24):
    if mode not in {"success", "wrong-public", "sign-failure", "short-signature", "verify-failure"}:
        raise ValueError("unknown cryptographic fault")
    path = root / f"openssl-{mode}"
    path.write_text(f'''#!/usr/bin/env python3
import json, pathlib, sys
a = sys.argv[1:]
with pathlib.Path({str(root / "openssl-commands.jsonl")!r}).open("a") as log:
    log.write(json.dumps(a) + "\\n")
mode = {mode!r}
def files(*indices):
    return all(not a[i].startswith("-") and pathlib.Path(a[i]).is_file() for i in indices)
if len(a) == 6 and a[:2] == ["pkey", "-in"] and a[3:] == ["-pubout", "-outform", "DER"] and files(2):
    value = {SPKI + RAW_PUBLIC!r}
    sys.stdout.buffer.write(value[:-1] + b"x" if mode == "wrong-public" else value)
    raise SystemExit(0)
if len(a) == 7 and a[:4] == ["pkeyutl", "-sign", "-rawin", "-inkey"] and a[5] == "-in" and files(4, 6):
    if mode == "sign-failure": raise SystemExit(23)
    value = {SIGNATURE!r}
    sys.stdout.buffer.write(value[:-1] if mode == "short-signature" else value)
    raise SystemExit(0)
private_verify = len(a) == 9 and a[:4] == ["pkeyutl", "-verify", "-rawin", "-inkey"] and a[5] == "-in" and a[7] == "-sigfile" and files(4, 6, 8)
public_verify = len(a) == 12 and a[:6] == ["pkeyutl", "-verify", "-pubin", "-keyform", "DER", "-inkey"] and a[7:9] == ["-rawin", "-in"] and a[10] == "-sigfile" and files(6, 9, 11)
if private_verify or public_verify:
    raise SystemExit({verify_failure} if mode == "verify-failure" else 0)
try:
    pathlib.Path({str(root / "openssl-undeclared")!r}).open("x").close()
except FileExistsError:
    pass
raise SystemExit(64)
''', encoding="utf-8")
    path.chmod(0o755)
    return path


class PublicationFixture:
    def __init__(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="wsprrypi-publication-")
        try:
            self.root = Path(self.temporary.name)
            self.git = Path("/usr/bin/git")
            if not self.git.exists(): self.git = Path(shutil.which("git") or "")
            self.openssl = make_fake_openssl(self.root)
            self.staging = self.root / "staging"; self.staging.mkdir(mode=0o700)
            self.private = self.root / "private.pem"; self.private.write_text("test private\n"); self.private.chmod(0o400)
            self.signing = self.root / "signing.json"; self.bundle = self.root / "bundle.json"
            write_key_metadata(self.signing, self.bundle)
            preparation.prepare(openssl=self.openssl, private_key=self.private,
                signing_metadata_path=self.signing, bundle_metadata_path=self.bundle,
                staging_directory=self.staging, generation=1, published_at="2099-01-01T01:00:00Z",
                expires_at="2099-04-01T01:00:00Z", status="active", minimum_client_protocol=1,
                minimum_upload_version="1.3.0", request_url="https://www.dropbox.com/request/publication-test",
                release_url="https://github.com/WsprryPi/WsprryPi/releases/latest", user_message=None)
            self.repository = self.root / "support-intake.git"
            self.run_git(["init", "--bare", "--initial-branch=main", str(self.repository)], git_dir=False)
            self.repository.chmod(0o700)
            self.run_git(["config", "core.hooksPath", os.devnull])
            self.run_git(["remote", "add", "origin", publication.EXPECTED_REMOTE])
            blob = self.run_git(["hash-object", "-w", "--stdin"], input_bytes=b"support intake\n").strip().decode()
            tree = self.run_git(["mktree"], input_bytes=f"100644 blob {blob}\tREADME.md\n".encode()).strip().decode()
            commit = self.run_git(["commit-tree", tree], input_bytes=b"Initialize support intake\n").strip().decode()
            self.run_git(["update-ref", "refs/heads/main", commit])
        except BaseException as failure:
            try:
                self.close()
            except BaseException as cleanup:
                raise BaseExceptionGroup("publication setup and cleanup failed", [failure, cleanup]) from None
            raise

    def close(self):
        self.temporary.cleanup()

    def env(self):
        value = {key: val for key, val in os.environ.items() if not key.startswith("GIT_")}
        value.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull, GIT_TERMINAL_PROMPT="0",GIT_AUTHOR_NAME="Test", GIT_AUTHOR_EMAIL="test@invalid",
            GIT_COMMITTER_NAME="Test", GIT_COMMITTER_EMAIL="test@invalid")
        return value
    def run_git(self, args, input_bytes=None, git_dir=True):
        forms = (
            ("init", "{}"), ("init", "--bare", "--initial-branch=main", "{}"),
            ("remote", "add", "origin", "{}"), ("remote", "set-url", "origin", "{}"),
            ("remote", "set-url", "--add", "--push", "origin", "{}"),
            ("config", "--unset-all", "remote.origin.pushurl"), ("config", "core.hooksPath", os.devnull),
            ("symbolic-ref", "HEAD", "refs/heads/devel"),
            ("hash-object", "-w", "--stdin"), ("mktree",),
            ("commit-tree", "{}"), ("commit-tree", "{}", "-p", "{}"),
            ("update-ref", "refs/heads/main", "{}"), ("update-ref", "refs/heads/main", "{}", "{}"),
            ("rev-parse", "refs/heads/main"), ("count-objects", "-v"),
            ("rev-list", "--parents", "-n", "1", "{}"),
            ("diff-tree", "--no-commit-id", "--name-only", "-r", "{}", "{}"),
            ("show", "{}"), ("show", "-s", "--format=%T", "{}"))
        if not any(len(args) == len(form) and all(
                actual == expected or (expected == "{}" and isinstance(actual, str) and actual and not actual.startswith("-"))
                for actual, expected in zip(args, form)) for form in forms):
            raise AssertionError("unexpected fixture Git operation")
        if not self.repository.resolve().is_relative_to(self.root.resolve()):
            raise AssertionError("fixture repository escaped its temporary root")
        if args[0] == "init" and not Path(args[-1]).resolve().is_relative_to(self.root.resolve()):
            raise AssertionError("fixture initialization escaped its temporary root")
        command = [str(self.git), "-c", "core.hooksPath=" + os.devnull] + (["--git-dir", str(self.repository)] if git_dir else []) + args
        return subprocess.run(command, input=input_bytes, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                              env=self.env(), check=True, timeout=30).stdout
    def head(self): return self.run_git(["rev-parse", "refs/heads/main"]).strip().decode()
    def publish(self, approve=False, **changes):
        values = dict(approve=approve, git_path=self.git, repository=self.repository,
            openssl=self.openssl, staging_root=self.staging, signing_metadata_path=self.signing)
        values.update(changes); return publication.commit_publication(**values)
