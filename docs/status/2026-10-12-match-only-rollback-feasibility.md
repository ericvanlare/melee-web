# Match-only rollback feasibility

**Compiled / Source-only snapshot diagnostic / Terminal browser tick-and-draw diagnostic / No-disc native initialization control / Browser rollback and frame-budget feasibility unproven**

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

### October 14 first-draw observation

At runtime and harness producer `fa58cb7b`, all three Release builds pass
(ordinary/frame-effect/combined private artifacts: 44/44/45). The full regression
runs 2,429 tests with 164 skips and no failures. The 52 focused checks and four
changed Aurora translation units' diagnostic-off projection also pass. A fresh
static inventory binds each profile's actual JS/Wasm exports; it does not prove
runtime ownership or safe hidden-state restoration. Earlier failures and their
producer identities remain retained above and in the linked evidence JSON.

One reviewed headless browser attempt collects the first actual draw while
preserving the normal callback's source decisions. This sample naturally ran
one tick and one draw, source frame 180 → 181, with two audio returns and 1,067
PCM frames. It records 1,763 events, 193 handles, 105 resource rows, 1,104 binding
entries and 243 draw rows. One normal submission and its fence complete. Native
retirement and owned browser/server cleanup pass. No depth request occurred;
zero selected map owners become finalized at retirement. Multi-tick callbacks
and pending depth-map completion remain fixture coverage, not this browser result.

The historical collection found `UNKNOWN_RESOURCE` (18) first, together with
`LOAD_ATTACHMENT` (7) and `COMMAND_SHAPE_MISMATCH` (27). Its first unknown was a
static bind-group descriptor created before observation was armed. That failed
ownership result remains tied to `fa58cb7b`.

The reviewed follow-up at producer `410e9dd9` retains actual creation identities
and descriptors for the shared/staging buffers, static/uniform/empty groups,
layouts and selected empty sampler. All three Release builds and the full
2,429-test regression pass, with 164 skips; all 133 build artifacts were checked.
One changed-source browser observation passes at source frame 180 → 181:
one tick, one draw, one audio return and 534 PCM frames. The current outer/renderer
schemas are 4/3. It retains 1,783 events, 196 handles, 119 resource rows, 1,123
binding entries, 19 layout entries, 14 creation-provenance rows and 246 draw rows.
One normal submission/fence completes, the native observer retires, and all ten
owned browser processes plus capture/server groups are absent.

**Renderer restore remains blocked.** The `410e9dd9` browser report still records
`LOAD_ATTACHMENT` (7) first and `UNKNOWN_RESOURCE` (18); those findings belong to
that captured producer. A later source/test component at `8c20a0da` adds
creation provenance for the three persistent EFB owners (color, resolved color,
and depth): six texture/view rows retain their original descriptors, parent
identity and creation serial. Its observer-collector fixture exercises the
metadata path through a LOAD pass with detached test handles, verifies the six
resource/provenance rows and keeps `LOAD_ATTACHMENT` as the first blocker. This
is component-only evidence: the browser observation predates the change, and no
real GPU attachment bytes were captured or restored. Other texture/view owners
and attachment contents remain unresolved; a LOAD operation does not establish
the content that must be restored. Historical shape finding 27 is absent from
that callback, whose draw count differs; that is not evidence of a shape repair
or equality with the earlier run.

The current H1 recorder has zero forced timing pauses, eight preparation
incidents and no dropped incidents, but retains only 100 of 3,128 callback
samples. This lossy history does not establish timing acceptance. The historical
`fa58cb7b` run retained 100 of 3,120 samples and its separate audio totals remain
unchanged. Neither result demonstrates a restore/repeat pair, PCM/pixel equality,
a seven-tick corrective budget or two-computer acceptance. The evidence JSON
retains exact build, capture, review and cleanup identities. Draft #324 remains
open for the drawn restore/repeat milestone.

The first documentation head `c8095351` failed repository-content CI because a
private helper variable named `apiKey` held a public Module property name. The
reviewed repair renames only its three local references to `accessorPropertyName`;
the fixed property string and all scanner rules remain unchanged. The complete
tracked-content check and both existing Wasm ownership controls pass after the
repair. This does not relabel the `fa58cb7b` build or browser evidence.

### October 14 private-Wasm no-disc boot and 13922 validation checkpoint

At validated source `13922b8d` (tree `14923787`), the ordinary, frame-effect
and combined first-pair Release builds pass with 44, 44 and 46 inventoried
artifacts. The full regression ran 2,431 tests: 2,267 passed and 164 were skipped, with no
failures; the independent review verifies 134 emitted artifacts. A fresh static inventory binds the
46-file private profile and its 15,980,997-byte Wasm. This is static binary and
artifact evidence, not runtime hidden-state or restoration closure.

The initial private-profile controls passed: the compiled private hook reaches a
native callback. A missing copied helper refuses before the loader starts; a
missing compiled hook refuses before native main. The separate ordinary-only attempt in the retained v1
packet timed out after 90 seconds. Its review notes the path-only
`route.fulfill({path})` harness response omitted the real server's isolation
headers; because that attempt did not capture an isolated-status snapshot, this
remains a harness failure with a source-supported explanation, not a runtime
defect diagnosis. The reduced v3 control preserved the real loopback response
headers and passed: COOP is `same-origin`, COEP is `require-corp`,
`crossOriginIsolated` and `Module` are present, and the ordinary native callback
arrives at 70.72 ms. All 38 requested responses match the ordinary artifact
inventory; the disc input stays empty, the private accessor is absent, and
runtime, page, console and route errors are zero. CDP attribution and owned
browser/server/capture cleanup pass, with the checkout unchanged. These receipts
bind `13922b8d` only. The later `fb63c3f2` renderer producer has its own
validation and observation below.

This proves ordinary no-disc native initialization and the listed private
startup refusal controls only. It does not import a disc, reach CSS/SSS or
Gameplay, prove runtime Wasm ownership, or establish a restore/repeat pair,
visual/audio equality or whole-session acceptance.

### October 14 texture ownership and actual clear-value observation

Executable `fb63c3f2` adds copied creation identities for actual TextureRef
factories/replacement and canonical snapshot/offscreen owners, plus exact
BeginRenderPass clear values. The focused real collector, 17 current/historical
JavaScript controls, native serialization, 11 translation units and 10 ordinary
OFF compiler projections pass. Two reduced fixture failures remain retained:
device-mismatch classification and a zero-ID capacity check. Their narrow repairs
preserve the original refusal and capacity requirements.

All three affected Release builds and the full 2,431-test regression pass with
164 skips. Independent review rehashes all 134 artifacts and confirms the exact
source/Aurora patch trees and owned-process absence. One new browser observation
then reaches original CSS → SSS → the four-stock Mario/FD match and observes
source frame 180 → 181 with one original tick, draw and audio render. The audio
counter advances 533 frames; this is not a PCM comparison. The strict outer-6 /
renderer-5 receipt contains 1,755 events, 266 handles, 265 resource descriptors,
160 creation-provenance rows and six actual attachment clear-value records.
Normal submit/fence and checked native retirement pass; all nine owned browser
PIDs and the capture/server PIDs, across three groups, are absent.

`UNKNOWN_RESOURCE` (18) is absent in this selected frame. `LOAD_ATTACHMENT` (7)
remains first, and `COMMAND_SHAPE_MISMATCH` (27) is retained: this callback has
261 command records, 242 draws and three passes, against the diagnostic's fixed
265/246/3 shape. This differs from the historical `410e9dd9` callback and does
not establish a rendering regression, shape equivalence or all-owner coverage.
Only roles 1–18 were exercised; no snapshot/offscreen-role coverage is inferred.
Exact clear values and descriptors do not establish prior texture contents.
The next step traces the first actual LOAD/sample dependency in this retained
receipt to an exact earlier definition or an explicit unknown starting input.
No additional capture, texture-content restore or same-instance replay occurred.

H1 retains eight preparation incidents and zero forced timing pauses or dropped
incidents. Its sample history is incomplete: 100 samples remain from 3,133
callbacks, with 3,033 samples evicted. This observation does not close live timing,
pixel/PCM equality, complete host ownership or rollback/frame-budget acceptance.

On October 15, a separate one-texture guard component passed on executable
`5cdcd2a7` (tree `a5f3fa6c`). The real-GPU probe used Apple Metal 3 on Apple M4
with a nonfallback adapter. Two 64-byte readbacks matched exactly; all 17
expected refusal cases covered writes, aliases, copies, destruction, mapping
changes and owner detach. All 30 recorded process IDs and both owned groups
were absent. The first isolated build at `a6cfbcba` failed because the archive
link did not extract its EM_JS bridge; the minimal executable-source link repair
and the original failed output remain separately identified in the receipt.

All three required Release builds and the full 2,435-test regression pass with
164 skips on `5cdcd2a7`. Independent review verifies 134 emitted artifacts,
source and patch identities, guard exclusion from the existing runtime profiles,
and owned-process absence. The updated draft head still requires its own CI.

This is component evidence only: gameplay arming remains refused. It does not
restore Wasm, resolve `LOAD_ATTACHMENT`, or prove complete native/GPU ownership.
Native final-release/import-alias bookkeeping, pending work, audio/file state and
full reference lifetime remain open. Exact component, retained failure and
validation identities are under `readonly_gpu_target_component` in the
[existing structured receipt](../evidence/match-only-rollback-feasibility-v1.json).

On October 15, the selected-sampled-owner native diagnostic component was
integrated at source `c89bae3d` (tree `6c9f1158`), with reviewed Aurora tree
`4be066f4`. The focused candidate controls passed: native observer controls,
actual GX translation-unit syntax with the diagnostic enabled, and
byte-identical ordinary/OFF preprocessing. The diagnostic remains OFF in
ordinary profiles. The root review rehashed 3,566 inputs, all five command log
pairs, and confirmed all five process groups absent. The checks cover selection
of the next sampled candidate and its owner/serial/epoch/seal validation; GX
slot reborrow is source/TU evidence only.

This is a native diagnostic/source component, not a real GX scene, browser
capture, or quiescence result. It uses diagnostic identities; gameplay arming
remains refused. It does not establish pending-work coverage, GPU-content
ownership, Wasm restore, or complete renderer closure. Keep this source
(`c89bae3d`) distinct from the earlier real-GPU probe (`5cdcd2a7`) and
first-draw observation (`fb63c3f2`). On `c89bae3d`, all three existing Release
profiles and the full 2,435-test regression pass, with 164 skips. The 134 build
artifacts, source identities and four retired process groups were independently
verified. The earlier `5cdcd2a7` validation and real-GPU result remain separate.

### October 15 native pending/submission component

At integrated source `299d5a04` (tree `05dba633`), the focused native component joins copied recorder-packet facts with the existing submission owner book, pool and sequence stamps without pumping or draining work. The 13-step focused run passed the actual packet copier and owner-book negative controls, both actual `frame.cpp` quiescence branches, `recording.cpp` syntax, both declared/defined symbol joins, and two byte-identical ordinary/OFF projections. Review rehashed 3,570 source/tool pins and 564 overlay files; all 13 owned process groups were absent.

This is component evidence only. `native_submission_component_ready` remains a native predicate; no runtime getter/global invocation, GPU call, live match interval or gameplay arm was exercised. It does not close depth/device/host-asynchronous work or establish a safe interval. Three Release profiles pass on runtime producer `299d5a04`; the full 2,435-test regression (164 skips) passes on test-only child `927a07f0`, with all 134 retained runtime artifacts rehashed. The initial full-suite failure is retained: a test fixture classified the browser report as native because an ancestor directory contained “native”. A two-line fixture repair checks the immediate role directory and exercises the ancestor collision without changing any assertion. The reduced before/after reproducer and all 16 focused tests pass. Keep this source distinct from the `c89bae3d` selected-sampled component, `5cdcd2a7` real-GPU probe, `fb63c3f2` first-draw observation, and `13922b8d` no-disc boot. No Wasm restore or gameplay/rollback admission is claimed. Exact component and review identities are in `native_submission_pending_component` under the [existing structured receipt](../evidence/match-only-rollback-feasibility-v1.json).

At source `ad871c96` (tree `e68b31d4`), the depth no-work diagnostic component and its required local validation pass: four Release builds and 2,437 tests with 164 skips. The validation binds Aurora tree `5142c624` and patch `c334e3b8`. This is component/source evidence only; live gameplay depth eligibility and restore remain unrun. Exact validation identities are in `depth_no_work_component` in the existing structured receipt.

The first-draw-components diagnostic is integrated at runtime producer `9a22eec3` (tree `9b985972`). Its private diagnostic, ordinary, frame-effect and combined first-pair Release builds pass with inventories of 44, 44, 44 and 46 files. The earlier candidate's 46 JavaScript controls and four reduced controls passed in separate runs; they were not one 50-pass invocation. A later actual-checkout `node --test` run passed all 50 selected JavaScript subtests in one command. The initial full suite exposed one stale test assertion for the explicit private first-draw build flag. A later V2 regression-launch preflight stopped before its rerun because its `git` helper comparison retained the trailing newline; that preflight launched no child. The two identity comparisons were normalized before the successful rerun. The only checkout change after producer `9a22eec3` was a one-line test expectation in `tests/test_aurora_browser_quiescence_option.py` at test-only child `baf66778`; the runtime producer stayed `9a22eec3`. The rerun passed 2,440 tests with 164 skips in 580.897 seconds. Independent review rehashed 178 build artifacts, four caches, and 18 prior evidence items.

The actual V5 first-draw capture observed the first naturally due callback from source frame 180 to 181: one tick, one draw, one audio return, and 534 PCM frames. The native readiness read reported the component ready and depth `READY` at sequence 3; a later, separate depth arm read `ACTIVE` at sequence 4. These are separate observations, not an atomic admission interval. The bounded H1 recorder retained 100 samples from 3,115 callbacks, with 3,015 older samples evicted; it recorded eight preparation incidents, zero dropped incidents, and zero forced timing pauses. This lossy recorder does not establish timing or preparation completeness.

The V5 root review and independent replay passed the four strict outer/component decoders. The capture owner recorded 13 owned PIDs and three owned process groups absent after cleanup. Renderer findings 7 (`LOAD_ATTACHMENT`) and 27 (`COMMAND_SHAPE_MISMATCH`) remain. This is one component observation, not a restore, GPU-content equivalence, later-draw, gameplay, or whole-session acceptance result. Composite eligibility remains false; LOAD content, sampled-texture continuous protection, device/host futures, native references, and restore remain open. Exact validation, capture, replay, and retained-failure identities are recorded under `first_draw_components_candidate_v3` and `first_draw_observation_v5` in the existing receipt.


On October 15, the selected-texture continuous-interval guard was integrated at
`635fc17b` (tree `7f5fe199`; Aurora tree `19cea02f`). The focused actual-TU,
symbol and ordinary/OFF controls pass. Five Release builds pass, and the full
regression passes 2,440 tests with 164 skips; the independent validation review
rehashes all 180 declared artifacts. Candidate source-overlay and syntax failures
from earlier v3/v4 attempts remain separately identified in the structured
receipt; they were setup/compiler integration failures, not GPU behavior results.

The isolated v6 real-GPU fixture also passes root and independent review: it
uses an actual non-fallback Apple Metal 3 adapter, validates two equal 64-byte
readbacks, and retains six distinct terminal refusal cases. Errors and owned
process cleanup pass. This remains an isolated component result. No actual-game interval had run at the time of the v6 fixture; the first actual-game attempt is recorded below. The first-draw blockers—including findings 7 and 27 from their earlier producer—remain open. Gameplay arm and Wasm restore remain refused/unimplemented. Exact isolated build, capture, cleanup, and retained source-failure identities are recorded under `selected_texture_continuous_interval_component` in the existing receipt.

The first actual-game attempt used runtime producer `635fc17b` and harness head `1f74eb2c`. The harness full regression passed 2,440 tests with 164 skips; that test identity is separate from the five Release builds above. The route reached normalized Mario-versus-Mario Final Destination VS ready at source frame 181. The requested callback advanced source frame 181→183, with two planned steps, two tick returns, and two draw returns. It selected frame 182 and left one later draw unobserved. The one-frame observer refused this callback with `component_requires_valid_exact_one_tick_draw`; it emitted no component read and did not start the selected-texture interval. The retained callback renderer packet was fenced (one submit and one successful fence), with 1,789 event rows, 269 handles, 246 actual draw rows, and first blocker `LOAD_ATTACHMENT` (7); finding mask `167903360` has bits 7, 17, 25, and 27 set. The callback returned one audio result/533 PCM frames; the separate pre-tick observation recorded one audio return/534 PCM frames. H1 recorded eight reason-7 incidents, no dropped incidents, and no timing pauses; 100 samples were retained from 3,119 callbacks, with 3,019 older samples evicted. This is a valid gate refusal, not a selected-texture guard result. The retained evidence does not establish why the callback accumulated two steps.

The failed run had no page errors. Browser-owner cleanup passed: all ten recorded browser process entries were absent, and the capture, server, browser, and strict-replay process groups were absent. Native retirement was not attempted under the browser-owner-only failure policy. A secondary strict replay failed because the selected-interval schema was absent after the component gate refused. In that first attempt the selected-texture interval was unobserved; gameplay arm, Wasm restore, and renderer-ownership closure remained unestablished. Exact capture, cleanup, suite, and retained source-failure identities are recorded under `selected_texture_continuous_interval_component` in the existing receipt.

The follow-up runtime producer `dbd0ed9c` adds a private, opt-in natural-one enrollment request and copied receipt for the selected-texture diagnostic. It preserves the ordinary first-draw path and its 1–8-step behavior: enrollment is promoted only at the real begin-tick hook when the naturally computed plan is exactly one step within source frames 180–240; other callbacks keep their original work. The earlier `635fc17b` attempt remains a two-step gate refusal (181→183, selecting frame 182, with no interval dispatch), not an interval result, and does not explain why the callback accumulated two steps.

Four Release profiles pass (44/44/44/46 artifacts), with 178 artifact instances rehashed; the full regression passes 2,442 tests with 164 skips. The permanent natural-enrollment controls pass. One released original-route attempt then selected the naturally due callback at source frame 180→181: the copied receipt records one planned step, tick, draw, and audio return, with 533 PCM frames and no skipped callbacks, ticks, draws, audio, or PCM. The first interval issue operation returned `issued=0`; its guard remained `INSTALLED` with no identity or attempts, so no interval close or interval fence was observed. Independent strict decoders accepted the component read and arm, but that is not a successful issue or restore. The renderer packet separately reports `LOAD_ATTACHMENT` as its first blocker and finding bits 7 and 27; that does not establish the cause of the issue refusal. The exact native issued-zero branch was not observed, so the refusal cause remains unresolved. The bounded run had no page errors or timing pauses; cleanup passed after 59.421 seconds, with 13 browser-process entries covering 11 PIDs and all recorded browser, capture, server, and replay groups absent. H1 retained 100 of 3,124 callbacks (3,024 evicted), with eight reason-7 incidents and zero dropped incidents; this is lossy diagnostic coverage, not a completeness or timing claim. The next step is retained-data/source reduction of the first issue refusal, not an unchanged capture retry. No restore or gameplay acceptance is established. Exact source, validation, preserved refusal, packet, and capture identities are in `natural_one_tick_enrollment` in the existing structured receipt.

The reason-bearing follow-up retains runtime `dbd0ed9c` and uses only the reviewed diagnostic harness child `f62b1afc`; no runtime rebuild was needed. Thirteen focused actual-helper controls and the retained-data loss reproducer pass, and the harness-only full regression passes 2,442 tests with 164 skips while all 178 runtime artifact instances and four caches remain unchanged. This second released attempt preserved an initial noneligible callback at frame 180→182 with its original plan/two ticks/two draws/one audio return and 534 PCM frames, then naturally selected frame 182→183 with one planned tick/draw/audio return and 533 PCM frames. Component read/depth arm and strict enrollment decoding passed. Issuance again returned zero, now with copied native reason `selected_texture_current_owner_mismatch`: the project submitted-witness/frame/queue gate passed and the actual GX issuer returned non-one. The particular current-slot/owner-fact/host subcondition was not recorded. The guard remained unarmed `INSTALLED`, identity-null and attempts-zero; only read and issue ran, with no interval fences/close, native retirement or restore. The renderer separately reported first blocker 7 and finding bits 7/17/25/27 (167903360), with 266 command records, 246 actual draws and 1,787 events; these findings do not identify the issuance cause. Cleanup passed after 63.879 seconds: all 11 unique recorded browser PIDs and four owned groups were absent, with no page errors or timing pauses. H1 retained 100 of 3,169 callback samples (3,069 evicted), eight reason-7 incidents and zero dropped incidents; its lossy scope is unchanged.

The earlier source-only suggestion that usage-20 publication identified the first sampled owner was withdrawn: resolve also promotes `TextureRef` owner facts, and retained rows do not distinguish promotion through resolve from GX sampling. Both failed interval-boundary attempts and the additive correction remain retained. A smaller control now passes using the verbatim actual GX issuer and current observer: unchanged owner reaches the synthetic host bridge once; current-slot replacement, new authentic serial, raw view mismatch and missing ref refuse before the bridge; forced host-zero is distinguishable by one bridge call. The six fresh cases and 849 pinned compile dependencies establish the mechanism only, not which field differed in the capture or live native-resource lifetime. Later original draws can legitimately replace an authored GX slot, so the current first-sample/current-slot contract is soundly fail-stop. That checkpoint proposed a narrow authenticated reborrow of the exact still-live first owner at the stopped boundary, or an explicit no-go if its lifetime/current identity could not be established; it did not yet implement a runtime seam. This is a diagnostic ownership-contract question, not a game bug or permission to choose a later passing texture. No unchanged capture retry, guard-usage widening, full interval/restore/composite admission or rollback acceptance is established.

Source `4d00abc9` now implements that conditional exact-cache borrow. At the actual first sampling event it copies an authored object-cache key only when the existing entry owns the exact sampled `TextureRef`. Both issuance and the later current-owner check use the same immutable key and a local borrow of the still-owning cache handle. They do not resolve, scan, touch cache accounting, retain another strong reference or fall back to the later GX slot. Missing/replaced/retired authority refuses; first-sample consumption, identity checks and the usage-6 guard predicate remain unchanged. Request, detach, device and diagnostic reset invalidate the copied authority without resetting the host guard. This establishes a local C++ owner boundary, not native GPU lifetime or unchanged contents.

Five affected existing Release targets pass: isolated guard (2 artifacts), private first-draw (44), ordinary (44), frame-effect (44) and combined first-pair (46). All 180 artifact instances and five caches were rehashed after 2,443 tests passed with 164 skips in 564.888 seconds; the new permanent actual-source cache-owner test ran successfully. The first focused execution passed all 39 commands but left the compiler shim's inherited interpreter/ccache selection insufficiently bound. Its outputs remain retained with that limitation. A reviewed executor-only correction bound the existing Python explicitly and required ccache absent; all 39 commands then passed with the effective environment recorded, ten dependency closures verified, three native symbol joins, exact ordinary-OFF byte equality for both changed TUs, and canonical Aurora tree reconstruction. The 25 synthetic native cases include one documented redundant mode. The prior `dbd0ed9c`/`f62b1afc` failed captures and withdrawn usage-20 inference remain unchanged. The changed-owner browser observation is retained below. That earlier `4d00abc9` attempt did not establish a successful actual-game interval; native GPU lifetime/content closure, restore/repeat and gameplay admission remained open. Exact identities are in `natural_one_tick_enrollment.exact_cache_owner_component` in the existing structured receipt.

The one released `4d00abc9` cache-owner observation also refused issuance. Natural enrollment selected frame 180→181 with the original one planned tick/draw/audio return and 534 PCM frames, without skipped work. Strict outer/renderer decoding, component read and depth `READY`→`ACTIVE` arm passed, but issue returned zero with copied native `selected_texture_current_owner_mismatch`. The submitted-witness/frame/queue prerequisite passed; the specific cache authority, current-owner facts or host-bridge predicate remains unobserved. Guard state stayed unarmed `INSTALLED`, identity-null and attempts-zero. Only read and issue ran; there was no interval fence/close, native retirement or restore. The separate renderer packet has first blocker 7 and finding bits 7/27 (134217856), 256 command records, 237 draws and 1,720 events; those findings do not identify the issue cause. Browser ownership cleanup passed after 59.710 seconds (155 ms cleanup), with 15 owned PIDs and four groups independently absent. There were no page errors or timing pauses. H1 retained 100 of 3,119 callbacks (3,019 evicted), eight reason-7 incidents and no dropped incidents; it remains lossy diagnostic evidence.

Before that launch, source review caught an unexecuted packet-V1 release-variable mismatch and four-profile metadata despite five actual builds. Packet V2 corrected the alias/metadata, restored the release-to-launcher-review hash check and deleted the verified disc buffer before browser work; the capture body was unchanged, and no packet-V1 capture ran. The failed V2 capture and its secondary strict-replay refusal remain retained as preparation history.

The later `f3340de4` runtime/harness passed five Release profiles (2/44/44/44/46 artifacts), rehashed all 180 build artifact instances and five caches, and passed the full regression (2,443 tests, 164 skips). Its released natural-one callback advanced source frame 180→181 with one planned step, tick, draw, and audio return and 533 PCM frames. Component observation failures and component failures were empty, but native issue returned zero with the exact refusal `selected_texture_cache_generation`. The root review scopes this to the captured bind-generation differing from the current generation at issue; it does not identify the mutation site or amount, establish selected-owner survival, or validate resource contents. No interval was issued or closed, and there was no native retirement or restore. Browser-owner cleanup passed after 62.868 seconds (152 ms cleanup), with all 13 recorded PIDs and three process groups absent. H1 retained 100 of 3,140 callbacks (3,040 evicted), with eight incidents and zero dropped incidents; this is a bounded lossy ring, not complete event or timing evidence. The earlier `4d00abc9` `selected_texture_current_owner_mismatch` remains a separate generic refusal and is not retroactively reclassified by this result.

The reduced actual-source controls now distinguish three cases: pending invalidation refuses without draining work; applying a generic invalidation changes the global generation while preserving the exact cache entry, owner and authored tuple; applying a cache clear removes the entry and releases its map-owned reference. All 35 cases in the focused permanent test pass against the unchanged `f3340de4` production methods with the separately hashed test candidate. GPU endpoints remain synthetic. This proves the distinction between binding invalidation and entry lifetime, not which invalidation caused the browser failure. The earlier unexecuted fixture draft had a pre-clear reference-count assertion; its reviewed correction and the retained actual-method result remain separate from production behavior.

The private per-entry publication-serial contract is now implemented at `ba2b4b83`
(tree `7ce4399a`; Aurora tree `7643f5b9`). Every actual store creates a fresh
nonzero serial; invalidation preserves a still-live entry, while replacement,
erase/reinsert, overflow and stale first-sample authority refuse. Pending-work,
native owner, device and host checks remain unchanged. The 49-case actual-source
control passes, including the previously failing invalidation distinction.
Five affected Release profiles pass with all 180 artifact instances verified;
the full regression passes 2,443 tests with 164 skips in 566.212 seconds.

One actual-game observation on that producer passes the selected-texture
read-only component interval. Natural enrollment advances source frame 180→181
with one original tick, full draw and audio return (533 PCM frames). After that
callback and its normal fence, the stopped returned-idle interval arms, completes
both host fences and closes, with no tick or GPU submission inside the interval.
Native diagnostic retirement succeeds. Independent strict replay consumes all
eight responses, and root raw-result review rehashes every retained output and
all 44 served artifacts. All 14 recorded or lineage-derived owned PIDs and four
process groups are absent; browser-owner cleanup takes 140 ms. H1 remains enabled:
100 samples retained from 3,144 callbacks, 3,044 evicted, eight reason-7 incidents
and zero dropped incidents. This is a lossy diagnostic record, not timing acceptance.

This closes the selected-entry stopped-interval component only. Its publication
counter lives in Wasm and is rewindable: it cannot authorize heap restore.
Renderer findings 7 (`LOAD_ATTACHMENT`) and 27 (`COMMAND_SHAPE_MISMATCH`) remain,
as do previous-content preservation, complete device/native references and host
futures. Composite eligibility is false. The next bounded source task joins the
existing audio/file reader at the same fenced boundary; current profiles still
forbid combining those hooks with first-draw components. No restore/repeat,
combined seven-tick budget, gameplay rollback or whole-session acceptance follows.

Earlier generic-owner and global-generation refused captures remain above with
their original identities. The first entry-serial focused attempt failed before
any case because its extraction omitted `s_frameCount`; its unwrapped invocation
also violated workspace ownership. A reviewed extraction-only repair passed
under the wrapper. Those failed outputs and the explicit missing historical
PID/group records remain retained, separate from later exact cleanup receipts.
Current validation, actual capture and independent/root reviews are bound in
`entry_authority_validation` and `entry_authority_actual_interval` in the
[existing structured receipt](../evidence/match-only-rollback-feasibility-v1.json).

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
