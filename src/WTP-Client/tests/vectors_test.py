#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Pinned normative vectors plus independent wire/schema boundary expectations.

Uses only the Python standard library and a hardware-free C++ codec adapter.
Transition vectors belong to the later session slice and are not claimed here.
"""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
provenance = json.loads((ROOT / "PROVENANCE.json").read_text())
for item in provenance["files"]:
    if item["kind"] == "unmodified test snapshot":
        assert hashlib.sha256((ROOT / item["local"]).read_bytes()).hexdigest() == item["upstream_sha256"], item
vectors = json.loads((ROOT / "tests/fixtures/wtp-1.json").read_text())
contract = json.loads((ROOT / "tests/fixtures/wtp-1-contract.json").read_text())
cases = []


def add(name, message, valid=True):
    raw = (json.dumps(message, separators=(",", ":"), ensure_ascii=True).encode()
           if isinstance(message, dict) else message)
    if isinstance(raw, str):
        raw = raw.encode()
    cases.append((name, raw, valid))


def altered(name, message, path, value, valid=False):
    changed = copy.deepcopy(message)
    cursor = changed
    for part in path[:-1]:
        cursor = cursor[part]
    cursor[path[-1]] = value
    add(name, changed, valid)


for case in vectors["schema_cases"]:
    add(case["name"], case["message"], case["valid"])
for case in vectors["raw_json_cases"]:
    add(case["name"], case["json"], case["valid"])

sid, rid, jid, owner, boot = "1" * 32, "a" * 32, "3" * 32, "2" * 32, "5" * 32


def request(op, body):
    return dict(type="request", protocol="WTP/1", session_id=sid, request_id=rid, op=op, body=body)


def response(op, body):
    return dict(type="response", protocol="WTP/1", session_id=sid, request_id=rid, op=op, ok=True, body=body)


def event(kind, body):
    return dict(type="event", protocol="WTP/1", session_id=sid, boot_id=boot, event_id="0", event=kind, body=body)


error = dict(code="MISSED_START", message="missed", retryable=False)
clock = dict(state="synchronized", utc_now_ns="1000000001", monotonic_now_ns="1",
             uncertainty_ns="100", sync_age_ns="0", leap="normal")
job = dict(job_id=jid, profile="rf-events/1", mode="tone", total_duration_ns="2",
           events=[dict(offset_ns="0", duration_ns="1", rf_on=True, frequency_nhz="1"),
                   dict(offset_ns="1", duration_ns="1", rf_on=False)])
caps = copy.deepcopy(vectors["schema_cases"][5]["message"])
status = response("STATUS", dict(boot_id=boot, state="complete", output_active=False,
                  owner_id=owner, job_id=jid, terminal_records=[dict(job_id=jid, state="complete",
                  ended_monotonic_ns="2000", output_active=False)]))
load = response("LOAD", dict(job_id=jid, state="loaded", adjustments=[dict(event_index=0,
                requested_frequency_nhz="1", realized_frequency_nhz="2")]))
failure = dict(type="response", protocol="WTP/1", session_id=sid, request_id=rid,
               op="ARM", ok=False, error=error)
requests = {
    "HELLO": dict(versions=["WTP/1", "WTP/2"], client_name="WsprryPi", client_version="1"),
    "CAPS": {}, "CLAIM": dict(owner_id=owner, lease_ms=5000),
    "RENEW": dict(owner_id=owner, lease_ms=60000), "RELEASE": {}, "LOAD": job,
    "ARM": dict(job_id=jid, start_utc_ns="2000000001", max_start_uncertainty_ns="100"),
    "ABORT": dict(job_id=jid), "STATUS": {}, "GET_CLOCK": {}, "PING": {},
}
responses = {
    "HELLO": dict(selected_version="WTP/1", device_id="4" * 32, boot_id=boot,
                  product="WsprryPico", firmware_version="test"),
    "CAPS": caps["body"], "CLAIM": dict(owner_id=owner, granted_lease_ms=5000, expires_monotonic_ns="5000000001"),
    "RENEW": dict(owner_id=owner, granted_lease_ms=60000, expires_monotonic_ns="60000000001"),
    "RELEASE": {}, "LOAD": load["body"],
    "ARM": dict(job_id=jid, state="armed", start_utc_ns="2000000001", start_monotonic_ns="1000000001", clock=clock),
    "ABORT": dict(job_id=jid, state="aborted", output_active=False),
    "STATUS": status["body"], "GET_CLOCK": clock, "PING": {},
}
events = {
    "JOB_STATE": dict(job_id=jid, state="running", output_active=True),
    "MISSED_START": dict(job_id=jid, state="missed", output_active=False, error=error),
    "OWNER_RELEASED": dict(owner_id=owner, reason="released", output_active=False),
    "DEVICE_FAULT": dict(state="failed", output_active=True, error=error),
    "INVALID_FRAME": dict(error=dict(code="INVALID_FRAME", message="bad frame", retryable=False)),
    "SESSION_REPLACED": dict(error=dict(code="SESSION_REPLACED", message="replaced", retryable=False)),
}
assert set(requests) == set(responses) == set(contract["operations"])
valid_messages = [request(k, v) for k, v in requests.items()]
valid_messages += [response(k, v) for k, v in responses.items()]
valid_messages += [event(k, v) for k, v in events.items()] + [failure]

# Every populated member in these minimal objects is required, including nested
# clocks, adjustments, terminal records and errors. Test missing/wrong/extra
# members independently of the C++ field tables and without a schema library.
def objects(value, path=()):
    if isinstance(value, dict):
        yield path, value
        for key, child in value.items():
            yield from objects(child, (*path, key))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from objects(child, (*path, index))


for message in valid_messages:
    name = message.get("op", message.get("event")) + " " + message["type"]
    add(name, message)
    for path, obj in objects(message):
        altered(name + f" extra {path}", message, (*path, "unexpected"), 1)
        for key, value in obj.items():
            # rfEvent frequency is conditionally required when rf_on is true.
            changed = copy.deepcopy(message)
            cursor = changed
            for part in path:
                cursor = cursor[part]
            del cursor[key]
            add(name + f" missing {path}/{key}", changed, False)
            altered(name + f" type {path}/{key}", message, (*path, key), {} if isinstance(value, list) else [])

for code in contract["errors"]:
    altered("error code " + code, failure, ("error", "code"), code, True)
for mode in contract["job"]["modes"]:
    altered("mode " + mode, request("LOAD", job), ("body", "mode"), mode, True)
for state in contract["states"]:
    add("status " + state, response("STATUS", dict(boot_id=boot, state=state, output_active=True,
        owner_id=None, job_id=None if state == "empty" else jid, terminal_records=[])))
    add("job event " + state, event("JOB_STATE", dict(job_id=None if state == "empty" else jid,
        state=state, output_active=True)))
for reason in ("released", "lease_expired", "terminal"):
    altered("owner reason " + reason, event("OWNER_RELEASED", events["OWNER_RELEASED"]), ("body", "reason"), reason, True)

for state in ("complete", "aborted", "missed", "failed"):
    for has_error in (False, True):
        for active in (False, True):
            terminal = dict(job_id=jid, state=state, ended_monotonic_ns="0", output_active=active)
            if has_error:
                terminal["error"] = error
            valid = (state != "complete" or not has_error) and (state not in ("missed", "failed") or has_error)
            altered(f"terminal {state}/{has_error}/{active}", status, ("body", "terminal_records"), [terminal], valid)
# Preserve explicit unsafe output evidence; terminal labels do not mean success.
altered("nine terminal records allowed", status, ("body", "terminal_records"), status["body"]["terminal_records"] * 9, True)
for state in contract["clock"]["states"]:
    for leap in contract["clock"]["leap_states"]:
        c = dict(clock, state=state, leap=leap)
        pending = leap.endswith("pending")
        if pending:
            c["leap_transition_utc_ns"] = "100"
        add("clock " + state + "/" + leap, response("GET_CLOCK", c))
        if pending:
            del c["leap_transition_utc_ns"]
        else:
            c["leap_transition_utc_ns"] = "100"
        add("inconsistent clock " + state + "/" + leap, response("GET_CLOCK", c), False)

arm = request("ARM", requests["ARM"])
for value in ("0", "18446744073709551615"):
    altered("u64 boundary " + value, arm, ("body", "start_utc_ns"), value, True)
for value in ("", "00", "01", "+1", "-1", "1.0", "1e3", " 1", "1 ", "1\n", "18446744073709551616", "9" * 100, 1, True):
    altered("bad decimal " + repr(value), arm, ("body", "start_utc_ns"), value)
for value in ("a" * 31, "a" * 33, "A" * 32, "g" * 32, "a" * 32 + "\n", None, 0):
    altered("bad id " + repr(value), arm, ("session_id",), value)
for value in (4999, 60001, True, "5000", -2147483649, 2147483648):
    altered("lease bound " + repr(value), request("CLAIM", requests["CLAIM"]), ("body", "lease_ms"), value)
for value in ([], ["WTP/1"] * 2, ["WTP/0"], ["WTP/01"], ["WTP/1\n"], ["WTP/1"] * 17):
    altered("versions " + repr(value), request("HELLO", requests["HELLO"]), ("body", "versions"), value)
for value in ("", "a" * 64, "é" * 32, "😀" * 16, 'a\x00\t\n"\\', "a" * 65, "é" * 33):
    add("ping bytes " + repr(value), request("PING", dict(token=value)), len(value.encode()) <= 64)
for allow in (True, False, None, 1, "false"):
    altered("adjustment policy " + repr(allow), request("LOAD", job), ("body", "allow_frequency_adjustment"), allow, type(allow) is bool)
for count in (0, 1, 512, 513):
    j = dict(job, total_duration_ns=str(count), events=[dict(offset_ns=str(i), duration_ns="1", rf_on=False) for i in range(count)])
    add("event count " + str(count), request("LOAD", j), 1 <= count <= 512)
for duration in ("86400000000000", "86400000000001", "18446744073709551615", "0"):
    add("job duration " + duration, request("LOAD", dict(job, total_duration_ns=duration,
        events=[dict(offset_ns="0", duration_ns=duration, rf_on=False)])), duration == "86400000000000")
for path, value in ((('events', 0, 'offset_ns'), "1"), (('events', 1, 'offset_ns'), "0"),
                    (('events', 0, 'duration_ns'), "18446744073709551615"),
                    (('events', 1, 'frequency_nhz'), "1"), (('events', 0, 'frequency_nhz'), "0")):
    altered("bad timeline " + str(path), request("LOAD", job), ("body", *path), value)
for path, value in (("max_events", 0), ("max_events", 513), ("max_payload_bytes", 65535),
                    ("output_disable_timeout_ns", "0"), ("output_disable_timeout_ns", "5000000001"),
                    ("maximum_arm_ahead_ns", "604800000000001"), ("max_job_duration_ns", "86400000000001"),
                    ("response_cache_entries", 7), ("terminal_record_ttl_seconds", 3599),
                    ("modes", ["wspr", "wspr"]), ("profiles", ["rf-events/1", "rf-events/1"]),
                    ("frequency_ranges", caps["body"]["frequency_ranges"] * 33)):
    altered("caps bound " + path, caps, ("body", path), value)
altered("active abort acknowledgment", response("ABORT", responses["ABORT"]), ("body", "output_active"), True)
altered("active missed-start event", event("MISSED_START", events["MISSED_START"]), ("body", "output_active"), True)

base = json.dumps(request("STATUS", {}), separators=(",", ":")).encode()
add("maximum payload", base + b" " * (65536 - len(base)))
add("over maximum payload", base + b" " * (65537 - len(base)), False)
for prefix, suffix in ((b"\xef\xbb\xbf", b""), (b"", b"{}"), (b"", b"\0"), (b"", b",")):
    add("bad JSON boundary " + repr((prefix, suffix)), prefix + base + suffix, False)
add("decoded duplicate keys", base[:-1] + b',"sess\\u0069on_id":"' + sid.encode() + b'"}', False)
add("escaped unique key", base.replace(b'"type"', b'"ty\\u0070e"'))
for raw in (b'"\xc0\xaf"', b'"\xed\xa0\x80"', b'"\xf4\x90\x80\x80"', b'"\x80"',
            b'"\xe2\x82"', b'"\\ud800"', b'"\\udc00"', b'"\x01"', b'"\\x00"'):
    ping_raw = json.dumps(request("PING", dict(token="VALUE")), separators=(",", ":")).encode()
    add("invalid UTF8/string " + repr(raw), ping_raw.replace(b'"VALUE"', raw), False)
for raw in ("1.0", "1e0", "NaN", "Infinity", "2147483648", "-2147483649", "+1", "01"):
    text = json.dumps(dict(failure, error=dict(error, detail=dict(value="VALUE"))), separators=(",", ":"))
    add("invalid detail scalar " + raw, text.replace('"VALUE"', raw), False)
for depth in (16, 17):
    # Envelope, error and detail each contribute one container level.
    nested = 0
    for _ in range(depth - 3):
        nested = dict(x=nested)
    add("depth " + str(depth), dict(failure, error=dict(error, detail=dict(x=nested))), depth == 16)

result = subprocess.run([str(Path(sys.argv[1]).resolve())], input="\n".join(raw.hex() for _, raw, _ in cases) + "\n",
                        text=True, capture_output=True, timeout=60, check=True)
lines = result.stdout.splitlines()
assert len(lines) == len(cases), (len(lines), len(cases), result.stderr)
for (name, raw, valid), line in zip(cases, lines):
    assert (line != "invalid") == valid, (name, valid, line, raw[:500])
    if not valid:
        continue
    source = json.loads(raw)
    expected_type = "error" if source.get("ok") is False else source["type"]
    assert line.split()[0] == expected_type, (name, line)
    if source["type"] == "request":
        assert json.loads(bytes.fromhex(line.split()[1])) == source, (name, "request round trip changed semantics")
frames = vectors["framing_cases"]
result = subprocess.run([str(Path(sys.argv[1]).resolve()), "--encode-frame"],
                        input="\n".join(f["payload_utf8"].encode().hex() for f in frames) + "\n",
                        text=True, capture_output=True, timeout=10, check=True)
assert result.stdout.splitlines() == [f["frame_hex"] for f in frames], result.stdout
print(f"WTP vectors and independent codec cases passed: {len(cases)}; framing vectors: {len(frames)}; pinned snapshots: 3")
