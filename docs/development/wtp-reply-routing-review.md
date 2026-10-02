# Adversarial review: selected-interface WTP replies

## First assessment and repairs

| Finding | Repair / verification |
| --- | --- |
| Binding only the local IP leaves the host's preferred route free to send replies through another NIC. | Pass the selected interface from the runtime and set Linux `SO_BINDTODEVICE` before `bind` and `listen`. The private-namespace baseline misroutes through WiFi; the corrected listener uses Ethernet under identical routes. |
| Binding after `accept` would leave SYN-ACK routing incorrect. | Bind the listener before it accepts connections; packet evidence covers handshake and payload, and the unit test reads the inherited binding on accepted sockets. |
| Permission/unsupported binding errors could silently leave an unrestricted listener. | Reject startup and close the candidate socket; preserve the error and zero port. Injected `EACCES`/`ENOPROTOOPT`, a missing interface, retry and descriptor checks cover this. Publication occurs only after successful listener startup. |
| Linux truncates overly long interface names; an embedded NUL can silently choose a different name. | Reject both cases before creating the socket. Dedicated assertions verify refusal. |
| A device recreated with the same name/address has a different kernel index; its old bound socket becomes stale. | Track the index in selected station identity and withdraw/rebind when it changes. Selection tests verify identity preservation and conflicting observations fail closed. |
| A route change can invalidate an established socket's cached route. | Namespace acceptance changes equal-metric route order after HELLO, then requires STATUS over that connection and HELLO/STATUS over a new one. Both retain Ethernet replies and boot identity. |
| A privileged acceptance harness could disturb host routes or leave resources behind. | Three private namespaces contain all synthetic links/routes/sysctls. Cleanup runs on failure; the first route-setup failure left no namespace or fixture process. Live changes have private backup and independent rollback. |
| Buffered capture startup lines could strand readiness in userspace. | Capture stderr is unbuffered and startup is bounded. Equal-metric duplicate routes use `append`; command failures include diagnostics. |

No reusable component or UI source is modified. The inherited WTP principal,
authority, leases, bounded clients, session replacement and job engine remain
unchanged. Tests use an inhibited RF engine; the baseline bypass is link-time
fixture code and cannot be selected in the application.

Reference: the Linux [socket option contract](https://man7.org/linux/man-pages/man7/socket.7.html)
defines named-interface binding; [Linux 6.18 socket implementation](https://raw.githubusercontent.com/torvalds/linux/v6.18/net/core/sock.c)
stores its kernel index and resets the socket route cache. Packet and accepted-socket
tests establish the behavior used here on the tested kernel.

## Final adversarial reassessment

Focused Linux listener and interface tests passed. The namespace case reproduced
WiFi replies in the baseline and exclusively Ethernet replies after the repair,
including established and new WTP connections. Live wspr5's original listener
reproduced a HELLO timeout after reconnect: requests arrived on Ethernet while
SYN-ACK and payload replies left on WiFi.

After the index-identity repair, focused listener/interface tests, full Linux
semantics, WTP application integration and the release build passed. The namespace
case passed again. Live wspr5 passed nine HELLO/STATUS inspections from wspr4:
three immediately after deployment and three after each of two Ethernet reconnects.
Both reconnects withdrew/recovered the listener and DNS-SD. Ordinary routes still
preferred WiFi, while every captured handshake/payload reply used Ethernet.
Device ID and post-deployment boot ID remained stable across reconnects.

Original route ordering, address identity, configuration/Fleet/catalog/profile
hashes and paused assignment state were restored and verified. Timing services
and PPS were unchanged. No namespace, capture service or rollback timer remains
active. The qualified candidate remains installed with inactive, unowned, known-off
output. No unresolved finding remains in this routing slice. Pushed CI is a
separate gate recorded in the completion report.
