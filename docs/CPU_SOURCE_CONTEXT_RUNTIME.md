# CPU source context in the runtime

The CPU skipped-conversion path now runs with a checked source register context
and live Fighter allocation identity. The [receipt](evidence/cpu-source-context-runtime-v1.json)
records the exact browser prefix, build identities, focused regressions and
remaining failures. [STATUS](../STATUS.md) indexes the current result. This
supersedes the component-only integration limits in the earlier
[carry](CPU_R5_CARRY.md) and [allocation](SOURCE_ADDRESS_CONTEXT.md) records.

## Causal repair

`ftCo_800AC5A0` can skip defining its stick locals during hitlag with a near-zero
knockback vector. The original instructions consume signed low bytes of the
reaching r5 and r30 words. The runtime preserves two audited r5 routes:

- `HSD_Randf` leaves the source seed-global identity.
- `mpCheckFloor` leaves the address of its own `y0_sp44` local. The logical source
  stack follows the original callback chain and prologue sizes.

The r30 word comes from the current Fighter's source allocation. The context
checks world, callback, Fighter and allocation lifetimes. Unsupported reaching
definitions fail at the skipped-conversion consumer. Captured register values,
Fighter addresses and expected CPU outputs are never runtime inputs.

The caller's `floor_pos` experiment tested the wrong owner. A later experiment
using the correct callee's raw Wasm stack address also failed. The implemented
route derives the source stack address from the owned executable's stack root
and frame geometry. Matching a host pointer's low byte would not establish this
identity. The owned-DOL regression checks the profile directly against
instructions and the pinned symbol map.

## Allocation and source consumers

Mirroring allocation operations only works when original consumers execute in
their original order. The integration restores scene-manager allocations,
typed archive/file ownership, stage/collision/camera initialization, common
fighter loading, fighter asset allocations and source startup continuations.
The address model follows actual OS heap operations; it does not add allocation
padding to reach a comparison address.

Two later reductions exposed concrete ordering and hydration defects:

- Readiness checks evaluated spawn matrices before original Fighter common
  initialization. Readiness now checks publication; the original spawn consumer
  evaluates the matrix at its source boundary. The separate native spawn query
  checks consumed positions for nonfinite values.
- Native texture hydration dropped present TEV descriptors when `active == 0`.
  Original `HSD_TObjLoadDesc` still allocates and copies those descriptors. The
  parser now retains their raw fields for native consumers. An internal HSD
  piece trace located the omission in the common material before it affected
  main-heap allocation order. All four complete Fighter source identities then
  agreed with the independent original trace.

Common material joints have explicit ownership without synthetic GObjs.
Results demo Fighters acquire and release source leases, and owned effect banks
are republished after original effect-table initialization. Cleanup/restart
checks cover these boundaries. Temporary allocation and piece probes are
removed from the runtime; retained diagnostic captures are comparison evidence.

## Validation boundaries

Focused regressions cover both carry routes, distinct live Fighter identities,
stale generations, unknown routes, inactive TEV bytes/ownership, lazy matrix
evaluation, archive retirement, effect reset/reload and repeated Results
construction. Run the owned profile check with `MELEE_CPU_DOL` pointing to the
owned GALE01 revision-2 DOL; it reports an explicit skip without that input.

The browser comparison uses the existing recorded controller-queue recipe and
unchanged exact RNG, match-frame, PAD and complete Fighter records. This is
conditional state evidence. Port draw ordinals are not captured by this trace;
the original tick-1816 cadence marker alone establishes no port draw divergence.
Pixels, PCM, live input, foreground timing and full-session equivalence require
their separate gates. Later failures remain explicit in the receipt.
