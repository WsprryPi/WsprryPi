# Issue 473 integrated requirement-to-evidence matrix

Status: accepted 2026-11-18. All named current-source checks and documentation review pass; exact commands, return codes, candidate identity and hashes are retained in the [acceptance record](phase-8-acceptance.json). Historical design approval alone is not test evidence. See [closeout progress](phase-8-progress.md), the [authoritative plan](../device-owned-solar-scheduling-plan.md), [D01–D12](phase-1-decisions.md) and [phase-6.2 amendments](phase-6-2-decisions.md).

## Candidate and evidence identity

Pi `7de3c3019376594c447c4b35124f2a4dfe062edd` and Docs `12873e228b61b86b0c68a3bc08fea830a925ef1f` are on `codex/issue-473-device-owned-scheduling`; companion Pico `ad697eaf982f35f554ffd319694837e901a32205` is on `codex/issue-473-phase62-paired-candidate`. These are feature-branch candidates, not delivery to devel/main. Phase 8 initially changes engineering acceptance records and four existing operator guides only. The final acceptance record must identify any additional changes and final document hashes.

Current-source checks are rerun against a frozen pair. Phase-7 input hashes are verified against the committed candidate, including its new Scheduling editor and autonomy harness. All Pico non-document inputs and Pi production C++ inputs remain byte-identical to the accepted phase-6.2 inputs; the only latter metadata delta is committed Pico provenance. Provider/platform qualification is reused only for byte-identical provider/build/packaging inputs, with an explicit comparison; UI and integrated runtime evidence are refreshed. No old whole-application result is silently relabeled as a new candidate run.

## Approved decisions and product contracts

Paths beginning `src/` and `WsprryPi-UI/` name Pi. “Docs” names the authorized operator repository. Test names are existing Makefile targets unless a command/path is shown. The final machine-readable acceptance record supplies exact commands, return codes and source/log hashes.

| Requirement | Current implementation and boundary | Current evidence required | Operator/engineering coverage |
| --- | --- | --- | --- |
| D01: independent edit context | `setup-targets.js`, target editors, `pico_target_controller`, `pi_pair_remote_controller`; selection has no backend/claim/save side effect | UI units/browser; actual two-Pico and paired-Pi browsers; `pico-target-controller-test` | Docs Setup, Transmitter, Device I/O |
| D01: explicit zero-output controller | `controller_runtime.cpp`, `main.cpp`; no failed-hardware fallback or fabricated station/output | `controller-only-test`, actual protected controller fixture; startup negatives | Docs Setup/controller-only and runtime |
| D02: automatic operator workflow | Pair/Replace/Remove in Maintenance; device-owned keys, no file/key/certificate handling | `pi-pair-local-test`, `pi-pair-connect-test`, actual paired browser | Docs Maintenance Controller access; pairing engineering contract |
| D02: peer authentication and purpose separation | PAKE-only Botan; OpenSSL TLS 1.3 PSK-DHE for Pi, existing Pico Mbed TLS; retirement ALPN cannot manage | crypto/TLS/server/history-TLS/admission/machine-service tests; complete actual carriers | Provider qualification, Pair/1 definitions, threat model |
| D02: lifecycle and replacement | Durable authorization generations, revocation, automatic renewal/rotation and exact interrupted rotation recovery | records/recovery/outgoing-rotation/usage/owner/service-lifetime/runtime tests | Docs Maintenance; normative pairing/rotation |
| D03: closed current schemas | Authoritative Pico control policy/schema and generated Pi C++/JS; exact current major 1 | generator parity, controller-contract, pair-contract, actual network/USB negatives | Owning protocol docs; prerelease lifecycle unchanged |
| D03: atomic complete settings | `pi_management_service`, managed store/records and bounded WAL; private settings preserved | service/store/records/persistence-coordinator/journal tests, actual settings interop | Docs explicit paired save, runtime, repair/recovery |
| D03: lost reply and durable readback | Original operation IDs and target binding; exact receipts, no blind mutation retry | journal/history/peers tests; actual paired Pi/Pico browsers; uncertainty UI regressions | Docs Setup/Scheduling recovery |
| D04: independent S/P/T/boot/authority | `native_recurrence`, scheduling configuration, target management enable/disable/Stop | native recurrence and actual-main tests; authority/service tests; direct invalid API refusal | Docs runtime, Operations, first transmission |
| D04: disable drains, Stop separate | No new recurrence admission after disable; complete accepted work may finish; Stop uses observed output | native recurrence, actual paired browser and lifecycle checks | Docs Scheduling/Operations/Setup Stop guidance |
| D05: durable reservation and at-most-once admission | `recurrence_progress`, native recurrence; reserve before launch and conservative crash consumption | recurrence-progress, native recurrence, actual-main crash/restart tests | Docs runtime/recovery; no exactly-once RF claim |
| D05: list rotation and cursor isolation | Independent explicit-period lists, shared inherited progression, refusal/skip/pair rules | native-recurrence-test, recurrence-progress-test, placement and selector regressions | Docs Scheduling inheritance and engineering state policy |
| D06: deterministic geometry | `solar_geometry`, attributed Pico geometric center convention; day −0.833°, gray lower inclusive/upper exclusive | solar-geometry-test and actual Pico geometry pair; poles/hemispheres/date/leap/invalid vectors | Docs Scheduling elevation and runtime |
| D06: target site and UTC | Target saved locator center, read-only Linux clock discipline; no controller/browser timezone input | solar-clock, solar-policy, native recurrence, two-target autonomy | Docs target locator/time holds and Mac limitation |
| D06: no implicit provider fallback | Native Pi GPS unavailable; Pico existing receiver freshness remains target-owned | policy/provider refusals, strict carrier config vectors | Docs Scheduling/Pico capability limits |
| D07: retirement before new authority | `recurrence_authority`, controller retirement, Fleet durable retirement book | authority-record, managed service/store, Fleet and machine-service tests | Docs Maintenance Scheduling ownership, Fleet/runtime |
| D07: unknown/offline stays pending | Target-owned authority ledger, no empty-browser proof, uncertain output barrier | machine-service/history/resume-refusal tests; actual retirement integration | Docs pending migration and explicit recovery |
| D07: automatic clean initialization | Trusted installation provenance consumed by local protected confirmation; wipes/unknown history blocked | managed startup/store/main; authority initialization and repair negatives | Docs Maintenance Set up this transmitter |
| D07: safe rollback | Disabled/reconciled target, durable one-use currently paired old-controller grant; original verifier for old-scope unknown outcome | machine-service/resume-refusal/recovery; actual retirement/restore integration | Docs Maintenance previous schedule recovery |
| D07: unchanged ordinary WTP | Transmit-off claims remain independent; no permanent fence against arbitrary new Plain-LAN principals | network runtime/interop, WTP binding, Fleet and managed-output tests | Docs Setup transport distinction; WTP review backlogged |
| D08: faithful Pico capabilities | Existing typed WSPR/gray-line settings; no Pi profiles, message master or controller sunrise writes | actual Pico browser, strict descriptors, complete network and USB suites | Docs Scheduling and Pico settings boundaries |
| D09: defaults and retained values | Missing new config preserves scheduling=true/solar=false; no automatic Fleet conversion; unsupported saved values cannot admit | managed main/store and native recurrence; full round trips/failed writes | Docs runtime defaults and troubleshooting |
| D10: save-disabled/readback/enable | `scheduling-editor.js`, `pi-target-client.js`; desired choice separate from confirmed state, digest-bound enable | scheduling client/browser; actual paired native settings/enable, dropped replies | Docs Scheduling procedure, first transmission |
| D10: existing autosave preserved | Only This Pi ordinary form autosaves; paired drafts require explicit save | UI context/browser isolation and actual Pi browser | Docs WSPR/CW/Device I/O and Setup |
| D10: drafts/context retirement | Bounded target caches, immutable selection epochs, retained unavailable entries; no stale response enables | held A→B→A, withdrawal, busy return, 64-entry Device churn, exact receipt recovery browser regressions | Docs retained drafts, capacity and unknown review |
| D11: strict limits and secret handling | Bounded parser/header/messages, profiles/connections/operations/forecasts; no credentials in UI/log/support export | contract/TLS/window/discovery/store tests; secret-redaction and provider-boundary scripts | Normative bounds, provider and support docs |
| D11: SSRF/origin/CSRF guard | Protected browser principal; pinned private-address endpoints, no redirect/proxy/path fallback | local-browser-principal, guarded-http, bootstrap-client, privileged-network policy/runtime; actual HTTP pair | Docs network safety; trusted-admin browser model |
| D11: bounded wake/write/search | Native scheduler timer/generation fences; bounded forecast and progress writes | native recurrence/progress and clock tests; no poll-time durable write | Engineering D11/status contract and Docs unknown forecasts |
| D12: full candidate before writes | Active and inactive periods and derived CW tones validate; one snapshot freezes complete pair policy/intent | solar-policy, native recurrence, managed service/store, direct API invalid inactive policy | Docs inactive fields validate; full precision/units |
| D12: commitment/launch fencing | Config/location/clock generations checked before reservation/launch; missed canonical slot refused | native recurrence, scheduled-launch-test and wspr-plan-placement-test | Launch engineering contract; no timing/RF qualification |

## Phase-6.2 closure amendments

| Approval | Required integrated evidence and limit |
| --- | --- |
| D62-01 | Exact Botan pin/license/source boundary and maintenance/package evidence remains input-identical; fresh profile crypto/TLS tests. BoringSSL remains unadopted. |
| D62-02 | Direct USB checks and automatic attached-Pico management over authenticated Pi, actual USB gateway suite; no extra grant or WTP forwarding. Existing Pico IP login retained. |
| D62-03 | Assignment digest is retirement identity; ETag only concurrency. Fleet/authority/machine-service negatives. |
| D62-04 | One-use current-pair rollback grant and disabled unknown state; machine-service/resume/refusal and retirement integration. |
| D62-05 | Target-owned retained authority history, strictly retirement-only callback credentials; history-TLS and purpose-negative tests. |
| P62-01, P62-08, P62-10 | Redacted host pairing status, explicit window action, automatic renewal indication; exact schema parity, HTTP/window/usage/current-context tests. |
| P62-02, P62-03, P62-04 | Durable temporary rotation intent and exact canonical transcript/retirement HKDF contexts; independent public vectors and crypto/rotation/retirement-key tests. |
| P62-05, P62-09 | Private bounded peers image and uncertain-pair recovery; peers/store/recovery tests with no key eviction on uncertainty. |
| P62-06, P62-07 | Bounded `_wsprry-mgmt._tcp.local.` observation interface; discovery/publisher/private-address tests. Discovery grants no authority; physical LAN discovery unqualified. |
| P62-11 | Host pairing receipts use dedicated local binding, including controller-only removal after lost reply; local/runtime/actual Maintenance browser. |
| P62-12 | Distinct parent management binding for attached Pico, no borrowed transmitter context; USB paired-Pi gateway suite. |
| P62-13, P62-14 | Bounded remote journal enrollment and explicit authenticated readback/context retirement review; journal/remote actual HTTP and browser recovery. Historical outcome stays unknown. |
| P62-15 | Fresh current same-controller pairing plus original-grant proof for unknown resume recovery; actual machine-service positive/negative tests. |
| P62-16 | Authenticated WTP generation/ports refresh only verified old assignment connection fields during handback; retirement/restore pair and WTP-binding tests. RF settings stay frozen. |

## Complete state and phase coverage

| Gate | Evidence and expected boundary |
| --- | --- |
| Phase 1 | All D01–D12 and subsequent exact amendments approved; no new unresolved behavior choice introduced by closeout. |
| Phase 2 | Local/Pico APIs, controller zero-output mode, startup readiness and protected INI recovery; fresh foundations, current actual controller and both carriers. |
| Phase 3 | Shared Device selector outside form/tablist; editing independent of output, drafts and late replies; full UI and actual two-Pico browser. |
| Phase 4 | Geometry, precision, complete config lifecycle, atomic failed-write/refusal and feature-off parity; focused solar/store/service/actual-main checks. |
| Phase 5 | Target-owned initialization, legacy retirement, offline/unknown pending, safe restore; fresh authority/Fleet/machine integration. |
| Phase 6.1 | Target-local recurrence, canonical start, launch generations, durability/no catch-up and bounded work; native/clock/progress/launch plus actual-main. |
| Phase 6.2 | Provider, purpose-authenticated management, automatic pairing lifecycle, gateway and retirement/recovery; exact input comparison plus fresh complete security/carrier/actual browser checks. |
| Phase 7 | Shared five-tab UI, explicit enable semantics, capability honesty, two real target owners while controller/browser absent; current browser, visual and autonomy proof. |
| Phase 8 | This complete matrix, current-source validation, reconciled Docs, repeated adversarial assessment, feature delivery and exact-head enabled-CI observation. |
| Ordered admission table | Startup/readiness, authority, S-off, T-off, external ownership/fault, solar-off, missing UTC/site, policy skip, eligible complete intent; native recurrence/service/main tests. No prior failed condition falls through. |
| Boot Cartesian product | Never/Follow/Always affect T only; S/P unchanged; startup inhibition wins; disabled work cannot resurrect; managed main/native recurrence tests. |
| Event table | Enable, disable, Stop, solar save, T change, reconnect/exit, config/site/clock change, restart: distinct durable and in-flight effects; store/service/native and actual browser/autonomy. |
| Threat model | Hostile discovery/DNS/browser/profile/response, stale or revoked controller, SSRF, leaked secrets and exhaustion tested at owning boundary. Trusted OS/admin and trusted local browser access remain assumptions, not adversary resistance. |
| Autonomy | Separate configs/journals/clocks/sites, real production-main recurrence and simulator events during absence, no replay on reconnect; typed identity/health/lease fixtures disclosed. No cached forecast substituted for execution. |
| UI/accessibility | Desktop/mobile, light/dark, keyboard/focus, readable disabled/errors, invalid raw drafts, capacity, uncertainty and capability holds; current screenshots and independent Impeccable full assessment. |
| Documentation | Strict warning-free Sphinx build, local links/fragments, matching target units/defaults/state and actual page renders; existing images reviewed for material accuracy. |

## Qualification limits and explicitly retained backlog

This matrix qualifies hardware-free software behavior only. Mac uses the explicit portable semantics profile; full Linux semantics and the named selector/repeat regressions additionally run with hardware access disabled. Neither profile qualifies physical operation. Physical USB cable behavior, target clock precision/jitter, GPIO/I²C/MMIO, actual RF, installation, installed services and deployment are not performed or claimed. Existing typed simulator and fake-command fixtures do not grant live-device authority.

Native Pi solar clock health is supported through Linux OS discipline; production Mac solar health remains unknown. Ordinary solar-off scheduling remains available. Native Pi GPS ingestion, Pico Pi-style day/night lists and a combined atomic Pico schedule master are unsupported, with no controller emulation. Browser HTTP and Plain-LAN WTP are not made confidential by Pi pairing; the WTP role/TLS and browser-hop HTTPS/WSS reviews remain in TODO.md. No release readiness, devel/main delivery, PR/merge or issue closure is implied.
