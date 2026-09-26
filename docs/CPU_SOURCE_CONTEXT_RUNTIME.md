# CPU source context in the runtime

The CPU skipped-conversion path now runs with a checked source register context
and live Fighter allocation identity. The [receipt](evidence/cpu-source-context-runtime-v2.json)
records the exact browser prefix, build identities, focused regressions and
remaining failures. [STATUS](../STATUS.md) indexes the current result. This
supersedes the component-only integration limits in the earlier
[carry](CPU_R5_CARRY.md) and [allocation](SOURCE_ADDRESS_CONTEXT.md) records.

## Causal repair

`ftCo_800AC5A0` can skip defining its stick locals during hitlag with a near-zero
knockback vector. The original instructions consume signed low bytes of the
reaching r5 and r30 words. The runtime preserves two audited r5 routes:

- Each of the three `HSD_Randf` sites in `ftCo_800ADE48` publishes the source
  seed-global identity when the audited callback route is active.
- An eligible line in `mpCheckFloor` defines the address of its own `y0_sp44`
  local. The logical source stack follows the original callback chain and
  prologue sizes. Horizontal tests preserve that definition on hits and misses.

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

## Executed definitions and clobbers

Independent review of the first runtime receipt found that unconditional
publication at `mpCheckFloor` return falsely admitted two synthetic queries:
all joints pruned, and a final sloped-line test. That finding concerns register
tracking; it does not invalidate the earlier exact Final Destination capture.
The [earlier receipt](evidence/cpu-source-context-runtime-v1.json) remains the
record for that build. The current receipt binds the corrected implementation
and its fresh comparison.

The runtime tracks the executed source boundaries, independently of the query's
hit result:

| Boundary | Carry action |
| --- | --- |
| `mpCheckFloor` entry | Invalidate the caller's unclassified flags-output argument |
| Eligible line setup before `mpLib_8004ED5C` | Publish the callee local from logical r1 + the owned DOL offset |
| Arbitrary floor callback | Invalidate before and after the call, including rejected or skipped lines |
| Nonhorizontal `mpLineIntersection` | Invalidate its unsupported r5 = 0/1 definition |
| Later eligible flat line | Replace an earlier clobber with the newly executed local definition |
| `mpJointFromLine` valid-line lookup | Invalidate its joint-array definition; preserve the -1 early return |
| `gm_8016C75C` standings refresh | Invalidate inside the executed refresh branch; preserve cached queries |
| All three caller `HSD_Randf` sites | Replace prior carry with the seed-global identity |
| Caller continues past state-18 selection | Invalidate before other command-specific routines |
| Executed charge-cancel branch in `ftCo_800ADC28` | Invalidate before its command writers |

The joint lookup covers all four stage predicates reached through
`ftCo_800A1B38`. Other-stage predicate returns do not execute it. The standings
refresh can occur on either caller query, regardless of whether its returned
stock count changes. These hooks follow the executed source branches. The
callback route, live Fighter lease and generation checks remain necessary in
addition to a known reaching definition.

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
stale generations, unknown routes, actual floor hits/misses, pruned/sloped and
callback clobbers, later-line overwrites, joint lookup, inactive TEV bytes and
ownership, lazy matrix evaluation, archive retirement, effect reset/reload and
repeated Results
construction. Run the owned profile check with `MELEE_CPU_DOL` pointing to the
owned GALE01 revision-2 DOL; it reports an explicit skip without that input.

The browser comparison uses the existing recorded controller-queue recipe and
unchanged exact RNG, match-frame, PAD and complete Fighter records. This is
conditional state evidence. Port draw ordinals are not captured by this trace;
the original tick-1816 cadence marker alone establishes no port draw divergence.
Pixels, PCM, live input, foreground timing and full-session equivalence require
their separate gates. Later failures remain explicit in the receipt.
