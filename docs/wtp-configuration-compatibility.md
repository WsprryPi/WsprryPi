# Pico control compatibility

Pi and Pico use one current `Wsprry-Control/1` settings/control contract under
`/api/control/v1`. Earlier beta settings APIs and resource views are unsupported;
update both applications together. WTP/1 finite-job compatibility is independent.

Pico owns the authoritative contract. Pi validates the current contract,
capabilities and selected carrier before using a member resource. Unknown
optional capabilities grant no authority. Known documents remain closed and
bounded; malformed descriptors, unsupported types/units, inconsistent
advertisement/readback and wrong device/boot/profile bindings refuse before
writes. Policy values are typed and bounded rather than identified by old
literal session-limit tables.

The current application/hardware representation includes amplifier-enable pin
and polarity, GPIO drive and saved versus boot-active state. The general Pi
coordinator validates and applies that representation. Amplifier timing and
filter selection retain their domain resources and transaction boundaries.
Changed hardware remains inhibited until explicit restart, including A/B/A
saves. No save measures RF power or proves hardware readiness.

Ordinary settings updates contain intended changes only and preserve omitted
settings. Explicit false and zero remain values. Pi does not reconstruct full
configuration from a redacted read or switch automatically to an older save
strategy. Pico remains the validator and journal authority. Profile-owned
station/network credentials retain transactional setup; generic edits do not
bypass it. Independent stored drafts require explicit domain application and
never establish transmit readiness merely by being saved. For pin drafts, omitted
amplifier polarity preserves the prior draft, otherwise the applied setting,
otherwise active-high. Pi derives the expected readback from authenticated
state bound to the same saved revision before sending a mutation.

Mutations require the current target and exact If-Match revision. Missing,
stale, busy, faulted, unhealthy or ambiguous context refuses. Secrets remain
redacted. A lost or malformed reply invalidates the write context: retain the
draft, read saved state and review before another deliberate save. No mutation
retry, peer substitution or carrier fallback is automatic.

USB and supported authenticated network carriers use the same control model
through their existing production adapters. USB retains its matching response
ACK before deferred effects; network retains complete-response delivery
confirmation. Durable saves and accepted jobs can survive a lost reply.

Both actual production network and USB suites must pass for the explicit
candidate source pair, including amplifier-enable operations and old-contract
refusal. Generated contract provenance and tested full-source identities are
recorded separately. Software evidence does not qualify physical deployment,
services, timing or RF.
