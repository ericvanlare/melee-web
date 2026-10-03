# Local desktop Slippi connectivity and lifecycle

**Compiled / Source identified / Native traced** for the local networking
boundary. Two fresh isolated pairs of pinned headless Slippi clients connected
through the local ENet matchmaker, completed the original Direct opening game
and a Final Destination rematch in the same two client processes, then returned
through the expected CSS flow. Both clients' replay records identify Mario on
the expected player ports with four starting stocks. The Direct opening follows
the source's random-stage pool; the loser selects Final Destination through the
original SSS for the rematch. The clients each consumed distinct non-neutral
input from the other peer in both games. The final pair observed a peer
disconnect; a separate interrupted harness run released its child process
groups and ports. Runtime socket sampling observed loopback destinations only.

The [hash-bound receipt](../evidence/local-slippi-connectivity-v1.json) links
the pinned client and patches, configuration, replay and pairing summaries,
remote-input observations, cleanup results, and hashes of the retained private
receipts. Setup and the reproducible acceptance command are in the
[local testbed guide](../../reference-capture/slippi/LOCAL_TESTBED.md). This is a
local interoperability result; rollback correctness, browser cross-play,
public Internet/NAT, rendering/audio accuracy and production readiness remain
unclaimed.

The [local reporter framing receipt](../evidence/local-slippi-reporter-framing-v1.json)
is a separate **Compiled / Source identified** HTTP boundary check. It binds
the v4 old/new service hashes and six exact 503 responses per variant; the new
response is close-delimited with `Connection: close` and no `Content-Length`.
It preserves the retained reporter-thread failure as a separate, non-
deterministic inference and makes no native gameplay, browser or official
service claim.

A separate [headless browser-to-desktop transport receipt](../evidence/slippi-browser-desktop-transport-v1.json)
records one 24-frame raw PAD exchange through the loopback ENet relay and the
desktop peer's existing input consumer. It does not establish browser gameplay,
general cross-play, or rollback correctness.

The [source snapshot feasibility receipt](../evidence/source-snapshot-feasibility-v1.json)
adds **Compiled / Native traced** evidence for a synchronous, source-only
Mario/Final Destination prefix. Two fresh runs pass repeated same-instance
restore/replay at depths 1, 2, 4 and 7, comparing full native observations and
PCM each replayed sample and all linear memory at each depth endpoint. The
script exercises a fireball and stock-loss transitions; a deliberately changed
RNG bit is detected. Every forward observation/PCM hash repeats between the
fresh runs. Cross-process raw-memory hashes differ and remain unclassified;
cross-instance restoration is unsupported. The full-memory resource baseline
is not practical player rollback admission. Drawing, JS/GPU/audio commits,
mutable Wasm globals beyond the stack, whole matches, browser multiplayer and
Slippi compatibility remain separate gates. Reproduction and ownership are in
the [testbed guide](../../reference-capture/slippi/LOCAL_TESTBED.md#source-runtime-snapshot-experiment).

The [shared-page follow-up receipt](../evidence/source-shared-page-snapshot-v1.json)
adds **Compiled / Native traced** evidence for bounded immutable page sharing
in the same source-only profile. Two fresh runs pass repeated exact
restore/replay, eight retained-state restores, release-to-zero ownership and
the RNG sensitivity/recovery control. All forward inputs, observations and PCM
agree with the unoptimized run. Every page is compared exactly before sharing;
changed pages retain hash-plus-byte comparison. The receipt distinguishes
first-capture cost, subsequent retained-state cost, owned payload and Node RSS.
Browser host/draw/audio/save ownership and practical rollback admission remain
open.
A [frame-scripted desktop diagnostic receipt](../evidence/desktop-rollback-diagnostic-v1.json)
records the original baseline and HOLD boundary. The compact [E06 transport
matrix receipt](../evidence/desktop-transport-matrix-e06-v1.json) records the
root-audited fresh NONE, DUPLICATE, JITTER, REORDER, HOLD, DROP and ordinary
scenarios with exact parsed finalized peer/baseline comparisons, receiver
attribution where applicable, native HOLD correction observations, and cleanup.
The superseded expanded receipt remains in ignored retained work after SHA
verification; it is not part of the public evidence payload.
The E06 validation ran 1,714 tests with 133 skips. DROP remains tolerance
evidence without a rollback claim; DUPLICATE, JITTER and REORDER remain
transport evidence without a native rollback claim. These results are local
adapted-desktop evidence only; see the [diagnostic command and observation
boundary](../../reference-capture/slippi/ROLLBACK_DIAGNOSTIC.md) for exclusions and
reproduction.

The first retail replay calibration now passes: two independent automated
Mario/Mario Final Destination captures repeat exactly for 240 neutral source
ticks, and the port matches their entry data, input vectors, declared fighter
fields, RNG and match clock. The v2 fixture additionally restores and compares
the semantic PAD configuration and all Master/Copy/Game histories on every tick.
A separate 240-tick retail camera-traversal audit finds no changes to these
declared fields during drawing for this neutral sequence. The calibration found
and fixed missing stage particle bank30
publication and the absent owned rumble data/interpreter boundary. Stage banks
30/64 share decoded assets; authored particle dependencies are checked before
entry. See the [capture procedure and evidence](../RETAIL_REPLAY_CAPTURE.md).
After these fixes, the visible Release Marth/Dream Land warm action inventory
completed all 46 cases and 6,168 source frames with zero hard-gate failures
(worst native callback 12.19 ms); an earlier incomplete sweep exposed a test
driver recovery gap at the original platform-edge teeter state, now corrected.
The modern Fox/Falco Battlefield donor now also has two independently repeatable
vanilla captures: all 686 source ticks consume the intended PAD vectors, and
both 686-draw audits leave the declared state unchanged. The port matches every
declared field after restoring the original MSL sine/cosine and arctangent
routines with their explicit fused-operation rounding. Two retained numerical
reds at air-dodge entry led to these shared fixes; no tolerance or per-frame
state correction was added. This is still **zero admitted gold Slippi fixtures**:
broad corpus/coverage and content admission remain open. The first donor now
also passes visible source-draw state comparison and Release cleared-origin/warm
performance checks, with zero hitches, audio underruns or live pipeline creation
over all 686 ticks. Worst native callbacks were 12.735/7.42 ms and match preparation
186.745/200.905 ms on an Apple M4, macOS 26.6.2, visible Chromium 152, 640×480
framebuffer. Driver caches were not controlled. The final pair had no live heap growth. An earlier warm run grew the Wasm heap
by 86,441,984 bytes without a timing failure; this remains measured memory work,
not a zero-allocation claim. That earlier calibration's seed contained 389 pipelines.
`check_browser_replay.py` independently joins recipe, state, timing and build
evidence while refusing a broader admission claim.

A successor public corpus audit supplies 1,005 UCF-off candidates on the current
fighter/stage surface. Independent full parsing verifies 76 complete source
identities, including 16 exact human P1/P2 games compatible with processed-v2
input export. Eight development games and two fresh whole-game holdouts are
reserved before runtime execution. These legacy recordings lack complete raw
controller samples; original vanilla captures must still establish expected
state. The earlier 952-file zero-UCF-off sample no longer constrains candidate
selection. See the [verified split and limitations](../REPLAY_CORPUS.md#independently-verified-ucf-off-candidate-split).

The UCF-off cohort now has all eight development donors executed; both fresh
holdouts remain unexecuted. The port supports their original
eight-minute stock countdown through the source timer and timeout paths; exact
donor settings and separate timer sidecars are mandatory gates. Reusable
construction-input calibration passes the production collector and original
reference controls. The first Fox/Falco Battlefield donor matches all 5,772
recorded ticks, ending on the last KO before the source ending; it remains a
bounded recording. Marth/Marth on Yoshi’s Story and Fox/Fox on Final Destination
match both original captures through complete endings of 6,525 and 7,481 ticks.

The first three workloads found shared matrix/vector lane-order, Dolphin Slash
rotation and linear-spline rounding differences. Their earlier ten-game source
regression and six cold/warm runs passed on the previous candidate; those reports
retain their original build identities. See the [timed cohort evidence](../REPLAY_CORPUS.md#final-timed-cohort-candidate-performance-gate).

The remaining five donors independently repeat their original setup and endings
and now match 20,915 exact visible source ticks on the final Release build. They
exposed and cover Counter's wind command, stage quake animation ownership,
camera descriptor addressing, and source tick/draw ordering. Fresh-reference
replay entry also rejects unknown prior heap history, which v2 recipes do not
encode. The earlier 13-game state regression still passes on the source-fix
build, for 71,735 unique visible ticks across 17 complete games and one bounded
recording. Together with three earlier controls, the final Release build matches 38,640
exact current-build ticks. Old reports are not reassigned to a new executable.
All 518 tests pass.

Profiling found content-dependent vertex-array registry scans and unnecessary
SDK heap walks during ownership checks. An exact live-owner array index and
cheap generation accessor remove those costs without changing source math or
draw order. The first five-game cold/warm sweep on that Release build passed
eight of ten runs; two intermittent frame-finalization spikes remain under
investigation. Expanded timing reproduced a third red, then four further
full-partition repetitions passed. Those passing retries do not close the reds.
The final ten cold/warm runs pass their scoped gates: worst native/browser
callbacks are 12.830 / 29.420 ms, with zero live pipelines, heap growth, timing
resumes or audio faults. Those measurements do not clear the earlier unexplained
reds. The reviewed seed contains 507 pipelines. See the [remaining-five evidence](../REPLAY_CORPUS.md#remaining-five-ucf-off-development-donors).
The candidate is not frozen; broader gold/content admission remains open.

The reusable [bounded hitch-capture loop](../HITCH_CAPTURE.md) now preserves
every abnormal callback, previous timing context and optional browser trace in
an immutable attempt ledger. Its first fixed twelve-attempt matrix is complete:
eight unprofiled attempts measured six native 16.67 ms deadline misses, including
one native 33.3 ms hard failure, and three separate browser hard gaps across
37,915 callbacks. Maxima are 94.115 ms native / 106.645 ms browser. Four profiled
attempts had no hitches and cannot close these failures. The cold red is in
begin-frame work; warm recording spikes repeat the earlier Dream Land source
frame 1746 and Yoshi source frame 279. SQLite/IDBFS sync during frame finalization is a concrete
code-path suspect, still awaiting event-level causal confirmation. All raw
reports and the unrelated favicon 404s that marked these attempts aborted are
retained. No source gameplay code or numerical behavior was changed for this
capture work. Both fresh holdouts stay unopened, followed by a separate required
whole-sequence reference/performance track for consecutive matches with retained
source heap state before any public 4×4 readiness claim.
All 550 regression tests and the affected Release build pass. Harness recovery,
trace loss, overflow and served-build checks are covered; this is validation of
the diagnostic loop, not resolution of the measured gameplay red.

The subsequent four-slot causal capture completed 18,960 development input ticks
with four complete traces and no retries. Warm Fox/Marth on Dream Land reproduced
callback 1,883/source frame 1,746: 20.580 ms native and a 37.610 ms browser gap.
Three actual cache `fsync` waits account for 14.280 ms within that callback;
correlated stacks show the renderer's SQLite transaction triggering an automatic
WAL checkpoint through Asyncify/IDBFS. This identifies an optional persistence
path to remove from live gameplay. The other three runs record focus loss;
none is acceptance evidence. The earlier 91.795 ms cold begin-frame stall remains
unresolved. The new instrumentation passes 552 tests and the Release build;
its frozen build, all failures and causal evidence are recorded in
[the hitch-capture notes](../HITCH_CAPTURE.md#causal-capture-results--2026-09-12).
The optional-cache fix is now implemented and verified on these two development
workloads: SQLite transactions remain queued during source ownership and drain
only after native teardown, with bounded coalescing and explicit save failures.
Both original A/B comparisons pass for 9,480 source ticks, including declared
state/RNG/PAD, timers, source draws and match completion. Both real exported
DB/WAL pairs pass integrity checks and reload. Four separate unprofiled cold/warm
runs pass across 18,960 source ticks / 18,961 callbacks: zero native 16.67 ms
misses, zero native 33.3 ms failures and zero browser 33.3 ms gaps. Worst native
callback is 12.845 ms; worst browser interval is 30.555 ms. A separate complete
profiled run records zero live cache syncs and a successful post-teardown flush.
All 554 tests and the Release build pass. See the [fix evidence](../HITCH_CAPTURE.md#deferred-cache-fix-and-verification--2026-09-12).
The earlier cold begin-frame red remains independently unresolved; these clean
runs do not classify it as external scheduling. Both fresh holdouts and the
subsequent retained-heap gate remain closed.

A four-slot fresh/reloaded-browser experiment now reproduces the remaining
cold failure: a 90.745 ms native callback includes 85.945 ms across 27 waits for
a staging buffer's GPU completion. There are zero CPU frame-slot waits or live
cache syncs. A 115.722 ms GPU-process task overlaps it, including a Dawn worker
with only 1.057 ms of thread CPU across 114.404 ms wall time. This localizes the
wait but does not identify the underlying GPU operation or establish external
scheduling. Two subsequent, separately bounded startup GPU traces do not
reproduce the stall; both experiments preserve a focus-loss failure as well.
All six diagnostic traces are complete for their declared windows. The capture
tool now supports a frozen ten-second GPU startup preset with deferred stream
reading. No gameplay or renderer implementation changed; the red remains
unresolved and both holdouts remain unopened. See the [results and next discrimination](../HITCH_CAPTURE.md#cold-begin-wait-and-gpu-startup-diagnosis--2026-09-12).

A subsequent fixed four-slot diagnostic-output painting experiment completes
15,568 source ticks/draws but does not reproduce the long GPU worker. It retains
43 native deadline misses, zero native hard failures, 28 browser hard gaps and
three focus-loss failures. Startup CPU traces implicate application work in
some smaller misses; they do not explain the old wait. A separate native GPU
profiled replay adds two deadline misses and one browser gap, with no focus
loss. The native recording's exported retention does not cover those failures,
despite successful attachment and file creation. The new canvas verifier's
logical/backing-size mistake is corrected; all original failed attempts remain
preserved. No performance fix or external-scheduling classification is claimed.
Both holdouts remain unopened. See the [experiment and coverage limits](../HITCH_CAPTURE.md#diagnostic-page-painting-and-native-gpu-capture--2026-09-12).

Scene preparation now waits nonblockingly for submitted GPU work to complete
before arming the source clock, preserving source ticks/draws and menu audio
ownership. A controlled delayed-completion test proves the old build started
source execution prematurely and the new build waits without extra draws.
Both full development replays still match both original references across
9,480 ticks, and all nine original menu/entry comparisons pass. All 558 tests
and the Release build pass. Four independent cold runs complete 18,960 ticks
with zero native 16.67 ms misses, native 33.3 ms failures or browser 33.3 ms
gaps; native maximum is 10.710 ms. Three pass fully; the fourth retains a
focus-loss failure. All four are already GPU-ready at their first arming poll,
so this protocol fix does not establish the cause of the old 115 ms GPU task.
That red stays unresolved and both holdouts stay unopened. See the
[readiness fix and bounded verification](../HITCH_CAPTURE.md#submitted-work-readiness--2026-09-12).

The typed content path now carries Falco, Fox, Marth, Battlefield, Yoshi's Story and Dream Land
source IDs, runtime manifests and focused development traces. Source stock icon IDs are
selected from the typed character/fighter identity rows. In a fresh Release
browser route, raw PAD input selected P1 Falco and P2 Mario in the original CSS,
selected Battlefield in the original SSS, and entered a four-stock match. The
rendered frame showed Falco, Mario, Battlefield geometry and the correct four
Falco and four Mario stock icons. A complete ordinary-input loop, uninterrupted
audio and retail-reference comparison have not passed, so the accepted
first-deliverable claim remains Mario on Final Destination. The integrated
source match trace completes Fox on Final Destination and Battlefield,
including all four costumes, laser, Reflector, Illusion and Fire Fox. A combined
Fox/Falco run proves shared effect-bank ownership. Yoshi's Story completes
repeated source lifetimes with four map objects, Randall state, moving collision
and real Shy Guy creation after the original 120-frame scheduler. A Release
browser route selected Fox in the original CSS, selected the upper Yoshi's Story
tile in the original SSS, and rendered the source Ready countdown with Fox stock
icons and Yoshi's Story geometry. The lower adjacent tile is Yoshi's Island and
remains unavailable. Uninterrupted audio and retail comparison for these
additions remain open.

Marth's exact 0x98-byte extension, five costumes, 327 actions, three authored
dynamic-bone chains, null Article table, two-entry effect table and source audio
bank are pinned by owned-asset tests. The integrated trace executes jab, Shield
Breaker, Dancing Blade, Dolphin Slash, Counter and an aerial across all admitted
stages and repeats teardown. Dream Land pins eight source map objects, its joint
and material animation consumers, collision, lights, shadows, flagged objects,
and exact bird/tree/wind/blink parameters. The Release browser selects this pair
through the original CSS/SSS and runs a versioned 46-case inventory covering
grounded normals, shield/roll/dodge/grab, aerials, twenty wavedashes and all four
Marth special families. See [Marth's notes](../content/MARTH_PORT_NOTES.md) and
[Dream Land's notes](../content/DREAM_LAND_PORT_NOTES.md).

The active first deliverable is **original in-game CSS → original in-game SSS
→ a playable four-stock Mario-versus-Mario match on Final Destination → original
CSS**, without a results screen. Real menu assets, original scene/input behavior,
transitions and repeat-match lifetime are required. The former HTML selector is
removed from the player flow. Full accuracy, physical controllers, sound and stable performance
remain required acceptance work under [the accuracy contract](../ACCURACY_CONTRACT.md).
