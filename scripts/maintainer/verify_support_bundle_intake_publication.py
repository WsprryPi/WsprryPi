#!/usr/bin/env python3
"""Verify the exact publicly retrieved WsprryPi intake publication pair."""
from __future__ import annotations
import argparse, fcntl, os, subprocess, tempfile
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
import stat
from typing import Protocol
import commit_support_bundle_intake_publication as publication
import manage_support_bundle_intake_manifest as lifecycle
import prepare_support_bundle_intake_manifest as preparation

URLS=("https://raw.githubusercontent.com/WsprryPi/support-intake/main/wsprrypi/intake.json",
      "https://raw.githubusercontent.com/WsprryPi/support-intake/main/wsprrypi/intake.json.sig")
class VerifyStatus(Enum):
    verified="verified"; retrieval_failed="retrieval_failed"; content_mismatch="content_mismatch"
    authentication_failed="authentication_failed"; local_validation_failed="local_validation_failed"
@dataclass(frozen=True)
class VerifyResult:
    status: VerifyStatus; generation:int=0; manifest_sha256:str=""; candidate_commit:str=""
class Transport(Protocol):
    def fetch(self,index:int)->bytes: ...
class CurlTransport:
    def __init__(self) -> None:
        info = os.lstat("/usr/bin/curl")
        if (not stat.S_ISREG(info.st_mode) or info.st_uid != 0
                or stat.S_IMODE(info.st_mode) & 0o022 or not os.access("/usr/bin/curl", os.X_OK)):
            raise RuntimeError("curl executable is unsafe")

    def fetch(self,index:int)->bytes:
        maximum=(preparation.MAX_MANIFEST_BYTES,preparation.MAX_ENVELOPE_BYTES)[index]
        env={k:v for k,v in os.environ.items() if k in {"PATH","LANG","LC_ALL","TZ","TMPDIR"}}
        with tempfile.TemporaryFile() as body:
            done=subprocess.run(["/usr/bin/curl","--disable","--silent","--show-error","--fail",
                "--noproxy","*",
                "--proto","=https","--max-time","15","--connect-timeout","5","--write-out","\n%{http_code}","--output","-",URLS[index]],
                stdin=subprocess.DEVNULL,stdout=body,stderr=subprocess.DEVNULL,env=env,check=False,timeout=20)
            body.seek(0); value=body.read(maximum+5)
        if done.returncode or len(value)<4 or value[-4:]!=b"\n200" or len(value[:-4])>maximum: raise RuntimeError("retrieval failed")
        return value[:-4]
def verify_for_test(*,expected_commit:str,git_path:Path,repository:Path,openssl:Path,staging_root:Path,
                    signing_metadata_path:Path,transport:Transport)->VerifyResult:
    try:
        root=preparation.require_staging(staging_root); fd=os.open(root,os.O_RDONLY|getattr(os,"O_DIRECTORY",0))
    except Exception: return VerifyResult(VerifyStatus.local_validation_failed)
    try:
        fcntl.flock(fd,fcntl.LOCK_SH)
        source=lifecycle.authenticate_current(root,openssl,signing_metadata_path)
        candidate=publication.validate_repository(git_path,repository)
        if candidate!=expected_commit: return VerifyResult(VerifyStatus.local_validation_failed)
        parents=publication.single_line(publication.git(git_path,repository,["rev-list","--parents","-n","1",candidate]),"parents").split()
        if len(parents)!=2: return VerifyResult(VerifyStatus.local_validation_failed)
        publication.verify_commit(git_path,repository,parents[1],candidate,source.manifest_bytes,source.signature_path.read_bytes())
        try: remote=(transport.fetch(0),transport.fetch(1))
        except Exception: return VerifyResult(VerifyStatus.retrieval_failed,source.value["generation"],source.manifest_sha256,candidate)
        if remote!=(source.manifest_bytes,source.signature_path.read_bytes()):
            return VerifyResult(VerifyStatus.content_mismatch,source.value["generation"],source.manifest_sha256,candidate)
        try:
            with tempfile.TemporaryDirectory(prefix="wsprrypi-public-verify-") as temporary:
                temp=Path(temporary); temp.chmod(0o700)
                generation=temp/f"generation-{source.value['generation']}"; generation.mkdir(mode=0o700)
                (generation/"intake.json").write_bytes(remote[0]); (generation/"intake.json.sig").write_bytes(remote[1])
                (generation/"intake.json").chmod(0o600); (generation/"intake.json.sig").chmod(0o600)
                authenticated=lifecycle.authenticate_generation(generation,openssl,signing_metadata_path)
                if authenticated.value["generation"]!=source.value["generation"] or authenticated.manifest_sha256!=source.manifest_sha256:
                    raise ValueError("identity mismatch")
        except Exception: return VerifyResult(VerifyStatus.authentication_failed,source.value["generation"],source.manifest_sha256,candidate)
        return VerifyResult(VerifyStatus.verified,source.value["generation"],source.manifest_sha256,candidate)
    except Exception: return VerifyResult(VerifyStatus.local_validation_failed)
    finally:
        try: fcntl.flock(fd,fcntl.LOCK_UN)
        finally: os.close(fd)
def verify(**kwargs):
    try:
        transport = CurlTransport()
    except Exception:
        return VerifyResult(VerifyStatus.retrieval_failed)
    return verify_for_test(transport=transport, **kwargs)
def main(argv):
    p=argparse.ArgumentParser(); p.add_argument("--expected-commit",required=True); p.add_argument("--git",required=True,type=Path)
    p.add_argument("--repository",required=True,type=Path); p.add_argument("--openssl",required=True,type=Path)
    p.add_argument("--staging-root",required=True,type=Path); p.add_argument("--signing-metadata",required=True,type=Path); a=p.parse_args(argv)
    r=verify(expected_commit=a.expected_commit,git_path=a.git,repository=a.repository,openssl=a.openssl,staging_root=a.staging_root,signing_metadata_path=a.signing_metadata)
    print(f"status: {r.status.value}");
    if r.generation: print(f"generation: {r.generation}\nmanifest SHA-256: {r.manifest_sha256}\ncandidate commit: {r.candidate_commit}")
    return 0 if r.status is VerifyStatus.verified else 1
if __name__=="__main__": raise SystemExit(main(None))
