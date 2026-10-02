# Live DNS-SD topology and cache acceptance

Execute the remaining discovery acceptance on `devel`, starting at
`960914333884158c87040c030d49ecaeb8795e65`. Use wspr5's installed application,
Ethernet `eth0` at `192.168.1.54` and station WiFi `wlan1` at `192.168.1.117`.
Start with wspr4 at `192.168.1.120` as an independent observer; preserve
diagnostics and use healthy wspr2 at `192.168.1.123` if its API stalls. Pico A's existing
`wsprrypico-0a60df.local` advertisement is available for native TTL expiry;
Pico B is not required. Preserve the five paused Fleet assignments, catalog,
local Enable off, current binary, INI, backend, GPS/PPS and clock configuration.
No RF job, CLAIM, LOAD or ARM is part of this campaign.

Before any interruption, capture the complete addresses/routes, active
NetworkManager connection UUIDs, relevant configuration hashes, service PID,
endpoint boot/device identities, DNS-SD records and paused Fleet state. Keep
the backup containing WiFi credentials private on wspr5. Establish SSH through
both independent physical interfaces and arm an independently scheduled
systemd rollback before changing an interface. Capture native mDNS packets
and continuous installed discovery/endpoint API observations on both Pis.

1. Disconnect and reconnect `wlan1` in software. Require observations on that
   interface to become unavailable, Ethernet observations and the selected
   Ethernet WTP listener to survive, and WiFi observations to recover.
2. Disconnect and reconnect `eth0` while managing wspr5 through WiFi. Require
   Ethernet observations to become unavailable, WiFi observations to survive,
   the selected Ethernet listener/publication to withdraw, and that same
   endpoint to recover after Ethernet returns.
3. Probe a temporary LAN address for conflicts, then apply it to the active
   Ethernet connection without rewriting its saved DHCP profile. A temporary
   NetworkManager clone with a locally administered MAC and a fresh DHCP lease
   is an alternative. Keep observer-management traffic bound to unchanged
   WiFi, and restore the original Ethernet MAC and remove the temporary profile.
   Require the
   listener to bind the new address, an independent observer to receive its
   endpoint-specific A/SRV/TXT records, and read-only HELLO/CAPS/STATUS
   identification to return the original full device ID. Restore DHCP and
   require rediscovery and successful identification at the original address.
4. Measure an existing real advertisement's wire TTL. For Pico A the ordinary
   PTR/SRV/TXT/A records use 120 seconds; Pi PTR/TXT use 4500 seconds and
   SRV/A use 120 seconds. Suppress only Pico A's incoming IPv4 UDP/5353 on
   both wspr5 interfaces using temporary, uniquely identifiable traffic-control
   filters. Leave its WTP listener and wspr4's observations reachable. Do not
   inject shorter TTLs or synthetic records, stop a browser, restart Avahi,
   remove a link or send a goodbye during the expiry interval. Require both
   cached candidates to expire naturally, identification to reject them, and
   unrelated records to remain online. Remove the filters, query again, and
   require both observations and original-device identification to recover.

Compare final configuration and persistent-state hashes against the baseline.
Restore addresses, active profiles, original default-route preference and
traffic-control configuration. Stop test observers and rollback timers. Verify
that all five assignments remain paused, their definitions and consumed-slot
watermarks are unchanged, local Enable remains off, output is inactive and
the installed service and endpoint boot remain unchanged. Transfer large
evidence through compressed streams; never commit private WiFi/INI backups.

Review the capture and assertions adversarially. Distinguish graceful
withdrawal from TTL expiry, local cache removal from publisher removal,
interface-specific records from duplicate devices, and a cached hostname from
an actually reachable WTP listener. Repair harness issues, retest the affected
case, and reassess. Record application/runtime findings separately for subsequent
implementation; this request executes discovery testing. Record the exact topology, methods,
failures and restoration evidence without claiming RF or wider fleet
qualification. This execution request does not authorize a new commit or push.
