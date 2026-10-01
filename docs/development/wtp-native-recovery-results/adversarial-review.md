# Adversarial assessment

2026-09-30. Review and reassessment were performed in the current thread; no
independent reviewer or sub-agent is claimed.

## First assessment and repairs

| Finding | Closure |
| --- | --- |
| Endpoint construction covered Si5351 and explicit simulation while other native transmitters could not serve WTP. | Shared native bridge/hooks now select GPIO, RP1 or Si5351 explicitly. Outbound WTP does not proxy or silently replace a native route. |
| GPIO capability and policy could use a default processor profile before native preparation. | Parent passes the exact Pi processor profile into the reusable backend before CAPS. Native preparation still verifies hardware agreement. Processor-specific policy remains in requested and realized-frequency admission. |
| GPIO cleanup returned unconditional success. A shutdown exception could also escape a scope guard. | Scheduled cleanup performs fresh native GPCLK/PWM/DMA quiescence and propagates failure. The guard catches exceptions and records failed execution. Fake-access regression and actual native confirmation-failure injection verify conservative unknown output. |
| Scheduled plans needed native validation, preparation and launch admission rather than a parent-only timing claim. | Contiguous finite duration/tone tables are validated; native preparation precedes admission, observed enable anchors the timeline, and realization uses the configured native program. All five GPIO modes, including a full WSPR frame, have live SDR evidence. |
| Treating every RP1 TONE as a general event sequence changed legacy local semantics. | Scheduled-event compilation is opt-in; the default local single/continuous-TONE contract is retained. Existing and added RP1 provider tests pass. Exact-job development confirmation and route/provider policy remain mandatory. |
| A new reusable GPIO dependency on application processor discovery broke standalone linking. | Processor profile is passed through an optional constructor parameter instead. Component build independence is preserved. The preexisting qualification target's missing clock-model/mailbox-revision objects were added to its own link list. |
| The private clone name initially changed executable/project identity. | Its canonical repository identity was restored before the final native release build and deployment. The deployed filename, runtime identity, private build baseline and binary hash are recorded explicitly. |
| The initial live lease test expected active-job abortion on lease expiry. | This conflicted with WTP/1. The expectation was corrected to finite completion and terminal ownership release; no lease behavior was changed. Actual Fleet crash/restart retained the consumed slot and required reconciliation. |

## Second assessment

Rechecked the complete component and integration diff against local control,
profile/frequency policy, finite event bounds, physical admission, realization,
cleanup, process singleton, request replay and retained-job contracts.

- Source and native binary identities match the recorded overlay. No application
  `src/` change exists between the private build base and parent baseline.
- No simulation fallback or production fault-injection setting was introduced.
- RP1 finite plans retain existing development confirmation; its physical WTP
  operation was not executed or promoted by mocked provider tests.
- Native realization is predictive metadata from divider/event counts, separate
  from SDR-measured frequency. The GPIO dither counter telescopes to the same
  finite count used in reported realization; metadata does not claim calibrated
  RF-edge timing or frequency accuracy.
- Local legacy test-tone dispatch remains unchanged. Scheduled TONE requires a
  finite duration; default RP1 local continuous behavior stays separate.
- Failed output-off confirmation keeps conservative active/unknown state even
  when SDR independently shows physical output absent. Restoring the injected
  seam alone does not clear it; explicit native recovery does.
- Lost replies returned identical cached responses, with one job and one launch.
  A consumed Fleet slot remained blocked after controller restart.
- Actual browser takeover confirms local pending/effective separation. Direct
  HTTP and actual monitored INI writes cancel without a central approval path.
- Original wspr5 binary/configuration were preserved; both services are active.
  wspr4's complete pre-takeover configuration is restored, keeping the authorized
  GPIO candidate idle. Persisted revocation generation is 4 and was not rewound.

A further claim issue was found: forced GPIO process death did not immediately
turn the clock off. RF continued until the approximately 30-second managed
restart interval and startup quiescence. Application and feature-gated operator
documentation now state this boundary. Responsive-process cancellation passed;
process-crash acceptance establishes startup recovery and stale-job rejection,
not a five-second stop guarantee after `SIGKILL`.

## Final reassessment

Full native Linux semantics, focused endpoint/RP1 tests, standalone component
checks, the shared fake-I2C transition regression, and the portable Mac suite
passed. Sphinx enabled/disabled builds and feature-flag assertions passed;
Impeccable returned no changed-source findings, and affected desktop/mobile
content was inspected. Whitespace checks and acceptance-data assertions passed.

No unresolved source defect was found within this adapter/recovery scope.
Actual power removal, RP1 physical WTP, larger/mixed fleets, live-browser Fleet
assignment editing and discovery topology/cache changes remain explicit
acceptance gates. These gates are not marked passed or release-ready.

## October 1 physical power follow-up

Actual operator power removal passed; the [power-failure assessment](power-failure/README.md#review-and-practical-limits)
records review and reassessment, retained-slot recovery, RF cessation, fresh boot,
restoration and the bounded controller guard's expiry. No application source
changed. Power return occurred after the original finite job window; no earlier
return or uninterrupted controller uptime through closure is claimed. The final
power case is closed. RP1, broader fleet/discovery/browser acceptance and the
newly completed Mac/network CI failures remain open.
