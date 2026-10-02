# Adversarial review and reassessment

Review the actual captures, configuration hashes and final direct WTP
observations independently of harness success markers. No application source
was changed in this testing request.

## Findings and disposition

| Finding | Assessment and disposition |
| --- | --- |
| Initial identification omitted Origin | HTTP 403 was correct. Harness now supplies same-origin JSON intent headers. Accepted identification was repeated with wspr2. Closed as a harness defect. |
| Original expiry assertion expected HTTP 400 | Actual candidate rejection is HTTP 409. Harness records the response before asserting it. Full ordinary 120-second expiry was repeated and passed. Closed as a harness defect. |
| Analyzer expected `stats.dropped` | Recorded `tc` JSON uses `stats.drops`. Corrected against the retained data and reran all assertions. Closed as an analysis defect. |
| Ethernet recovery was judged by publication alone | Packet tracing found WTP replies leaving WiFi after equal-metric route reordering. Original route preference was restored; negotiation succeeded. The automatic transport-recovery defect remains open. |
| wspr4 stopped serving its local API | Correlated local timeouts and backtrace identify Si5351 inventory under the configuration mutex. Reproduced after a restart. Replaced it with healthy wspr2 for acceptance; restarted again to drain the blocked request. Root cause remains open. |
| Could removal actually be a goodbye or test reset? | Accepted expiry interval had both links and native Avahi running. No relevant TTL-zero response was captured. Actual TTL 120, per-interface native removal times, dropped-packet counters, and independent healthy Pico observation agree. No shortened TTL or fixture record. Closed for the tested expiry case. |
| Packet capture still sees suppressed announcements | AF_PACKET sees ingress packets before traffic-control drops. All recorded actions match source `192.168.1.47`, IPv4 UDP source port 5353 and drop; counters are nonzero on both interfaces. The API removes both candidates while the independent observer retains Pico A. Capture alone was not used to assert delivery. Closed. |
| A different protocol/interface candidate could survive | The two expired records are interfaces 2/4, protocol IPv4. None of the seven remaining online records has Pico A's target. Both are unavailable for identification and recover afterwards. Closed for the observed IPv4 topology. |
| Address change could preserve stale identity or a cached old address | wspr2's wire capture includes the new endpoint-specific A record for `192.168.1.62`. The Pi listener is bound only to that address during identification; full expected device/boot IDs match. Identification repeats after restoring `.54`; catalog hashes are unchanged. Closed for the accepted DHCP case. |
| Could the test enable transmission or modify saved assignments? | Executed operations were network management and read-only identification/STATUS. All controller samples show local off, remote output off and no owner; captured Fleet samples remain paused. Persistent hashes/watermarks are identical and five final endpoints report empty/unowned/inactive. Pico B was not contacted. Closed within observed endpoints. |
| Restoration could leave a changed route/MAC/profile or filter | Exact persistent hashes, original global IPv4 addresses and Ethernet MAC, normalized original routes and original first default route, qdisc snapshots, and zero test units are asserted. Temporary clone absent. Backup is 0600 and was excluded from evidence transfer. Closed for wspr5. |
| Source/test evidence could overclaim provenance | Installed binary is identified by actual SHA-256. Linux test uses a source archive of base `9609143`; its generated metadata warning is disclosed. Intermediate harness revisions were not individually hashed. No full semantics, CI, RF, browser, IPv6 or 4500-second Pi expiry claim. Closed with explicit limits. |

## Reassessment

The repaired harness and captured-data assertions pass. Four DNS-SD acceptance
areas are exercised with real LAN interfaces, native Avahi and an ordinary
Pico advertisement. Native cache removal follows the advertised lifetime and
unavailable candidates are rejected. Restoration evidence is complete for
wspr5 and saved Fleet state.

**Two operational issues remain open:** wrong-interface WTP replies after
route-order changes, and wspr4's blocking Si5351 inventory/configuration read.
The final assessment does not declare full multihomed WTP operation or the
wspr4 configuration path closed. Both need focused implementation and live
regression testing. The Mac SDK/linker mismatch also prevented the optional
local unit executable; the exact focused unit passed on Linux.

## Follow-up, 2026-10-02

The [Si5351 isolation implementation and live regression](../si5351-inventory-isolation-report.md)
closed the configuration stall. Its [final adversarial reassessment](../si5351-inventory-isolation-review.md)
has no unresolved inventory-isolation finding. The subsequent
[selected-interface reply repair and live acceptance](../wtp-reply-routing-report.md)
also closes wrong-interface WTP reply routing. Both follow-up TODOs are closed;
Si5351 hardware nonresponse is outside this testing scope. The original campaign observations above
remain the evidence for the defects as first encountered.
