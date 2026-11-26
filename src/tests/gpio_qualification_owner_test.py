#!/usr/bin/env python3
"""Exercise the real one-call dispatcher and disposable workers without MMIO."""
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import time
import uuid

CONTRACT = "wsprrypi.gpio-qualification-owner.v1"
CARRIERS = (475700, 1838100, 3570100, 5366200, 7040100,
            10140200, 14097100, 18106100, 21096100, 28126100)
BANDS = ("630m", "160m", "80m", "60m", "40m", "30m", "20m", "17m", "15m", "10m")
POINTS = [{"point_id": f"P{10 * code + col + 1:03d}", "carrier_hz": carrier,
           "band": band, "dial_hz": carrier - 1500, "drive_ma": 2 * (code + 1)}
          for code in range(8) for col, (band, carrier) in enumerate(zip(BANDS, CARRIERS))]
BIN = str(Path(sys.argv[1]).resolve())


class Case:
    def __init__(self, path, pin):
        self.path = path
        self.pin = pin
        self.path.mkdir(mode=0o700)
        (path / "station").write_text("fixed station")
        (path / "device").write_text("1" * 32)
        (path / "boot").write_text("2" * 32)
        self.correction()
        self.source = None
        self.run = uuid.uuid4().hex
        self.owner = None
        self.instance = None
        self.worker = None

    def correction(self, ppm=0):
        (self.path / "clock.json").write_text(json.dumps({
            "policy": "selected" if ppm else "uncalibrated",
            "ppm_millionths": ppm, "provenance": "Simulation vector",
        }))

    def fault(self, name):
        (self.path / "fault").write_text(name)

    def execute(self, request, *, discard=False):
        raw = json.dumps(request, separators=(",", ":")).encode()
        result = subprocess.run([BIN, "--fixture", str(self.path)], input=raw,
                                capture_output=True, check=True, timeout=3)
        if discard:
            return None
        reply = json.loads(result.stdout)
        assert set(reply) == {"operation", "contract", "receipt", "request_sha256", "call"}
        assert reply["contract"] == CONTRACT and reply["operation"] == request["operation"]
        assert reply["call"] == request["call"]
        assert reply["request_sha256"] == hashlib.sha256(raw).hexdigest()
        return reply["receipt"]

    def call(self, operation, *, scoped=True, discard=False, **fields):
        request = {"contract": CONTRACT, "operation": operation, "call": uuid.uuid4().hex}
        if scoped:
            request.update(source=self.source, run=self.run)
        return self.execute({**request, **fields}, discard=discard)

    def review(self, *, pin=None, mux=None, profile="bcm2835"):
        gpio = pin or self.pin
        return self.call("review", scoped=False, route={
            "profile": profile, "gpio": gpio, "mux": mux or {4: "ALT0", 20: "ALT5"}[gpio],
        })

    def plans(self):
        return [self.call("admit", scoped=False, source=self.source, point=point, duration_ms=50000)
                for point in POINTS]

    def review_matrix(self, points=POINTS):
        return self.call("review_matrix", scoped=False, route={
            "profile": "bcm2835", "gpio": self.pin, "mux": {4: "ALT0", 20: "ALT5"}[self.pin],
        }, points=points, duration_ms=50000)

    def open(self, *, discard=False, expected_recovery=False):
        self.source = self.review()
        assert self.source["gpio"] == self.pin
        self.matrix = self.plans()
        prior_files = sorted(self.path.iterdir())
        assert self.review_matrix() == {"source": self.source, "admissions": self.matrix}
        assert sorted(self.path.iterdir()) == prior_files, "Read-only Review acquired owner state"
        assert all(plan["deadline_ms"] == 50000 and plan["refusal"] is None for plan in self.matrix)
        for point, row in zip(POINTS, self.matrix):
            target = point["carrier_hz"] * 10**9
            assert target - 10**9 <= row["lower_nhz"] <= row["upper_nhz"] <= target + 10**9
        claim = self.call("claim", lifetime_ms=6145000, discard=discard)
        if discard:
            claim = self.call("lookup", kind="claim")
        assert claim["exclusive"] and claim["durable"] and claim["other_gpio_input"]
        self.owner = claim["owner"]
        self.bank = claim["bank"]
        self.supervisor_pid = self.journal()["supervisor_pid"]
        matrix = self.call("admit_matrix", points=POINTS, duration_ms=50000)
        assert matrix.get("admissions") == self.matrix, matrix
        return self.group(0, discard=discard, expected_recovery=expected_recovery)

    def group(self, code, *, discard=False, expected_recovery=False):
        self.instance = uuid.uuid4().hex
        result = self.call("open_group", instance=self.instance, drive_code=code,
                           lifetime_ms=750000, discard=discard)
        if discard:
            result = self.call("lookup", kind="open_group", instance=self.instance)
        end = time.monotonic() + 2
        while result["phase"] == "pending" and time.monotonic() < end:
            time.sleep(.02)
            result = self.call("lookup", kind="open_group", instance=self.instance)
        if expected_recovery:
            return result
        assert result["phase"] == "ready" and result["listen"] == "127.0.0.1"
        self.worker = result["worker"]
        return result

    def reserve(self, index, *, discard=False):
        identity = {"instance": self.instance, "worker": self.worker, "request": uuid.uuid4().hex}
        row = {"point": POINTS[index], "admission": self.matrix[index], "duration_ms": 50000}
        receipt = self.call("reserve", **identity, **row, discard=discard)
        if discard:
            receipt = self.call("lookup", kind="reserve", **identity)
        assert receipt["phase"] == "reserved" and receipt["ceiling_ms"] == 0
        identity["generation"] = receipt["generation"]
        return identity, row

    def close(self):
        closed = self.call("close_group", instance=self.instance)
        assert closed["exited"] and closed["bank"] == self.bank
        assert closed["gpio_input"] and closed["other_gpio_input"] and closed["dma_idle"]
        assert self.call("lookup", kind="close_group", instance=self.instance) == closed

    def release(self):
        result = self.call("release")
        assert result["released"] and result["bank"] == self.bank
        assert self.call("lookup", kind="release") == result
        assert not (self.path / "recovery.json").exists()

    def journal(self):
        return json.loads((self.path / "recovery.json").read_text())

    def registers(self):
        return struct.unpack("=Ii", (self.path / "registers").read_bytes())

    def cleanup(self):
        path = self.path / "recovery.json"
        if path.exists():
            pid = self.journal()["supervisor_pid"]
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass


def full_matrix(case):
    case.fault("pending")
    case.open(discard=True)
    case.fault("")
    if evidence := os.environ.get("WSPRRYPI_QUALIFICATION_EVIDENCE"):
        (Path(evidence) / f"pi-gpio{case.pin}-admissions.json").write_text(
            json.dumps({"source": case.source, "admissions": case.matrix}, indent=2) + "\n")
    other = case.review(pin=20 if case.pin == 4 else 4)
    blocked = case.call("claim", source=other, run=uuid.uuid4().hex, lifetime_ms=6145000)
    assert blocked["phase"] == "refused"
    stale = None
    for index, row in enumerate(POINTS):
        if index and index % 10 == 0:
            case.group(index // 10)
        identity, request = case.reserve(index, discard=index % 10 == 0)
        accepted = case.call("start", **request, **identity, discard=index == 0)
        if accepted:
            assert accepted["phase"] == "accepted" and accepted["owner"] == case.owner
        active = case.call("status", **identity)
        assert active["mux"] == {4: "ALT0", 20: "ALT5"}[case.pin] and active["other_gpio_input"]
        assert active["phase"] == "active" and active["bank"]["drive"] == row["drive_ma"] // 2 - 1
        assert case.call("status", **identity) == active
        if stale:
            assert case.call("stop", **stale)["phase"] == "refused"
            assert case.call("status", **identity)["phase"] == "active"
        terminal = case.call("stop", **identity)
        assert terminal["phase"] == "terminal" and terminal["bank"] == case.bank
        assert terminal["ceiling_ms"] - terminal["armed_at_ms"] == 50000
        assert terminal["armed_at_ms"] <= terminal["inactive_at_ms"] <= terminal["ceiling_ms"]
        assert terminal["gpio_input"] and terminal["other_gpio_input"] and terminal["pwm_idle"]
        stale = identity
        if index % 10 == 9:
            case.close()
    case.release()
    print(f"GPIO{case.pin}: 80 exact points, eight disposable groups, verified terminal and idle proofs")


def refused(case):
    assert case.review_matrix(POINTS[:-1])["phase"] == "refused"
    assert case.review_matrix(POINTS[::-1])["phase"] == "refused"
    case.fault("service")
    assert case.review_matrix()["phase"] == "refused"
    case.fault("")
    assert case.review(mux="ALT5")["phase"] == "refused"
    assert case.review(profile="rp1")["phase"] == "refused"
    case.fault("model")
    assert case.review()["phase"] == "refused"
    case.fault("")
    for ppm, band in ((-76332200, "630m"), (-71997500, "160m")):
        case.correction(ppm)
        case.source = case.review()
        rows = case.plans()
        matches = [row for row in rows if row["refusal"]]
        assert len(matches) == 8, matches
        assert all(row["ppm_millionths"] == ppm and row["plan"] is row["lower_nhz"] is None
                   and row["carrier_hz"] == CARRIERS[BANDS.index(band)]
                   and "divider boundary" in row["refusal"] for row in matches), matches
    assert not (case.path / "recovery.json").exists()
    # Reject duplicate names before constructing a reply or claiming a lease.
    raw = b'{"operation":"review","operation":"claim"}'
    completed = subprocess.run([BIN, "--fixture", str(case.path)], input=raw,
                               capture_output=True, timeout=3)
    assert completed.returncode and b"Duplicate JSON key" in completed.stderr
    print("Native 630 m and 160 m boundary refusals preserve the requested carrier and correction")


def stream(case):
    route = {"profile": "bcm2835", "gpio": 20, "mux": "ALT5"}
    source = case.review()
    requests = [
        {"operation": "review", "route": route},
        {"operation": "admit", "source": source, "point": POINTS[0], "duration_ms": 50000},
    ]
    packets = [json.dumps({"contract": CONTRACT, "call": uuid.uuid4().hex, **request},
                          separators=(",", ":")).encode() for request in requests]
    run = subprocess.run([BIN, "--fixture", str(case.path), "--runtime-stream"],
                         input=b"\n".join(packets) + b"\n", capture_output=True,
                         check=True, timeout=3)
    ready, first, second = run.stdout.splitlines()
    assert ready == (CONTRACT + " ready").encode()
    for raw, reply in zip(packets, (first, second)):
        received = json.loads(reply)
        sent = json.loads(raw)
        assert received["call"] == sent["call"] and received["operation"] == sent["operation"]
        assert received["request_sha256"] == hashlib.sha256(raw).hexdigest()
    assert json.loads(first)["receipt"] == source
    assert json.loads(second)["receipt"]["carrier_hz"] == 475700
    assert not (case.path / "owner.sock").exists()


def stop_before_start(case):
    case.open()
    identity, request = case.reserve(0)
    assert case.call("stop", **identity)["phase"] == "cancelled-before-enable"
    assert case.journal()["phase"] == "cancelled-before-enable"
    assert case.call("start", **identity, **request)["phase"] == "refused"
    assert case.registers() == (27, 0)
    case.close()
    case.release()
    print("Identified Stop durably cancels a reservation before delayed Start")


def fault_scenario(case, fault):
    if fault == "publication":
        case.source = case.review()
        case.fault(fault)
        assert case.call("claim", lifetime_ms=6145000)["phase"] == "refused"
        assert not (case.path / "recovery.json").exists()
        assert case.registers() == (27, 0)
        return
    if fault == "prepare":
        case.fault(fault)
        assert case.open(expected_recovery=True)["phase"] == "unresolved"
        assert case.registers() == (27, 0)
        return
    case.open()
    identity, request = case.reserve(0)
    if fault == "station":
        (case.path / "station").write_text("changed station")
        assert case.call("start", **identity, **request)["phase"] == "refused"
        assert case.registers() == (27, 0)
        case.call("stop", **identity)
        case.close()
        case.release()
        return
    if fault in ("pre-enable", "process-loss"):
        case.fault(fault)
        assert case.call("start", **identity, **request)["phase"] == "unresolved"
    else:
        case.call("start", **identity, **request)
        if fault == "supervisor-loss":
            os.kill(case.supervisor_pid, signal.SIGKILL)
            until = time.monotonic() + 1
            while case.registers() != (27, 0) and time.monotonic() < until:
                time.sleep(.01)
            (case.path / "boot").write_text("3" * 32)
            assert case.call("claim", source=case.review(), run=uuid.uuid4().hex,
                             lifetime_ms=6145000)["phase"] == "unresolved"
        elif fault == "cutoff":
            active = case.call("status", **identity)
            start = time.monotonic()
            while case.registers()[1] and time.monotonic() - start < 50:
                time.sleep(.05)
            assert case.registers() == (27, 0)
            assert time.monotonic() - start < 50
            assert active["ceiling_ms"] - active["armed_at_ms"] == 50000
            time.sleep(.35)
            assert case.call("status", **identity)["phase"] == "terminal"
            case.close()
            case.release()
            return
        else:
            case.fault(fault)
            if fault == "service":
                time.sleep(.6)
            elif fault == "blocked-status":
                with concurrent.futures.ThreadPoolExecutor() as threads:
                    delayed = threads.submit(case.call, "status", **identity)
                    time.sleep(.02)
                    case.call("stop", **identity)
                    delayed.result()
            else:
                case.call("stop", **identity)
    expected_pad = 4 if fault == "external-pad" else 24 if fault == "restore" else 27
    assert case.registers() == (expected_pad, 0), (fault, case.registers())
    assert (case.path / "recovery.json").exists()
    assert case.call("release")["phase"] == "unresolved"
    assert case.call("open_group", instance=uuid.uuid4().hex, drive_code=1,
                     lifetime_ms=750000)["phase"] == "unresolved"


def main():
    with tempfile.TemporaryDirectory(prefix="q-") as directory:
        root = Path(directory)
        cases = []
        def run(name, pin, test, *args):
            case = Case(root / name, pin)
            cases.append(case)
            test(case, *args)
            print(f"{name}: PASS", flush=True)

        try:
            with concurrent.futures.ThreadPoolExecutor(max_workers=4) as threads:
                tasks = [threads.submit(run, "cutoff", 4, fault_scenario, "cutoff"),
                         threads.submit(run, "gpio4", 4, full_matrix),
                         threads.submit(run, "gpio20", 20, full_matrix)]
                run("planner", 4, refused)
                run("stream", 20, stream)
                run("cancel", 20, stop_before_start)
                for fault in ("station", "service", "prepare", "pre-enable", "process-loss",
                              "supervisor-loss", "external-pad", "restore", "publication",
                              "blocked-status"):
                    run(fault, 4, fault_scenario, fault)
                for task in tasks:
                    task.result()
        finally:
            for case in cases:
                case.cleanup()


if __name__ == "__main__":
    main()
