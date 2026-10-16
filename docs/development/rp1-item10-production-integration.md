<!-- SPDX-License-Identifier: MIT -->

# Item 10: published RP1 production integration

Status: **complete within the maintainer-accepted installation/runtime scope**
on October 16, 2026, including installed supplemental qualification reporting.
Existing documented RF qualification retains its recorded scope. Target rollback
rehearsals are explicitly excluded. No new RF operation or qualification is
claimed. WsprryPi release items 11–14 remain separate, open gates.

This is the current acceptance disposition. Commands, generated results, private
target captures and campaign evidence stay outside Git. The WsprryPi release
punchlist owns application sequencing; the provider's current ROADMAP describes
maintained capabilities and procedures rather than numbered historical phases.

## Accepted identities

| Input | Accepted identity |
| --- | --- |
| Frozen provider software | `v1.0.0`, commit `8c16d3c237cb1267cbedafb5da2bb74132ca5de0`, tree `4c1ad8acf513b36ba32e58a636942655accbb52d` |
| Annotated software tag | `52c5d42cf6e3f9c8a77fe9a03e1f012c3e8304a0` |
| Provider versions | Product/module/controller/DKMS `1.0.0`; Debian package `1.0.0-1` |
| Separate supplement | `qualification-v1.0.0-r1`, source `bb6815446beb9b96853ad5a353b0d355d4975710`, tree `b659a6e27ac07e77dff6f0ec89490afbe9e0ceb4` |
| Installed application binary | Clean consumer `8d5247a740a70df985dde920cad2775555d92311`; SHA-256 `c8bf65a9e1d19398b01accc01aba00dd37777440995c96dcd85b85bb11c0758b` |
| Installed qualification helper | Consumer repair `aae72f1fa1f98b3a21d424c7e57b15044aff2f92`; SHA-256 `41c5cd1a1846e188f37e5de4de5db65dc44076aa43e3657ee33ffa30bf865d8b` |
| Target | `wspr5`, Pi 5 Model B Rev 1.0, Debian 13 ARM64, running kernel `6.18.34+rpt-rpi-2712` |

The original [seven-asset software lock](../../scripts/tests/fixtures/rp1-release-1.0.0.json)
remains unchanged. The [separate supplement lock](../../release_tools/rp1-qualification-lock.json)
binds all four metadata assets and their source/revision. Later provider
maintenance source is neither the released software nor a replacement package.
Documentation-only closeout commits do not change the deployed binary identity.

## Acceptance matrix

| Boundary | Disposition and applicability |
| --- | --- |
| Software publication and production selection | PASS: immutable `v1.0.0` and its exact seven assets independently verified; actual production resolver accepted. `v1.0.0` remains latest software; supplemental tags cannot be selected as module releases. |
| Supplemental publication and resolution | PASS: immutable exact tag/source/tree, complete four-asset inventory and digests verified through the maintained resolver; original software identity retained. |
| Installation policy | PASS: automatic provider installation on positively identified Pi 5/CM5 remains the maintainer's selected policy, with explicit opt-out. Other/unknown platforms remain refusals or skips. |
| Migration, package ownership and lifecycle | PASS within accepted scope: prior cleanup-first development-to-release migration and normal target installation retained. The unchanged package's 22 disposable lifecycle scenarios cover reinstall/interruption/inverse cases; no claim of new target rehearsals. Foreign, mixed, stale and changed ownership refusal coverage retained. |
| Startup and route administration | PASS: prior independent GPIO4/GPIO20 selection, recovery, removal/reselection and two automatic reboot restorations retained. Bound provider, companion and startup inputs remain equal, so these checks were not repeated for metadata reporting. |
| Updated installed consumer | PASS: normal application installer completed with provider installation explicitly skipped. Binary, UI, collector, helper, lock and ownership receipt matched their source inputs. Separate supported cache refresh succeeded and every installed asset matched the pinned digest. |
| GPIO4 installed evidence | PASS independently: fresh digest-bound idle selection, exact released provider/build attribution, scoped TONE evidence verified, structural `Compatible-unqualified`, readiness true, owner/lease/endpoint absent, clock/GPIO/DMA quiescent. Asking for GPIO20 refused attribution. |
| GPIO20 installed evidence | PASS independently: the same installed checks after fresh idle restoration; asking for GPIO4 refused attribution. Final route remains GPIO20. |
| Negative evidence cases | PASS in isolated external snapshots: missing, corrupt, stale, wrong-route, wrong-source, wrong-module and incomplete-artifact cases do not produce false qualification or erase hardware readiness/application policy. The authoritative installed cache was not corrupted. |
| Application status, UI and support | PASS: actual detailed API and retained diagnostic DOM report scoped evidence and independent application eligibility. Visible compact route controls reviewed at desktop/mobile in light/dark themes. The installed support collector includes verified evidence; no upload or active I2C scan. |
| Control-path responsiveness | PASS software coverage: ordinary scheduler/route queries exclude supplemental inspection; detailed inspection runs outside configuration/route locks and is bounded. Existing STOP/cleanup acceptance is reused; no new live STOP or RF test is claimed. |
| Preservation and final state | PASS: provider package/binding/loaded module identities, boot configuration and managed startup receipt retained. Parsed INI values are unchanged after final GPIO20 restoration. Application/Apache/chrony/gpsd are active, startup worker successful, transmission disabled and boot transmission Never. |

The installed runtime binding remains
`232fc9be5119e3fbe5263b480c32e39e477d1d595545588e1e81a29601367e3f`.
Both canonical UAPIs, per-route overlays, complete bound artifact inventory and
both loaded module build notes match the supplement. Installed XZ module hashes
remain `21b1c019b267a0939dfd48ee2d92b646163c8de6d62822e816eb3944f4c82843`
(consumer) and `26655f915f9946777b675a80994b438c675fdbbbe567ffc7ffb8b64db6f8d768`
(controller). Acceptance applies to this running kernel and measured target;
it does not qualify every Pi 5/CM5 or installed kernel.

## Qualification and reuse boundaries

The original compatibility manifest remains `defaultState=Unavailable` with
`entries=[]`, as accepted by the maintainer. It is the original release artifact,
not the current complete evidence record. The separately published supplement
now attributes seven accepted finite conducted TONE cases to each route at
14,097,100 Hz, 2 mA and nominal 200 MHz `pll_sys`. It preserves the original
lifecycle/receiver scope and candidate-versus-release provenance. Relevant
executable/build inputs and extracted Debian package members match; archive
and source identities remain distinct. No provider rebuild/reinstallation or
new transmission/SDR capture was needed for this reporting closeout.

The module's structural `Compatible-unqualified` state can coexist with verified
external evidence. Missing evidence means `Evidence missing`; invalid or stale
bytes mean `Evidence invalid`. Neither is a hardware availability verdict.
Detailed evidence includes its own current readiness/output/ownership snapshot;
route-manager fields that were not inspected remain explicitly unknown or
conservative and must not be interpreted as measured endpoint facts.

The maintained [application band table](https://wsprry-pi.readthedocs.io/en/devel/About_Wsprry_Pi/index.html#band-qualification)
retains separate accepted authority: Pi 5 / 200 MHz PLL_SYS qualifies 2200 m
through 10 m including 12 m; 6 m and 4 m remain unqualified; 2 m, 1.25 m and
70 cm remain Unavailable. Experimental overrides cannot enable Unavailable
combinations. No provider band/mode gate, enrollment or transmission permission
was introduced. The supplement's finite TONE scope does not replace or broaden
that application statement.

## Review and validation disposition

Installed verification exposed one P2 consumer defect: isolated Python
startup prevented the released provider from importing its installed siblings.
The consumer repair retains that root-owned script directory while ignoring
Python environment and user-site injection. A real-subprocess regression covers
sibling resolution, ignored `PYTHONPATH` and bytecode suppression. The corrected
helper was installed through its owning receipt-based workflow and both route
checks repeated successfully. The frozen provider was not modified.

Separate adversarial assessment challenged installed/source identities, stale
helpers/caches, refresh success independent of installer success, substitutions,
route/build attribution, release selection, missing-evidence semantics, control
path locking, unchanged high-band refusal, final state and evidence reuse.
No actionable finding remains within item 10. Focused diagnostics, qualification,
release-selection, installer and UI regressions pass; Linux hosted CI covers
route/runtime/transmitter and support-installer tests. Consumer baseline
[8d5247a CI](https://github.com/WsprryPi/WsprryPi/actions/runs/38911212968)
and [aae72f1 repair CI](https://github.com/WsprryPi/WsprryPi/actions/runs/38920583987)
each passed all 11 jobs; [supplement CI](https://github.com/WsprryPi/RP1-GPCLK-DKMS/actions/runs/38909156788)
passed both jobs. The final pushed documentation closeout requires its own
exact-commit CI and parity verification in the execution handback.

No local macOS C++ compilation ran. The support-installer shell fixture requires
GNU `stat`, so its macOS invocation stopped on that platform dependency; the
passing Linux CI supplies that check. No new reboot, rollback rehearsal, active
I2C scan, transmission or RF/receiver capture occurred during this supplement
closeout. Prior accepted target and RF results retain their original identities.

## Documentation Impact

Updated here: this current acceptance disposition, the release punchlist and
[qualification/cache procedure](../rp1-qualification-metadata.md). Installation
and provider-acceptance guidance point to current installed acceptance and the
separate supplement. Runtime route procedures and licensing were reviewed;
provider contracts, original locks, MIT attribution and application policy are
unchanged. No component or UI source modification was required.

Item 12 must update `WsprryPi-Docs/docs/Advanced_Operations/rp1_gpclk.md`,
`docs/User_Interface/Setup/Transmitter/index.md`, `docs/Install/index.md` and
`docs/Experimental/rp1_gpio.md`: explain the cache/refresh and evidence-status
meanings, structural compatibility versus external qualification, the installed
consumer diagnostic command, the unchanged packaged `1.0.0-1` diagnostic's
limitations, and neutral/saved-route/migration/recovery behavior. Preserve
`docs/About_Wsprry_Pi/index.md` band dispositions and their recorded scope.
That sibling repository was inspected read-only; its changes and rendered
review remain separately authorized items 11–12.

Read-only issue inspection confirms #412 retains WsprryPi's final publication
closeout; #423's publication-only tracking exception does not waive outstanding
release-scoped documentation and integrated UI prerequisites. Both issues remain
open and were not edited. Item 10 completion does not close items 11–14 or
publish/freeze WsprryPi 4.0.
