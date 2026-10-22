# Match-only rollback feasibility

**Compiled / Source-only snapshot diagnostic / Terminal browser tick-and-draw diagnostic / No-draw declared-state component / Browser rollback and frame-budget feasibility unproven**

[Milestone #318](https://github.com/ericvanlare/melee-web/issues/318) and
[merged #319](https://github.com/ericvanlare/melee-web/pull/319) implement the
owner's match-only direction. Original menus, transitions, Results and rematch
remain local. The planned mode choice is Vanilla offline-only or Slippi rules
offline/online, with distinct pinned references; the toggle/profile are not yet
implemented. The [structured receipt](../evidence/match-only-rollback-feasibility-v1.json)
binds current producer and retained failure identities.

## Same-instance browser restore, issue #341 (in progress)

[Issue #341](https://github.com/ericvanlare/melee-web/issues/341) owns the next
browser boundary. Private components now capture the source allocation mirror,
bootstrap/context/flow values, decoded DAT allocations, and native audio/effect
buffers under explicit owner and lifetime guards. They are not composed into a
browser restore. Independent review caught a missing GX callback marker in three
snapshot admission checks; the repaired checks refuse simulation and draw
callbacks separately. The actual production flow control passes with synthetic
lifecycle prerequisites. The original single/two-player constructor, neutral
ticks, context checkpoint controls and unload/restart also pass in two Node
worlds after repairing the older fixture's missing match initialization and
source-header prerequisites. Both failed prerequisites remain retained. These
component checks do not establish browser restoration.

A private linker-map inventory records original/generated C static regions and
explicitly excluded host/runtime sections against the final Wasm and producer
graph. It is a candidate range inventory, not complete simulation state. Source
included through project wrappers, dynamic descriptors, stream/file owners and
renderer interactions still need closure. A read-only census now completes
samples 0–299 and the first non-neutral sample 300 in installed headless Chrome.
It preserves all literal PAD records, both selected fighters' positive source
draw dispatches, native PCM, actual Aurora frame completion and owned cleanup.
The earlier sample-zero failure was a harness assertion intended for the
no-draw lane; the reviewed repair retains the strict interactive draw checks.

The next read-only capture adds exact audio/FX allocation ranges with separate
owner lifetime tags. All nine ranges remain stable before the source step,
after it, and after the actual draw and Aurora completion. Independent review
caught and corrected a decoder that incorrectly assumed audio and effects used
the same lifetime counter. Positive controls now use different counters and
negative controls detect a changed effects lifetime. Earlier fixture/export and
cross-realm assertion failures remain retained. The preserved source review has a
transcription-only hash correction addendum; all ten committed source blobs were
rehashed, and the corrected pin matches the original application and capture.

In this capture the source step changes 100,211 byte values: 15,806 are attributed
to audio owners, 2,939 to diagnostic PCM output, 19,299 to the gameplay arena,
3,715 to candidate source statics and 337 to excluded sections; 58,115 remain
unmapped. The later draw/completion transition changes 738,066 byte values.
These are net differences per transition, **not unique mutable state size or a
snapshot budget**. The preceding census and both exact runtime producers remain
retained separately; changes in these totals are not a performance comparison.
Same-value stores and transient changes remain unobserved. Boundary DVD-idle
checks do not exclude transfers that completed between them. Every range remains
unadmitted for restore. The screenshot retains the known magenta/texture
artifacts and establishes no pixel accuracy. Owned browser, server and capture
processes closed, with independent checks of recorded PIDs.

Checkpoint `12f1008e` passes independent source/result review, affected Release
builds, the real-asset fighter/context trace, 2,445 regression tests with 165
skips, and all 14 exact-head CI checks (12 successes, two expected skips). The
later audio-range extension passes focused owner/runner controls, independent
source/result review and its private and ordinary Release builds. Its full regression passes
2,445 tests with 165 skips in 549.587 seconds. Audio checkpoint `a8fa9af1`
completes all 14 exact-head checks: 12 successes and two expected skips. The PR remains draft. No same-instance browser restore
or rollback performance result is claimed.

The subsequent match-context census identifies one exact 2,488-byte allocation
with a stable world and owner epoch at all three boundaries. Eight byte values
change inside it during the source step; none change during draw/completion.
The source step has 89,827 net changed byte values, including 47,785 still
unmapped. These totals belong to this producer; the difference from the prior
census is not a reduction attributable to the eight-byte classification. The
existing PAD, four-stock, selected-draw and Aurora-completion checks pass, and
all nine recorded process IDs are independently absent after cleanup. No memory
is restored.

Independent source review found and repaired an output-alias gap: descriptor or
error buffers overlapping the live owner now refuse before any write. Actual
source controls compare every owner byte after refusal, then perform a healthy
step. The focused fighter trace, 17 runner controls, both Release builds and
independent raw-result review pass. Full regression passes 2,445 tests with
165 skips in 530.297 seconds. Existing failed controls and the rejected candidate remain retained. Next, design a pure
observation accessor for the existing Aurora staging-shadow ownership descriptor;
its current implementation pumps events and cannot be reused unchanged. This is
host-state attribution for no-draw rollback, not a resumption of drawn corrections.

## First current-source copy/restore result

The existing synchronous source-only Mario/FD fixture passes one selected
checkpoint at input sample 300, source frame 176, after Ready at sample 124/frame 0.
Both fighters are observed grounded before the snapshot. One original scripted
input advances to frame 177. Three restores and one-tick replays exactly match
the baseline observation, native PCM and entire linear-memory endpoint. A
one-bit restored RNG perturbation is detected by the unchanged negative control.
The source match/audio/world closes; the recorded Node child exits normally and
is independently absent. There is no browser or server in this experiment.

| Measurement | Observed value |
| --- | --- |
| Whole linear memory copied | 134,217,728 bytes (128 MiB) |
| One capture | 14.646459 ms |
| Three restores | 6.953208 / 3.240584 / 3.080333 ms |
| Three one-tick replays | 0.518792 / 0.488333 / 0.729500 ms |
| Forward baseline step | 0.748375 ms |

These are raw samples on Apple M4/arm64, Darwin 25.6.0, Node 22.23.2. Capture
includes the native quiescence check and full-memory copy; restore includes
memory/stack restoration. Replay is the checked source step, excluding host
comparison/log copies. There is only one capture and three repeat samples;
no p95, generalized performance pass or combined frame-budget result follows.
Historical Node 24 measurements belong to a different cohort. The copied heap
includes immutable and unrelated data: **128 MiB is not the minimum or complete
mutable simulation-state size**.

Executable source is `d78af98c`. The artifact was built from `2f12d0b9` plus the
exact reviewed patch later committed unchanged; the receipt keeps those
identities separate. The affected Release target, compiler layout assertions,
focused controls, and independent source/result review pass. The subsequent
`92c11c2d` snapshot-runner checkpoint passes the full 2,383-test regression
(165 skips) and all 14 CI checks (12 successes, two expected skips). Later
executable changes require affected validation and final-head CI before
integration. The Wasm and harness/source hashes are recorded in the receipt.

## Shared-page ring and seven source-only ticks

A second experiment reuses the existing shared-page snapshot owner on the same
runtime, Node 22 binary and 34 assets. It starts from the same active grounded
sample 300/frame 176. Three restores replay seven original ticks with exact
observation and PCM agreement per tick and exact whole-memory endpoint agreement.
Eight further retained snapshots cover samples 307–314; every restore check,
RNG-bit corruption/recovery control and release/close invalidation passes.
The source/audio/world closes, all retained pages are released, and its recorded
Node child exits and is absent. This is still a fixture with source drawing off.

| Measurement | Observed value |
| --- | --- |
| Eight-state logical snapshot bytes | 1,073,741,824 (1 GiB) |
| Unique retained page bytes | 80,740,352 (77 MiB) |
| First cycle capture, without previous-snapshot hint | 57.489375 ms |
| First ring capture, without hint | 50.969500 ms |
| Seven later ring captures, with hint | 8.432000 ms, then 4.587167–4.854500 ms |
| Three seven-tick replay costs | 1.766582 / 1.623835 / 1.501042 ms |
| Three cycle restores | 7.240250 / 2.633500 / 2.496917 ms |
| Eight ring restores | 2.486250–2.737875 ms |
| Process peak RSS | 537,051,136 bytes |

These are limited samples, not percentiles. Shared pages reduce retained payload;
restore still copies every 64 KiB page. Hinted captures compare all 2048 pages and
hash 25–33 changed pages in these samples. The initial full-memory page scan and
hash/copy capture is much more expensive than the later hinted samples. The
seven-tick timing sums synchronous source steps and excludes source draw, browser
host state and comparisons/logging. **Do not add unrelated timing samples into a
claimed frame-budget result.** Source draw and combined seven-corrective-plus-current
measurement remain open; the seven replayed fixture ticks are seven total.

Harness commit `cbe64e8e`, execution source `92c11c2d`, and the earlier build
producer are distinct. The receipt retains exact hashes and the two-header
source-inventory caveat. Sample 300 observation/PCM matches the first experiment;
its one-tick and this seven-tick memory endpoints are different states and are
not compared with each other. No successful capture was repeated.

## First terminal browser tick and source draw

A private, default-off Release diagnostic reached the original two-human Mario/FD
match through CSS, Rules/Items/Rules Plus and SSS. At frame 188, both keyboard
ports were observed neutral and both fighters grounded with four stocks. The
next ordinary callback returned exactly one original tick and draw at frame 189,
with the observed GPU-submit counter increasing from 3231 to 3232. The terminal
measurement then polled the actual four-port PAD bank, advanced one original tick
to frame 190, ran the complete source draw and CPU finalization, and closed the
owner after real prior-work and cleanup fences. It never restored or resumed the
browser instance. The four-port raw bytes include disconnected-port errors;
unused rows were not replaced with invented neutral input.

| One instrumented sample | Observed value |
| --- | --- |
| Original tick | 0.215 ms |
| Begin frame | 0.130 ms |
| Source draw | 1.900 ms |
| CPU finalization | 1.590 ms |
| Native elapsed total | 3.935 ms |
| Nested WebGPU observer interval | 3.875 ms |
| GPU submits / encoder finishes / maps | 0 / 0 / 0 |
| GPU object creation | One command encoder, two bind groups |
| Queue writes | Four buffer writes, 1,220,880 bytes |
| Prior-work / cleanup fence | 0.320 / 0.285 ms, both resolved |

The intervals differ and include instrumentation; this is not uninstrumented
CPU cost. GPU writes and object creation still happen without a submission, so
omitting Submit does not make heap rewind safe. Native PCM evolution, audio/file
operation counts, same-instance restore, seven corrective ticks plus current,
and combined frame-budget cost remain unmeasured. Empty source-menu observations
are recorded as unknown; the actual match observations are retained separately.
The browser's reserved heap is 278,396,928 bytes at this boundary; that is neither
a copied snapshot nor the minimum simulation-state size.

The current H1 export contains no timing incidents and no observed audio
underruns/overflows; history eviction and missing embedded build identity remain
explicit. Independently verified served hashes bind the producer. Native
close/input/Aurora retirement and zero source-owner/cache checks passed; all 12
recorded PIDs and three process groups are independently absent. The retained
pre-diagnostic screenshot shows visible FD magenta/texture artifacts and does
not establish pixel accuracy. Independent source and raw-result reviews pass.

Producer `e8ba53f4` plus patch `128f2918` is distinct from its eventual commit.
The private Wasm is `ca043e5a`; the default-off runtime omits all four probe
exports. Both affected Release builds pass. The unchanged assembled executable
source passes 2,388 tests with 165 skips, including actual retirement-callback
and extracted-native failure/ordering controls. Executable checkpoint `ee3d724a` passes all 14 exact-head CI checks (12 successes,
two expected skips). Later documentation heads still require their own checks.
The structured receipt retains complete hashes, raw PAD, effects and cleanup.

## Feasibility recommendation and stopping point

**No-go for naive live-browser heap rewind or a current seven-corrective-tick
budget claim. Continue the match-only direction with explicit state and host
ownership under #115.** This is not evidence that rollback is impossible or too
slow. The measurements above answer the admitted component questions; minimum
complete mutable state size, browser restore and the combined frame budget are
still unknown.

The proposed next renderer gate stopped before implementation or another capture.
Source inspection found content effects inside `RenderPass`, beyond top-level
packet upload/copy counts: palette conversions, attachment loads, resolves,
color/depth snapshots and cached offscreen outputs. CPU packet reset does not
restore those GPU contents. A later frame could consume a cache handle whose
producing commands were discarded. This establishes possible source paths, not
that the observed Mario/FD sample exercised every one. Rejecting every drawn pass
would make the proposed test useless; admitting a useful path requires content
and owner classification beyond this bounded repair. No seven-frame run occurred.

Before another drawn multi-frame measurement, classify the actual match output
owners and prove a safe end-to-begin transition, including load dependencies,
cache content, callbacks and exactly-once lease return. Keep the private terminal
diagnostic one-shot. Do not expand this feasibility checkpoint into a broad
renderer transaction engine simply to obtain a timing number.

| Host owner | Source finding and proposed policy | Remaining evidence |
| --- | --- | --- |
| Simulation / allocation | Restore complete original mutable state, PAD history, RNG, draw-dependent state and allocator ownership at a quiescent boundary; retain exact immutable asset identities. | Complete mutable ranges, nonmemory globals/tables and browser-safe closure are unknown. |
| GPU / renderer | Preserve full source draw; distinguish immutable resources from speculative target/cache contents, writes, staging and completion ownership. Version or discard speculative effects and commit intended presentation once. | One terminal sample measured creates/writes; reusable content rollback and readback semantics are unproved. |
| Native audio / browser output | Original SFX commands precede later voice and PCM advancement. Native audio and the AudioWorklet have separate clocks. Preserve source progression and define confirmed/predicted output policy; consumed sound cannot be undone. | No per-tick SFX/voice/PCM count or browser audio rollback validation. |
| Files / banks / streams | Native requests cross into JavaScript Promise/result maps. HPS decoded slots and SSM bank mappings have separate owners. Retain immutable bytes and explicit timeline/request/completion identities before restoring destinations. | Stale callback rejection, asynchronous closure and source latency/order remain unproved. |
| Input / outcome / persistence | Keep transport, live devices and wall clocks outside rewind; replay exact logged inputs. Commit confirmed outcome and persistent writes once outside speculative work. | Match start agreement and actual reachable persistent effects still need validation. |

These are source-backed policies, not implemented rollback safety. The retained
11-source inventory is historical; its hashes were not rewritten. A later
lineage-aware review identifies the existing pre-close `source_file_drive_busy`
request-count guard, which does not prove pumping/region, JS Promise, native
HPS/SSM/audio or GPU closure. A separate source/compile-database review follows
original `ft_PlaySFX` through `lbAudioAx_800237A8` and `AXDriver_8038CFF4` to the
later synth callback; the unused `melee_web_audio_play` helper is not a gameplay
SFX observation seam. Browser audio rendering invokes the synth callback on its
own sample cadence, separately from match ticks. This is configured-source
inspection, not final-link retention or a runtime call count. Exact review and
source identities are in the structured receipt.

The recommendation is to retain these controls, keep A4 blocked on a concrete
state/host-ownership design and the Slippi profile gates, and move independent
engineering to the requested H1 first-use respawn/common-effect preparation
investigation. H1 keeps every timing guard and source tick/RNG/order invariant.
Stadium C3 remains next Track B. Source-only Slippi semantic/license/reference
work continues under #9; the complete profile and its reference agreement remain open. Original comparison,
physical input, foreground timing, audible output and two-machine acceptance
remain separate unrun gates.

## Retained preparation failures

The first current target build failed at a C/C++ `GObj_Create` linkage mismatch.
A direct replay of the single failed link preserved the diagnostic. Moving the
source declarations before their transitive C++ include required reusing the
existing HSD compatibility shim; failed intermediate syntax attempts exposed
`ssize_t` and duplicate typedef conflicts and remain retained. Actual Wasm
layout is now compile-checked: match stats 192 bytes, observation 1280 bytes, with
explicit field/PAD offsets. The checker refuses an incompatible layout and
invalid or partial checkpoint/depth selection. The default full-fixture matrix
and its strict original assertions remain unchanged.

## October 17 owner pivot: no-draw correction ticks

The paused drawn ownership results in draft #324 remain historical component evidence.
Their proposed next drawn restore/repeat is paused by the owner's new decision;
October 16 sprint items 2–9 are not an active prerequisite. The next milestone
combines Slippi-profile render-to-simulation fixes with a strict drawn/no-draw
re-simulation synctest. No new synctest, performance or profile pass is claimed
by this decision. Vanilla drawing and its original comparison gates stay intact.

Profile implementation is now authorized in the isolated GPL-3.0-or-later scope;
complete semantics and independent reference validation remain open. The
[architects' log](../ARCHITECTS.md) and [roadmap](../ROADMAP.md) record the license,
unofficial naming, own-relay-only service policy and indefinitely parked A5.

## First no-draw profile comparison

The isolated **`slippi-render-dependencies-v1`** diagnostic implements our own
C versions of the DynamicsFix matrix refresh and BrawlOffscreenDamage predicate.
It does not enable a player toggle or identify a complete online profile.
Vanilla retains its existing source/build path. The
[profile boundary and source mappings](../../src/slippi_profile/README.md)
record the remaining dependency inventory.

One headless Chrome run executed 301 original source draws. Its 300 setup/neutral
prefix samples and one subsequent input sample matched the no-draw Node lane exactly in
all 1,280 declared state bytes, 4,272 native PCM bytes and 44 literal PAD bytes.
The Node lane restored its 128 MiB checkpoint, repeated that one tick without
source draw, and reproduced the exact observation/PCM/PAD and full Node heap
endpoint. The browser and its GPU heap were never restored. RNG and raw-input
single-bit controls were detected, and recovery returned to the exact endpoint.
The report's `corruptedLowByte` is the RNG byte **after** the corrupted step;
the frozen source performs the single-bit change before it.

Native shutdown, browser/server closure and independently attributed owned-process
absence passed. The first-frame screenshot shows FD's background/starfield;
it supplies no gameplay pixel acceptance. The two earlier initialization failures
(SDL's read-only runtime property and missing cache directory) and their exact
runtimes remain retained. The repairs export the actual Emscripten helpers and
reuse normal cache preparation; native errors and comparisons remain strict.

This is **Browser exercised / declared-state, PCM and raw-input component
evidence**. It does not demonstrate the new offscreen and matrix predicates in
combat, complete state or host ownership, original Slippi reference agreement,
other fighters/stages, live rollback or a seven-correction frame budget. Those
boundaries remain open. The existing structured receipt's
`no_draw_profile_synctest_v1` records the dirty source based on `65e6c78e`, exact
runtime/harness/evidence hashes, failures and validation; capture identity must
not be replaced by a later documentation or integration commit. This precursor
ran on the paused draft branch; its full regression retained one ordinary
source-preparation call-shape failure. The focused mainline extraction restores
that existing call shape and excludes the paused renderer-ownership changes.

The fresh mainline candidate, based on `95113326`, separately passed the same
bounded comparison with its own linked runtimes: 82 pre/post source, runtime,
asset and tool pins agree, all declared state/PCM/PAD comparisons and corruption
controls pass, and owned cleanup passes. Independent source/raw-result review
passes for this bounded component. The Node heap hashes differ from the
precursor and remain bound to this build. Both profile and ordinary Release
builds pass; the actual ordinary Ninja graph excludes the profile sources and
define. The first fresh-checkout full regression retained three missing-default-
trace-build failures (two test failures and one setup error). The normal default
configuration and focused trace checks then passed, followed by the complete
2,412-test regression with 166 skips and no failures. This supplied the missing
environment prerequisite without source or assertion changes. The first exact-head
CI run passed every test/build partition but the aggregate rejected two imported
profile test identities as belonging to another shard. The strict audit remains
unchanged. Empty forwarding classes in the behavior-free discovery shim retain
all ten original profile test methods exactly once; an eleventh GPL control
checks actual discovery, shard assignment and aggregate acceptance/rejection.
Independent review and focused controls pass. The final regression passed 2,413
tests with 166 skips and no failures. All 14 exact-head CI checks on `bf996e69`
completed with 12 successes and two expected skips, including the strict
aggregate. PR #328 merged as `0aea806b`; its tree exactly matches the reviewed
head. Runtime/harness files and retained capture identities are unchanged.
The structured receipt keeps this result under `mainline_extraction`, separately
from the historical capture. The v1 runtime bytes were explicitly retained
before starting the next profile revision.

## Render-dependency coverage follow-on

The next revision keeps the initial browser comparison separate from path-specific
native controls. The actual offscreen timer block now has a reviewed reduced
fixture for its outer gate, threshold, damage request and reset. It composes the
ordinary source patch and isolated profile overlay, preserving authored types and
source guards. The fixture uses explicit synthetic bounds, timer values and damage
services. Its KO exclusions are checked against the independent authored action
enum; deliberately changing the Star-KO exclusion causes the expected failure.
This covers the extracted block, not full damage semantics or browser combat.

A separate original-source DeadUpFall reduction demonstrates why another profile
fix is needed. With a synthetic translated camera view, the original draw helper
writes a different fighter position; the actual next-tick position consumer and
terminal callback consequently differ from the omitted-draw path. Identity-view
and draw-guard controls pass. This is a causal native source counterexample,
not a full KO or original Slippi runtime comparison.

The three own-C hooks now pass independently reviewed native controls under the
distinct `slippi-render-dependencies-v2` identity. They initialize private fall
velocity, preserve its ordered update and transform only a temporary model
position during draw. Skipped, repeated and changed-view draws agree on the
fixture's declared simulation bytes. Removing each hook separately fails its
specific invariant. The focused cohort passes 13 Python and nine nested Node
controls. Both the opt-in profile and ordinary Release builds pass. The full
regression passes 2,415 tests with 166 skips. Source and runtime identities
are preserved in the validation freeze.
The first PR #330 CI run exposed a Linux-only compilation failure in two native
test fixtures: the upstream `__fabs` macro was active before glibc declared its
math functions. Moving `<math.h>` before upstream headers fixes the include
boundary without changing arithmetic, controls or warning flags. Both affected
controls pass locally. All 14 exact-head checks on `1a0e5bfd` completed with
12 successes and two expected skips (run `39109121768`). PR #330 merged as
`310e3309`; the merged tree matches the reviewed head. The failed job log and
its test report are retained.

Review also caught an incorrect extra X-velocity update despite an earlier green
fixture. Retained retail instructions show that the reference hook replaces the
entire original velocity accumulation. The corrected implementation preserves
existing temporary X and uses the authored Z operand order. Nonzero X sentinels,
signed-zero bits, clamp and float store/reload controls now cover those boundaries.
The earlier candidate and its self-consistent but incorrect expectations remain
retained; their pass is not used as reference-behavior validation.

The structured receipt's `render_dependency_coverage_v2` retains the source pins,
raw results, independent reviews and fixture failures. The first offscreen mutant
attempt failed to compile because an adjacent header was missing; only the later
executed mutant supplies the falsification result. Generator failures and the
reduced vector-expectation failure are also retained. The native fixture uses
synthetic Fighter/JObj projections and service adapters; it does not establish
live KO reachability or complete state-machine behavior. Complete render-dependency
and host-state closure, live rollback, original Slippi reference and acceptance
remain open. The v1 browser witness above is not relabeled as v2 validation.


Issue #331 adds a two-tick comparison with actual fighter callback and normal
model-dispatch counters. Its first capture stopped at the sample-300 attribution
guard: the recovered `controller_index` field is costume tint, not the input
controller. Both fighters correctly had tint zero. The 300-sample prefix matched;
offline comparison of the retained sample-300 raw state, PCM and PAD also matches.
Both fighters' callback and normal model-dispatch counters advanced. Sample 301,
restore and corruption controls remain unrun, so the capture remains failed.
Native/browser/server cleanup and owned-process absence passed. The next repair
records the actual processed PAD slot and checks its exact mapping through the
existing constructor and copied initializer; gameplay input routing is unchanged.
The structured receipt retains the failed runtime/harness and bounded diagnosis.

The corrected second capture **passes the two-tick component** on the v2 profile.
After the same 300-sample prefix, input samples 300 and 301 (source frames 177
and 178) match all 1,280 declared state bytes, 4,272 PCM capacity bytes and
44 raw PAD bytes. Both fighters advance the source callback counter by five and
normal model-dispatch return counter by three on each tick. Their actual source
slots, processed PAD slots and copied raw controllers map exactly to 0/1.
The 128 MiB Node-only checkpoint restores exactly, replays both ticks to the
same heap endpoint and recovers after the detected RNG/PAD corruption controls.
The browser heap is never restored. Actual PCM production is 533 stereo frames
on each target tick, with phases 20 and 40; capacity bytes are not frame counts.
Owned renderer/browser/server cleanup and process absence pass.

The special model-transform count is zero, so this does not exercise the live
KO branch or establish offscreen/dynamics path coverage. Screenshots show the
checkpoint and endpoint but supply no pixel-accuracy claim. Complete simulation
and host-state ownership, original Slippi parity, seven-tick performance and
online rollback remain open. The structured receipt pins the corrected source,
runtime, harness, raw evidence and retained failures. Independent raw-result review passed. Pre-integration review caught stale field
references in a native test fixture; four test-only lines were corrected and
its focused causal/omission controls pass. The interrupted regression and native
compile failure are retained. Runtime/capture source is unchanged. The final
full regression passes 2,416 tests with 166 skips in 505.482 seconds; the log
identity is in the structured receipt. PR #332 merged as `706fa1bb` after all
14 checks on reviewed head `86e00ca2` completed (12 successes, two expected
skips). The merged tree exactly matches the independently reviewed tree.
Capture, runtime and harness identities above remain distinct from integration.

## Stage decision dependencies, issue #333

The next incomplete profile revision, `slippi-render-dependencies-v3`, adds two
independent source hooks from the pinned Slippi behavior inventory. Whispy's
shared direction helper still queries each eligible fighter in original order,
but a fighter in the authored signed motion-state range through Sleep contributes
zero. The existing tie RNG remains. Stadium still runs its original tracking
helper and writes camera-specific presentation coordinates; only its transition
decision uses the selected fighter's post-helper position and the authored
inclusive rectangle. The selected fighter pointer is captured before that helper.

The source and retail-instruction mapping review passed. The first overlay
composition failures and the subsequent Whispy fixture failures are retained;
no native reducer or browser success is inferred from those attempts. The repaired
actual-source controls now pass independent review: the original and omitted-hook
lanes diverge, while each profile decision agrees across its paired lanes. Whispy
preserves query order and the existing tie RNG. Stadium preserves view-specific
wrapper coordinates while its decision and ordered transition RNG trace agree.
The projector, bone, RNG inputs and downstream mode-setter observer are explicit
synthetic services; this is not live stage execution. Both affected Release builds
succeeded; the private build lost its raw tool-exit field, so its success is bound
to fresh outputs and the post-build success journal. Full regression passed
2,418 tests with 166 skips in 544.101 seconds. All 15 reviewed source pins matched
and 13 prior runtime artifacts were preserved. The ordinary Wasm changed despite
unchanged JavaScript, data and CMake cache. Independent analysis accounts for every
difference: SDL embedded the parent repository revision, moving its string and
relocating references. No unexplained instruction/data difference remains; this
is artifact attribution, not gameplay equivalence. The current manifest retains
24 entries covering 16 distinct artifacts alongside the prior products.
PR #334 merged as `ecb2fe99` after all 14 checks on reviewed head `34a0c57b`
completed (12 successes, two expected skips); merged and reviewed trees match.
Historical v1/v2 captures retain their original identities and
do not validate these new hooks. The structured receipt records the current
candidate and its limits.

## Slippi profile source checkpoint

The [profile inventory](../investigations/SLIPPI_GAMEPLAY_PROFILE_INVENTORY.md)
and [source receipt](../evidence/slippi-gameplay-profile-inventory-v1.json) map
the selected configuration, mode behavior, source descriptions and repository
notices. All 156 unique untagged active ASM hook bodies now have bounded content
review: the 36-path reviewed baseline, 52 disjoint bodies in the semantic-
escalation packet, and the exact remaining 68 bodies. This closes hook-body
accounting only; original branch/continuation behavior, deeper callees, opaque
binaries and runtime/profile closure remain open. The separate 27 tagged paths
are not included in that count, and the current tagged behavior candidate has
own descriptions for 25 of them.

The reviewed sources distinguish Stadium transformation loading through the
synchronous EXI `File_Read` path from the separate frozen-stage toggle predicate.
`DynamicsFix` updates dynamic transforms that rendering normally refreshes when
fast-forward skips drawing. The Final Destination background hook saves and
restores RNG around the original thinker. Eight UCF 0.84 payloads have pinned,
static big-endian PPC decodes; none was executed and per-player activation is
unproven. Recording `SendGameInfo` allocates two GObjs/processes at priorities 0
and 15; its character/costume corrections apply to copied replay metadata, and
its hardcoded UCF=1 bytes do not prove live activation. Recording `SendGameEnd`
calls the original result builder midmatch. Its extra `ODB_IS_GAME_OVER` guard
applies only to online in-game end ID 2 (GAME); other nonzero ending IDs bypass
that guard. The builder's match-kind-3 score path calls `pl_80039450`, which
clears and writes live bonus-table data. Standard stock scoring avoids that
branch; the seven deeper bonus routines remain open.

This historical inventory used no real Slippi services or accounts. The October
17 owner decision parks official-service cross-play indefinitely and authorizes
the isolated GPL-3.0-or-later implementation scope described above. The pinned
Slippi ASM source carries GPLv3 text; Dolphin/EXI notices remain separate.
Other open boundaries include original hook branches/continuations, UCF
predicates and private-history lifetime, opaque `Binary/m-ex.bin` provenance,
`FreezeGlitchFix`, recommended FoD/XFB behavior, source-to-generated bundle
pairing and backend activation. No code was ported by that inventory checkpoint;
its source review does not establish online match activation, release
executable correspondence, peer agreement or profile acceptance.
