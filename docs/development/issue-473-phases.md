# Issue 473 phases

Status: ▧ not started · ⏳ in progress · ✅ complete.

These indicators track phase execution, not prompt preparation. Phase 1 is
design-only; its completion does not claim application implementation. All eight
prompts have been prepared and reviewed. At preparation (2026-11-15), no implementation
phase had started.

Follow the [authoritative plan](device-owned-solar-scheduling-plan.md) for
[issue #473](https://github.com/WsprryPi/WsprryPi/issues/473). Each prompt requires
separate authorization to execute and successful prerequisite gates.

1. ✅ **Define contracts and decisions:** Resolve security, capabilities,
   migration, ownership, persistence, and time/location requirements.
   [Phase 1 prompt](issue-473/01-contracts-and-decisions.md) ·
   [Approved decisions and phase-1 acceptance](issue-473/phase-1-decisions.md)
2. ✅ **Separate target management:** Manage the local Pi and existing Picos
   independently of the controller's transmitter backend; keep new remote Pi
   management unavailable under the approved security deferral.
   [Phase 2 prompt](issue-473/02-target-management.md) ·
   [Phase-2 acceptance and deferred-security boundary](issue-473/phase-2-progress.md)
3. ▧ **Unify Setup controls:** Add the shared Device selector and move existing
   controls into target-aware tabs.
   [Phase 3 prompt](issue-473/03-shared-setup.md)
4. ▧ **Build Pi solar policy:** Implement solar calculations and the complete
   configuration lifecycle, preserving existing behavior when disabled.
   [Phase 4 prompt](issue-473/04-solar-policy.md)
5. ▧ **Prevent competing authorities:** Add safeguards and explicit migration
   from controller-owned schedules before allowing new schedule enables.
   [Phase 5 prompt](issue-473/05-authority-transition.md)
6. ▧ **Integrate device-owned recurrence:** Apply solar policies to Pi scheduling,
   including ownership, enablement, restart handling, and status. Complete the
   [deferred Pi pairing/TLS and remote-management gate](issue-473/deferred-pi-security.md)
   before this phase can finish or phase 7 can begin.
   [Phase 6 prompt](issue-473/06-device-recurrence.md)
7. ▧ **Add shared Scheduling UI:** Provide target-specific scheduling controls
   and verify autonomous remote operation.
   [Phase 7 prompt](issue-473/07-scheduling-ui.md)
8. ▧ **Complete documentation and validation:** Finish operator guides,
   integration tests, visual reviews, and delivery evidence.
   [Phase 8 prompt](issue-473/08-closeout.md)

The user approved the [security sequencing amendment](issue-473/deferred-pi-security.md)
on 2026-11-16. It preserves all eight phases, existing WTP/Pico TLS, and the eventual
automatic-security requirements. It authorizes no later-phase execution.

Read the [shared execution rules and prompt review record](issue-473/execution-rules.md)
with every prompt. Update an indicator only from actual phase evidence; ✅ requires
its acceptance gate and applicable documentation and validation to be satisfied.
