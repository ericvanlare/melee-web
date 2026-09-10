# Performance and accuracy playbook

The product goal is full vanilla Melee in a desktop browser through compiled
source, with the original game as the behavioral reference. Accuracy and
performance are independent release gates. Every supported path must pass both;
a fast approximation and an accurate runtime that visibly stalls are both
unfinished.

This document is the operating guide for extending the port. The detailed
contracts and capture procedures remain in
[ACCURACY_CONTRACT.md](ACCURACY_CONTRACT.md),
[PERFORMANCE.md](PERFORMANCE.md),
[ORIGINAL_COMPARISON.md](ORIGINAL_COMPARISON.md), and
[TRANSITION_EQUIVALENCE.md](TRANSITION_EQUIVALENCE.md).

## What each claim means

Use precise evidence labels. Higher rows do not follow automatically from lower
rows.

| Evidence | What it establishes | What it does not establish |
| --- | --- | --- |
| Compiled | Recovered source and the browser target link | The code is reachable or correct |
| Source identified | Numeric kinds, archives, symbols, tables and hashes match GALE01 revision 2 | Runtime lifecycle or behavior |
| Native traced | The bounded source owner, action or stage lifecycle runs and tears down | Retail equivalence or browser timing |
| Retail compared | Declared fields match a pinned, repeatable read-only retail capture at the same source boundary | Unmeasured actions, rendering, audio, input or performance |
| Browser exercised | The Release browser runs the declared visible scenario through original input and scene routes | Retail equivalence outside separately compared fields |
| Performance passed | The declared cold and warm browser scenarios meet the timing, audio and resource gates on a named configuration | Other hardware, browsers, actions or content |
| Admitted | The character/stage pair has passed every required source, retail, browser and performance gate below | Full-game or tournament equivalence |

Coverage is always scenario and field scoped. Do not use “100% accurate,”
“complete,” or “stutter free” without naming the covered content, configuration,
fields and run inventory.

## Accuracy rules

The recovered source remains authoritative for gameplay state, action logic,
collision, damage, RNG consumption, object/process order, floating-point
semantics, animation commands, scene callbacks and audio requests.

- Keep source identities numeric and exact. Registry rows, DAT symbols,
  relocation targets, action counts, map entries, effect banks, Article kinds
  and audio archives must come from the pinned source and owned revision-2 disc.
- Reuse original HSD and game routines behind bounded ownership adapters. Reject
  missing services explicitly; do not add visual substitutes or success stubs.
- Preserve the original input sampling boundary and one source tick per consumed
  sample. Do not invent historical samples after a host stall.
- Preserve float bits unless a field-specific difference has independent retail
  evidence and a documented reason. Do not enable fast-math or relaxed precision.
- Preserve source teardown and reconstruction. Mutable fighters, stages and menu
  scenes are not reusable caches.
- Preserve menu and audio ownership. CSS to SSS retains the original menu audio
  owner and stream position; match entry performs the source owner change.
- Never advance a hidden match, consume RNG, synthesize input, omit an effect or
  alter source order to warm renderer resources.

Accuracy evidence is collected at the narrowest meaningful boundary. Source
identity and native lifecycle tests come first. Deterministic retail comparisons
then use the same rules, players, costumes, stage, RNG, inputs and source tick.
The comparator must report the first divergent tick and field rather than widen
a tolerance or remove a field.

Rendering, emitted PCM, physical input and end-to-end latency need independent
reference evidence. Matching gameplay state alone does not close those gates.

## Performance rules

Interactive callbacks contain input sampling, the required source simulation
and audio work, and frame submission. Optional or reusable work stays outside
that path.

- Build and measure Release WebAssembly. Debug, coverage and computer-control
  runs do not establish shipping performance.
- Keep archive parsing, immutable decoding, resource destruction, database/cache
  serialization and pipeline compilation out of live play.
- Split scene entry into visible preparation phases while the source clock is
  stopped. Report owner construction, renderer preparation and pipeline settling
  separately. No source tick or source draw occurs during owner construction.
- Cache only immutable decoded data and renderer descriptors. Verify cached
  archive bytes against their original immutable baseline and retain normal
  source teardown for every mutable owner.
- Prepare pipelines from unchanged visible scene draws. The reviewed seed may
  contain renderer descriptors, never disc assets. Persist newly discovered
  descriptors only after native teardown.
- Retain auto-pause on timing disruption. It exposes lost scheduling time and
  prevents a stalled host callback from silently changing input/simulation order.
- Treat browser callback interval, browser long tasks, native CPU phases, GPU
  work, audio queue/underruns, heap growth, uploads and pipeline creation as
  distinct measurements.

The current hard browser gate for a declared gameplay scenario is:

- zero automatic timing pauses;
- zero active callback intervals over 33.3 ms;
- zero browser `longtask` entries while the match is active (the browser API
  reports tasks at approximately 50 ms and above);
- zero audio underruns;
- zero pipelines queued or created during live actions; and
- no abort, uncaught exception, WebGPU validation error or manual resume.

The optimization target is a worst active native callback at or below the
16.67 ms source-frame budget on the named reference machine. A 16.67–33.3 ms
callback is recorded and investigated even when it does not fail the current
hitch gate. Scene preparation has a separate cold/warm distribution; report its
wall time and per-phase maximum and reject regressions rather than hiding it in
the active-frame average.

## Foundation already in place

The port currently has these reusable controls:

- recovered gameplay and HSD source compiled to WebAssembly, with pinned source,
  SDK, Emscripten and renderer dependencies;
- typed registries and bounded decoders for fighter, stage, animation, effect,
  item, collision, light, font and audio data;
- scoped source owners with immutable archive verification, complete teardown and
  repeated construction tests;
- original CSS and SSS callbacks, source match handoff, fixed-step input and
  simulation clocks, source pause/resume, stock/respawn and match exit;
- a pinned transition comparator that checks nine CSS/SSS/match boundaries,
  menu audio ownership, rules, player data and RNG and rejects a menu-audio
  restart;
- deterministic movement, jump, jab, damage, stock and respawn comparisons for
  their declared fields and traces;
- staged match construction, per-owner archive reuse, on-demand HPS slot decode
  and continuous CSS/SSS audio production;
- unchanged-scene renderer preparation and a reviewed initial Aurora cache with
  one shader record and 283 pipeline descriptors covering the current menus,
  stages, stock paths and observed Fox first use;
- teardown-only IDBFS persistence, so SQLite serialization cannot interrupt live
  gameplay; and
- telemetry for preparation phases, active native phases, browser callback gaps,
  browser long tasks, audio underruns, pipelines, uploads and heap size. Hitch
  records include the source frame, P1 motion, animation, position and ground/air
  state.

These controls make failures observable and make future content follow the same
ownership boundaries. They do not prove untested moves, effects, costumes,
opponents, stages, browsers or devices.

## Admission workflow for new content

Apply this sequence to every new fighter and stage. A failed step remains an
open gate; later evidence cannot erase it.

1. **Pin identity and dependencies.** Record source kinds, tables, archives,
   symbols, hashes, actions/map rows, Articles, effects and audio. Reject missing
   or ambiguous relocations.
2. **Hydrate bounded owners.** Implement the source callbacks and required
   services with explicit lifetimes. Run construction, action/stage behavior,
   teardown and a second construction while verifying input bytes remain
   unchanged.
3. **Exercise the native action inventory.** Cover every common action and each
   fighter/stage-specific action, Article, effect, scheduler and dynamic
   collision family. An action row that has not passed remains unavailable.
4. **Compare retail semantics.** Add or extend a pinned read-only retail capture
   for new behavior. Compare identical source ticks and report the first differing
   field. Re-run existing transition and gameplay comparisons after shared
   runtime changes.
5. **Run the visible Release browser matrix.** Use original CSS and SSS, then
   cover intro, ordinary movement, at least ten wavedashes in each direction,
   defense, ledge/recovery, every attack/special/article/effect variant, hits and
   shield contact, KO/respawn, pause/resume, match exit, return to CSS and a
   second match. Add stage-specific scheduled and dynamic behavior.
6. **Run cold and warm.** Start once with the origin render cache cleared and
   once after a complete unload/application reload. Record the machine, browser,
   OS, resolution, display/power state, build commit and scenario inventory.
7. **Close first-use work.** Any pipeline discovered by visible gameplay is added
   to the reviewed seed, then both runs are repeated. Any live texture upload,
   allocation/heap growth or long callback is classified and removed or kept as
   an explicit failed gate.
8. **Record the result.** Store raw local evidence under ignored `work/` and add
   the exact passed scope and remaining gaps to `STATUS.md`. Only then update the
   admitted content claim.

The detailed fighter and stage checklists are in
[ADDING_CHARACTERS.md](ADDING_CHARACTERS.md) and
[ADDING_STAGES.md](ADDING_STAGES.md).

## When a hitch or mismatch appears

Preserve the first failure before changing code. Record the source frame/motion,
browser interval, native phases, long-task entry, audio queue, cache state,
pipeline counts, upload bytes and heap size. Repeat once from a cleared origin
and once warm to separate deterministic action work from first-use/browser work.

Classify the first responsible boundary:

| Signal | Likely boundary to inspect |
| --- | --- |
| Native callback is long | Source simulation, audio work, frame build or synchronous allocation |
| Browser gap/long task is long while native phases are short | JavaScript, IDBFS, garbage collection, browser scheduling or driver work between callbacks |
| Pipeline count changes | Missing reviewed descriptor or a genuinely new render state |
| Upload bytes appear on first action | Texture/resource residency and scene preparation coverage |
| Audio underrun without a long native callback | Worklet scheduling, queue sizing or owner handoff |
| Retail comparator diverges | Source ordering, input/RNG boundary, numeric behavior or incorrect data ownership |
| Warm passes and cold fails | First-use decode, upload, compilation, heap growth or persistence work |

Fix the responsible host or ownership layer without changing source semantics.
Add the triggering input sequence to the action inventory and its narrowest
automated regression. Re-run the retail comparison when execution order, input,
RNG, numerics or source-visible lifetime could have changed, then repeat the cold
and warm browser matrix.

## Work still required

The foundation must expand with the port:

- turn the versioned browser action inventory into a repeatable in-page Release
  runner that uses the same visible raw-PAD boundary and emits a machine-readable
  report without hidden simulation;
- cover every fighter action/effect/article, costume/opponent combination and
  stage-specific renderer state, then keep the reviewed pipeline seed synchronized;
- move remaining first-use immutable decode/upload work into measured scene
  preparation without constructing duplicate mutable source state;
- add GPU completion/presentation evidence rather than treating CPU submission
  time as GPU frame time;
- broaden deterministic retail state traces to the complete versus action and
  stage inventory, including RNG-dependent characters and items;
- add independent rendering, emitted-PCM and long-run drift comparisons;
- test physical controllers, reconnect behavior and controller-to-photon/audio
  latency on a pinned competition configuration;
- run repeated full matches and scene cycles while tracking memory high-water
  marks and verifying complete teardown; and
- publish the versioned coverage inventory described by issue 3, separating
  compiled, source-traced, retail-compared, browser-exercised and admitted scope.

Worker-based simulation or other scheduling changes remain candidates only.
They must first prove browser input availability, deterministic ordering,
render/audio communication and latency, then pass the same retail comparisons.
They are not accepted as performance fixes merely because they move a stall to
another thread.

## Change checklist

Before merging a shared runtime, performance or content change:

- [ ] Source identity and ownership boundaries are explicit.
- [ ] No source semantics, ordering, RNG, input or numerical behavior changed
      without matching retail evidence.
- [ ] Focused native lifecycle/action tests pass twice through teardown.
- [ ] Relevant retail transition and gameplay comparisons pass.
- [ ] Release WebAssembly builds and the complete automated suite passes.
- [ ] The visible cold and warm browser inventory passes with the hard timing,
      audio, pipeline and error gates.
- [ ] First-use resources are covered without hidden source simulation.
- [ ] Raw evidence names its machine, browser, configuration, commit and scope.
- [ ] `STATUS.md`, the content notes and pipeline-seed inventory describe the
      measured result and every remaining gap.
