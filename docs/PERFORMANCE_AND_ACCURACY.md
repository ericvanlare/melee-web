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
[TRANSITION_EQUIVALENCE.md](TRANSITION_EQUIVALENCE.md). Scalable gameplay
coverage and replay admission are defined in
[SLIPPI_REPLAY_VALIDATION.md](SLIPPI_REPLAY_VALIDATION.md) and the measured
[vanilla replay corpus](REPLAY_CORPUS.md).

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
- Verify math-library boundaries as well as gameplay formulas. Host libm can
  differ from the original MSL approximation. The first repeated Slippi-derived
  vanilla trajectory caught both sine and arctangent differences during air
  dodges. Reuse the recovered routines, preserve their explicit fused-operation
  rounding, and keep a failing host-libm control alongside retail scalar evidence.
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

Require source-drawn comparison before admitting a replay workload. Headless
traces omit camera callbacks that can affect later gameplay: the original
magnifier sets an offscreen flag used by fighter damage. A draw audit that
preserves the currently declared fields does not prove independence from all
hidden render state. Keep a headless red visible, identify the source dependency
and validate it in the real drawn path; do not fake the missing flag or waive
the affected damage/RNG fields. See the [corpus example](REPLAY_CORPUS.md).

When a replay exposes hidden state, compare the original owner construction and
its first consumers as well as the visible formula. The expanded corpus found
missing screen-flash ownership and a missing ground reset whose floor sentinel
changed camera culling and later damage. Restore the original shared routine at
its source lifetime boundary, verify teardown and repeated construction, and
retain the complete failing replay. A locally plausible numerical change is
not a fix unless the actual failing operands reach it and the reference
comparison changes as predicted.

For a numerical red, compare operand bits before changing the consumer. Capture
the original producer's input/output boundary, keep the unfused or prior path
as a negative control, and verify a shared primitive before replaying the full
game. Local pose agreement isolated the expanded Yoshi's Story failure to the
SDK quaternion-matrix conversion. Its paired-single sums and reciprocal estimate
must retain the original order; algebraically equivalent C expressions can
change rounding and signed zeros. Keep a few observed scalar cases in fast
tests, with the complete original captures in the evidence ledger.

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
  one shader record and 344 pipeline descriptors covering the current menus,
  stages, stock paths and the complete versioned Marth/Dream Land sweep;
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
3. **Exercise the replay corpus.** Normalize supported recordings with explicit
   exact/derived input and setup provenance. Workload completion is separate
   from accuracy comparison. Use measured execution coverage to select canaries
   and a held-out admission set; process the broader corpus on scheduled runs.
   Keep `web/action-sweep.mjs` for smoke tests and focused reproducers.
4. **Compare retail semantics.** Require repeatability of the independent pinned
   vanilla reference before interpreting a port mismatch. Use complete declared
   initialization, actual consumed inputs and matching source phases. UCF `.slp`
   state is never the vanilla oracle, and state/RNG observations are never
   injected to repair drift. Report the first differing field. Re-run existing
   transition and gameplay comparisons after shared runtime changes.
5. **Run the visible Release browser matrix.** Play the same admitted replay
   canaries at real time through ordinary rendering and audio, then run the
   existing CSS/SSS, pause/resume, match-exit, return-to-CSS and second-match
   lifecycle checks which Slippi cannot cover. Add stage-specific scheduled and
   dynamic behavior to the corpus coverage requirements.
6. **Run cold and warm.** Start once with the origin render cache cleared and
   once after a complete unload/application reload. Record the machine, browser,
   OS, resolution, display/power state, build commit and scenario inventory.
7. **Close first-use work.** Any pipeline discovered by visible gameplay is added
   to the reviewed seed after teardown, then both runs are repeated. Live texture
   uploads are recorded and classified because source-authored animation can
   upload by design. Pipeline creation, long callbacks and audio underruns during
   the sweep fail the gate. Heap growth is recorded independently; growth outside
   the source clock must be explained, and growth correlated with a timing failure
   fails the gate.
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

## Repeated-session memory checks

For renderer or lifetime changes, run a retained complete workload at least
three times in the same application, with no reload or cache reset between
matches. Save each report before the next run. This complements the independent
cold/warm application runs; it does not replace them. Preserve timing failures
and do not resume a performance run into a passing result.

Use the replay report's lifecycle memory snapshots to separate Wasm capacity
from allocated bytes. Compare `before_preparation`, `prepared`,
`before_teardown` and `after_teardown`. The current pinned dlmalloc allocator
reports live, free and top-free bytes; sample these outside the source clock.
Its free-list walk does not belong in per-tick telemetry. Record any allocator
change along with the build profile.

Stable post-teardown live bytes with growing capacity indicate a high-water or
fragmentation investigation. Growing live bytes require ownership attribution
to the source world, imported archives, audio or renderer caches. Retained
immutable caches are expected, but their bounds and retirement must be explicit.
Neither raising the stopped-clock reserve nor forcing a page reload demonstrates
a lifetime fix. Follow the retained workload through real teardown and reuse;
measure additional characters/stages as the corpus expands.

Browser staging now has explicit bounded ownership: two frame packets each own
63 MiB of CPU shadow storage, and two GPU staging buffers remain leased until
submitted work completes. Upload only the used prefixes, clear the previous
dirty prefixes before reuse, and preserve the original command-buffer copy/draw
order. Queue completion failure is fatal; callbacks from a retired renderer
generation cannot release new leases. The native mapping path is unchanged.
`liveStagingUsedBytes` and `peakStagingUsedBytes` report logical per-frame used
ranges, not GPU allocation, driver memory, or separately allocated overflow
texture uploads. Measure those distinctions before adjusting capacities.

## Work still required

The foundation must expand with the port:

- expand the first passing Slippi-input → independent vanilla reference →
  visible state/performance canary into execution coverage and held-out inputs,
  as described in `SLIPPI_REPLAY_VALIDATION.md`;
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
- [ ] Release WebAssembly builds and the complete automated suite passes. Rebuild
  every trace cited for a changed shared runtime boundary before running it;
  tests that select an existing executable can otherwise pass against stale
  code. Keep fixture manifests aligned with the current owned dependencies.
- [ ] The visible cold and warm browser inventory passes with the hard timing,
      audio, pipeline and error gates.
- [ ] First-use resources are covered without hidden source simulation.
- [ ] Raw evidence names its machine, browser, configuration, commit and scope.
- [ ] `STATUS.md`, the content notes and pipeline-seed inventory describe the
      measured result and every remaining gap.

## Replay calibration lessons

The [retail replay calibration](RETAIL_REPLAY_CAPTURE.md) found two shared gaps
that hand-authored move sweeps had missed: an unregistered stage particle bank
and a headless rumble exclusion. Visual effects consume the shared gameplay RNG;
never omit them from a simulation oracle or remove them to improve performance.
Stage entry now verifies authored particle bank/command dependencies and shares
one decoded asset owner across both original bank slots. Native workload tests
exercise the original rumble interpreter; physical output remains a separate
acceptance check. New source systems need their real service/data owners in the
common runtime, with explicit hardware boundaries.

A repeated reference plus a complete exact-field port comparison is a useful
calibration milestone. It does not waive missing global input history, drawing,
PCM, hardware, browser timing or held-out coverage. Retain first-divergence
records from before a fix and run the relevant common-system checks afterward.
Reference generation is offline; validate a faster exporter against the small
GDB oracle before expanding the corpus. Reuse immutable expected traces for
routine checks so correctness does not make content iteration prohibitively slow.

Test drivers also need explicit failure evidence. The Release Marth/Dream Land
sweep stopped before Counter when neutral input could not leave the original
OttottoWait platform-edge state within the recovery bound. Recovery now uses
ordinary inward PAD input from Wait/Ottotto/OttottoWait, while still requiring
grounded Wait near center before the next case. No source state or timing gate
was changed. The subsequent warm 46-case run completed 6,168 source frames with
zero hard-gate failures, worst browser callback 22.115 ms and worst native
callback 12.190 ms. It queued/created no live pipelines, had no audio underruns,
preparation pauses or heap growth, and uploaded 727,040 texture bytes. This was
a visible Release IAB run on Apple M4 / 32 GiB / macOS 26.6.2 with normal audio;
the embedded browser version was not captured. It is not
cold-cache, full-match, hardware-input or Slippi replay acceptance. Both the
incomplete run and passing rerun are recorded under the local calibration
evidence directory. Source unload, render-cache persistence and owned browser /
server / reference-process cleanup completed afterward.

## Complete combat calibration lessons

A complete source match is a lifecycle claim, not an input-length claim. Require
the original exit observation, the successful JSONL end record and the final
source draw as separate completion evidence. Keep exact consumed PAD vectors and
all four-port checks as hard gates; a UCF recording supplies an input workload,
never a vanilla expected-state oracle.

Measure coverage on the new vanilla trajectory after rules or removed
modifications change its path. Preserve first-divergent ticks and verify
compiler-fused math and recovered library routines against the original DOL at
their rounding boundaries. Replace linker or global-adjacency assumptions with
actual object references: the effect-parameter-table failure showed that fields
can look correct while an adjacent global has already been corrupted.

Run constructor, teardown and per-tick ownership diagnostics alongside semantic
state comparisons. They can expose memory corruption before a state field
diverges and keep fixes focused on the original owner and lifetime.

The current Fox/Falco Battlefield calibration reaches 3,122 ticks with exact
independent reference repeatability, the expected elimination outcome, the
original exit and final draw, and an exact visible source-draw state comparison.
The final protected-dequeue collector also matches the repeated trajectory in
Interpreter64, preserving exact actual-input checks across the former capture
race.
The discovery pass found 38 new pipelines and expanded the reviewed seed from
389 to 427 descriptors. Final visible Release cold/warm runs pass all 3,122 ticks
with zero hard-gate failures: worst native callbacks 11.330/12.485 ms and worst
browser intervals 21.365/22.010 ms. This is scoped performance evidence; broad
content admission remains open. The [complete-game ledger](COMPLETE_REPLAY_CALIBRATION.md)
records reference cross-calibration, observed coverage, preparation, memory and
upload measurements alongside the joined receipt.

## First visible replay canary

The original 686-tick Fox/Falco Battlefield donor remains a movement canary. It
covers six air dodges but no combat or stock loss, so it cannot admit Fox, Falco
or Battlefield as complete content. The complete combat calibration above uses
a separate derived workload and retains the same exact-input and declared-state
boundaries. `scripts/check_browser_replay.py` rechecks the raw reference/port
files, requires the exact paired input recipe, binds the state report to the
trace, validates timing counters when performance evidence is available, and
records the build/configuration evidence. An operator still attests to the
visible machine profile and console inspection; the report does not pretend to
be cryptographic browser/build attestation.

The receipt remains scoped to the selected ticks and compared fields. Keep
memory growth and uploads in the report even when they do not cause a hitch. See
[the exact evidence and procedure](RETAIL_REPLAY_CAPTURE.md#visible-browser-replay).
