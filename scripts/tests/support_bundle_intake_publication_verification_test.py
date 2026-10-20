#!/usr/bin/env python3
from __future__ import annotations
import fcntl, os, sys, unittest
from unittest import mock
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]; sys.path[:0]=[str(ROOT/"scripts/maintainer"),str(ROOT/"scripts/tests")]
import verify_support_bundle_intake_publication as verify
import support_bundle_intake_publication_commit_test as fixtures
class Transport:
    def __init__(self,values,on_fetch=None): self.values=list(values); self.on_fetch=on_fetch
    def fetch(self,index):
        if self.on_fetch:self.on_fetch()
        value=self.values[index]
        if isinstance(value,Exception):raise value
        return value
class VerificationTest(unittest.TestCase):
    def setUp(self):
        self.f=fixtures.PublicationTest(methodName="runTest"); self.f.setUp(); result=self.f.publish(True)
        self.commit=result.publication_commit; self.manifest=(self.f.staging/"generation-1/intake.json").read_bytes(); self.signature=(self.f.staging/"generation-1/intake.json.sig").read_bytes()
    def tearDown(self):self.f.tearDown()
    def verify(self,transport,expected=None):
        return verify.verify_for_test(expected_commit=expected or self.commit,git_path=self.f.git,repository=self.f.repository,
            openssl=self.f.openssl,staging_root=self.f.staging,signing_metadata_path=self.f.signing,transport=transport)
    def test_exact_pair_verifies_idempotently(self):
        for _ in range(2):self.assertEqual(self.verify(Transport([self.manifest,self.signature])).status,verify.VerifyStatus.verified)
    def test_endpoint_failure_and_each_mismatch_fail_closed(self):
        self.assertEqual(self.verify(Transport([RuntimeError("down"),self.signature])).status,verify.VerifyStatus.retrieval_failed)
        self.assertEqual(self.verify(Transport([self.manifest,RuntimeError("down")])).status,verify.VerifyStatus.retrieval_failed)
        self.assertEqual(self.verify(Transport([RuntimeError("oversize"),self.signature])).status,verify.VerifyStatus.retrieval_failed)
        self.assertEqual(self.verify(Transport([self.manifest+b"x",self.signature])).status,verify.VerifyStatus.content_mismatch)
        self.assertEqual(self.verify(Transport([self.manifest,self.signature+b"x"])).status,verify.VerifyStatus.content_mismatch)
        self.assertEqual(self.verify(Transport([self.signature,self.manifest])).status,verify.VerifyStatus.content_mismatch)
    def test_wrong_expected_commit_is_local_failure_without_fetch(self):
        transport=mock.Mock(); result=self.verify(transport,"a"*40)
        self.assertEqual(result.status,verify.VerifyStatus.local_validation_failed); transport.fetch.assert_not_called()
    def test_independent_authentication_failure_is_distinct(self):
        real=verify.lifecycle.authenticate_generation; calls=0
        def fail_second(*args,**kwargs):
            nonlocal calls; calls+=1
            if calls==2:raise verify.lifecycle.LifecycleError("injected")
            return real(*args,**kwargs)
        with mock.patch.object(verify.lifecycle,"authenticate_generation",side_effect=fail_second):
            result=self.verify(Transport([self.manifest,self.signature]))
        self.assertEqual(result.status,verify.VerifyStatus.authentication_failed)
    def test_result_contract_does_not_disclose_publication_contents(self):
        result=self.verify(Transport([self.manifest,self.signature]))
        self.assertEqual(set(result.__dict__),{"status","generation","manifest_sha256","candidate_commit"})
        rendered=repr(result)
        for forbidden in ("request_url","release_url","user_message","signature","key_id"):
            self.assertNotIn(forbidden,rendered)
    def test_production_transport_preflight_failure_is_typed(self):
        with mock.patch.object(verify,"CurlTransport",side_effect=OSError("injected")):
            result=verify.verify(expected_commit=self.commit,git_path=self.f.git,
                repository=self.f.repository,openssl=self.f.openssl,
                staging_root=self.f.staging,signing_metadata_path=self.f.signing)
        self.assertEqual(result,verify.VerifyResult(verify.VerifyStatus.retrieval_failed))
    def test_staging_lock_is_held_during_fetch(self):
        observed=False
        def probe():
            nonlocal observed
            fd=os.open(self.f.staging,os.O_RDONLY|getattr(os,"O_DIRECTORY",0))
            try:
                with self.assertRaises(BlockingIOError):fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
                observed=True
            finally:os.close(fd)
        self.assertEqual(self.verify(Transport([self.manifest,self.signature],probe)).status,verify.VerifyStatus.verified); self.assertTrue(observed)
if __name__=="__main__":unittest.main()
