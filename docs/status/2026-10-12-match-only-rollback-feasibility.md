# Match-only rollback feasibility

**Compiled / Source-only snapshot diagnostic / Terminal browser tick-and-draw diagnostic / Browser rollback and frame-budget feasibility unproven**

[Milestone #318](https://github.com/ericvanlare/melee-web/issues/318) and
[merged #319](https://github.com/ericvanlare/melee-web/pull/319) implement the
owner's match-only direction. Original menus, transitions, Results and rematch
remain local. The planned mode choice is Vanilla offline-only or Slippi rules
offline/online, with distinct pinned references; the toggle/profile are not yet
implemented. The [structured receipt](../evidence/match-only-rollback-feasibility-v1.json)
binds current producer and retained failure identities.

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

The recommendation is to retain these controls and keep A4 blocked on a concrete
state/host-ownership design and the Slippi profile gates. The owner-approved
October 13 sprint continues the smallest drawn-browser checkpoint/restore/repeat
boundary under #115/#318. The H1 first-use respawn preparation component merged
in #322; broader H1 keeps every timing guard and source tick/RNG/order invariant.
Stadium C3 proceeds in parallel as the next Track B milestone. Source-only Slippi
semantic/license/reference work continues under #9; the profile is not ready to port. Original comparison,
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

## Drawn synctest sprint checkpoint

The October 13 sprint now has one successful normal submitted Mario/FD callback
observation. The private observer and ordinary Release builds pass, as does the
exact-source 2,402-test regression (164 skips). Producer and tested source are
`a34d5acf` plus patch `c50aa00b`; the structured receipt binds the new artifacts
and independent raw-result review. The earlier runtime patch `e932d6e2` and
harness/regression patch `e3887312` remain historical identities with their
retained failures.

The first capture failed before the observer was armed: after original CSS
changed P2 from CPU through empty to Human, one Start press did not enter SSS.
There were zero match observations. The pre-Start screenshot still has P2 faded;
the later failure screenshot shows READY TO FIGHT. Source inspection identifies
a readiness/cooldown gate, so an early Start is the current hypothesis, not a
confirmed input-delivery diagnosis. The next harness change reuses the existing
full CSS observer and waits for original readiness before one Start edge; it
changes no game state, timing threshold or deadline. No unchanged retry is
admitted. Native cleanup passed, all recorded Chrome/server processes are absent,
and served runtime/source identities are unchanged. The H1 recorder and failed
packet remain retained; no match timing or renderer-effect result follows.

The changed-hypothesis second capture passed that readiness gate with one Start
edge and reached the normalized Mario/FD match at source frame 180. It then
exposed a private-observer scheduling error: the next browser callback had zero
source ticks/draws while audio rendered normally. The observer had activated
before the source clock decided whether a tick was due, so its strict one-tick
check failed. No renderer events or consumed PAD records were collected; the
zero-filled diagnostic PAD buffer is not input evidence. The callback recorder
passed its checks with no timing pause, checked native retirement passed, and
all owned browser/server processes exited. That retained failure drove an
actual-source zero-step-then-due-step fixture and a reviewed repair that activates
the observer at the first tick selected by the unchanged game clock. Pending
owner/readiness changes and a stalled or multi-step first due callback still
fail terminally; original source-free audio callbacks remain unchanged.

The published observation checkpoint is `d68fd8b1`. Its first CI attempt failed only in the new native source-file fixture: strict C11 on Linux did not expose the POSIX `strnlen` declaration. The reviewed test-only compiler-flag repair is `99e6838f`; both focused methods pass, and all 14 checks on that head completed with 12 successes and two expected skips (run `38471800942`). Runtime and capture producers below remain unchanged. Draft #324 remains open for the restore/repeat milestone.

The third capture passed: source frame 180 advanced to 181 with one original
tick, one complete draw and one audio return of 533 PCM frames. Its actual
four-port PAD bytes retain disconnected-port errors. The renderer recorded
three operations/render passes, 246 draw commands, two resolves, one snapshot,
12 staging copies and four queue writes totaling 1,231,440 bytes. One normal
submission and its fence completed; checked native retirement and owned browser/
server absence passed. The observation kept **LOAD_ATTACHMENT (blocker 7)**,
so these events do not establish safe GPU-content or handle restoration.
The successful browser run had zero pre-tick callbacks; zero-source callback
skipping is covered by the actual-source fixture, not this browser sample.

There were no forced timing pauses in this route. The H1 recorder retained eight
reason-code-7 preparation incidents, zero dropped incidents and 3,029 evicted
samples; it is a lossy setup-and-match record. Audio-frame counting does not
establish PCM equality or a file-operation inventory. Snapshot/restore, renderer
ownership closure and the combined corrective-tick budget remain unproven.

A separate actual-source HPS/inner-I/O control now passes the idle-owner,
wrong-owner, queued-request and active-callback checks under the configured
Emscripten Release ABI. Its deliberate CHECK failure also reaches C and fails as
expected. Earlier host-ABI, callback-signature and environment-sentinel test
failures are retained. Fixture collaborators isolate audio identity and source
lifecycle; this does not establish decoded audio, browser file closure or restore.

This continues the approved same-instance draw/restore/repeat ownership work.
No browser restore, complete state-size result or combined correction budget is
claimed. Stadium C3 remains a separate parallel milestone.

### October 14 ownership and schema-2 checkpoint

The resumed sprint retains the earlier successful schema-1 observation above.
New native audio/file guards, PCM journals and JavaScript unfinished-read
accounting are reviewed component work. Their private profile still refuses
pair admission: no actual returned-boundary coordinator or browser restore is
implemented. The renderer now serializes complete schema-2 event, resource,
binding and draw rows with exact integer and floating-point representations.
Strict decoding and corruption controls pass. All 11 configured diagnostic-off
preprocessor outputs match the prior renderer source; this is an off-projection
check, not runtime equivalence or GPU-content closure.

At runtime producer `a557e9ce`, the ordinary, frame-effect and combined private
Release builds pass. The full 2,425-test regression (164 skips) failed two
producer/consumer source-fingerprint inventory checks. The minimal metadata
repair `bf59a893` passes all 56 affected checks. That focused pass does not replace
the final full regression or exact-head CI required before integration. The
runtime and capture producer remain distinct from the metadata-only harness.

One schema-2 browser attempt used the retained original route and unchanged
clock. After arming at source frame 180, its next naturally due callback ran two
ticks and two draws to frame 182, plus one audio return of 534 PCM frames. The
explicit single-tick admission guard refused before the renderer observer
armed. Empty renderer rows therefore establish no resource ownership result.
The first error is retained; default-zero planned-step fields are not observed
clock decisions. Native retirement, owned browser/server cleanup and unchanged
source/runtime checks pass. H1 recorded no timing pause and eight preparation
incidents; its history remains lossy. No unchanged retry is authorized.

The reduced actual-source admission control passes for zero, one and two
planned ticks, including exact tick/draw/audio ordering and terminal retirement.
The next step is a separately reviewed first-draw observation that preserves
every naturally due source action.
Any such observation must remain distinct from single-tick restore admission.
The Wasm-instance ownership helper and private generated-glue hook are committed
locally as `2d9e2927`. The reduced actual Emscripten fixture preserves prior
preRun callbacks, observes all four Asyncify writer calls and rejects unsupported
setup before main. The maintained helper, glue and packaging checks pass all
46 affected tests. The first fixture failure, a missing canonical Wasm-table
JavaScript dependency, remains retained; the explicit dependency repair passed
the second bounded attempt. No gameplay private-profile build or restore result
follows from these controls; GPU, audio/file and output ownership remain open. No snapshot/restore, seven-tick
combined budget, human-input or two-computer acceptance is claimed.

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

Cross-play, gateway/services and frame-cadence acceptance are deferred; no real
Slippi services or accounts were used. The pinned Slippi ASM source carries a
GPL-3.0 notice, and Dolphin/EXI licensing is separate. The owner's GPL
distribution question remains pending; repository notices are not clearance.
Other open boundaries include original hook branches/continuations, UCF
predicates and private-history lifetime, opaque `Binary/m-ex.bin` provenance,
`FreezeGlitchFix`, recommended FoD/XFB behavior, source-to-generated bundle
pairing and backend activation. No code or profile was ported, and this
source-only checkpoint does not establish online match activation, release
executable correspondence, peer agreement or profile acceptance.
