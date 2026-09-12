# Bounded gameplay hitch capture

This development diagnostic makes intermittent failures reproducible as evidence.
It does not admit content, close a red automatically, or open fresh holdouts.
The gameplay clock, original source ordering, numerical behavior and existing
hard gate remain unchanged.

## Measurements and decision boundary

| Signal | Definition | Interpretation |
| --- | --- | --- |
| Native deadline miss | Active native callback wall time strictly greater than `1000/60` ms; preparation excluded | Miss of the 16.67 ms target, even below the hard gate; includes any descheduling within that interval |
| Native hard failure | Active native callback strictly greater than `1000/30` ms | Existing 33.3 ms hard gate |
| Browser callback gap | Interval between active browser callbacks strictly greater than `1000/30` ms | Separate hard failure; not native CPU execution time or GPU presentation time |
| Browser long task / long animation frame | Browser-provided observation with its own threshold and capability | Corroborating diagnostic data; absence does not prove an absence of scheduling delay |

Report all three timing counters separately, including zero, missing/unknown,
their denominators, and the per-attempt maxima. A callback can draw more than
one source tick. Keep source tick/draw counts with each event rather than
relabeling callback duration as single-tick cost. Never infer GPU execution
time by summing nested end-frame CPU intervals.

`hitch-capture=1` opts into bounded event capture. Each native deadline miss
and browser hard gap retains its timestamp, preceding native callbacks, source
frame and full native/end-frame timing. Optional `hitch-marks=1` adds User Timing
correlation markers for a browser trace. The collector has a fixed capacity;
overflow is an explicit failed/incomplete diagnostic, never an overwritten red.
No per-frame reference state is collected in performance mode.

## Frozen repetition matrix

Freeze a plan before the first replay. The initial matrix is exactly twelve
attempts on two already-executed development inputs:

| Phase | Workloads | Cache and repetitions | Attempts |
| --- | --- | --- | --- |
| Unprofiled | Fox/Marth Dream Land (`a822`), Marth/Falco Yoshi's Story (`dca`) | Two rounds, each workload cold then warm | 8 |
| Profiled diagnosis | The same two workloads | Two warm rounds | 4 |

Each attempt has a fixed 300-second wall-time bound including preparation.
Start with a fresh application/heap and one ordinary disc import each time.
Cold clears the origin's optional render cache. Warm reloads the entire
application with the origin cache retained. Browser/driver caches are
uncontrolled. There are no hidden warmup matches or forced garbage collections.

An attempt start is saved before browser operations. Every failure, timeout,
crash and interrupted start consumes its slot. Resume skips finished slots and
requires an interrupted slot to be explicitly recorded as interrupted before
continuing. It never retries an occupied slot. Reaching the fixed bound with no
reproduction leaves the previous red open. Further experiments require a new,
explained plan referencing the old failures; they cannot replace its results.

The plan binds the development allowlist, exact recipes, frame counts, previous
red reports, executable build artifacts, browser/machine profile and runner.
Every saved report/trace/terminal snapshot has an independently rechecked digest.
The allowlist is declared from the development split; neither holdout is read
or executed. A named target alone is insufficient evidence of its split role.

## Running the loop

Use Node 20+ with Playwright installed, the project Python environment, an
existing Release build, and the user's own verified disc. The optional
`--playwright` argument names the installed package directory; no global package
installation or browser download is required when using installed Chrome.
Keep all local specs, profiles, recordings and disc paths under ignored `work/`.
The initial automatic machine-profile collector uses macOS system tools; its
recorded configuration must be extended explicitly for another host platform.

1. Finish builds and tests. Start `scripts/serve.py` on loopback for the Release
   directory. Stop reference captures and profiling tools before unprofiled runs.
2. Use `node scripts/run_hitch_matrix.mjs profile --out PROFILE --build BUILD
   --browser-profile OWNED_PROFILE --url URL` to record the installed browser,
   ordinary scheduling settings, machine/power/display, renderer dimensions,
   artifact hashes and runner identity. This opens and closes an owned blank
   browser; it does not execute a replay.
3. Create a local spec with only verified development recipes and the experiment's
   predeclared slots; use `python scripts/hitch_capture.py plan --spec SPEC --output
   PLAN`. Review the generated plan before execution.
4. Run `node scripts/run_hitch_matrix.mjs run --plan PLAN --disc DISC`. The driver
   uses the ordinary file picker, replay selector and start button. It creates
   a separate headed browser profile, disables Playwright's focus emulation,
   keeps audio enabled and ordinary browser scheduling, and closes its browser
   when finished. It does not access private game state or inject source inputs.
   It hashes the actual HTTP responses for every executable artifact before
   each replay, after reserving that slot. The document and Wasm heap are fresh
   per attempt; the browser process/context is retained across the matrix.
   This is not a fresh-browser-process or driver-cold protocol.
5. Inspect `python scripts/hitch_capture.py status --plan PLAN` and every failed
   attempt. Raw reports, console errors and terminal DOM remain next to its
   immutable start/finish records. Profiled rows additionally retain the Chrome
   trace and metadata, including trace loss/truncation. No profiler data is an
   acceptance timing result.

Record the exact browser profile. In particular, clean installed-Chrome runs
do not close earlier in-app Chromium failures. A browser change is a different
configuration, not a proven fix.

## Causal diagnosis and holdout lock

Correlate an actual failed interval to its trace using the page clock and User
Timing markers. Inspect the enclosing renderer task, callback phases, garbage
collection, browser/driver work and, when present, per-thread CPU duration.
Missing CPU/scheduler trace fields remain missing evidence. A long wall interval
with a short sampled stack, a large residual, or a quiet JavaScript trace alone
does **not** establish external scheduling.

Close a red only with either a supported runtime cause and a verified fix, or a
convincing classification of the same failed event as external scheduling with
correlated evidence. Distinguish a browser/driver block caused by the port's work
from unrelated OS preemption. Record trace completeness, profiler perturbation
and alternative explanations. Keep the failed raw report after resolution.
Classification never changes a failing measurement into a passing run.

Both fresh holdouts stay closed until that review is complete. Then freeze the
candidate build, seed, inputs and acceptance protocol and execute the holdouts.
Failures become development regressions and require future replacement holdouts.

## Consecutive-match track after holdouts

Passing fresh-process replays does not establish correctness after earlier
matches. Before declaring the public four-character/four-stage loop ready,
require a separate consecutive-match track through original CSS → SSS → match
→ CSS, with retained source heap state and no page reload between matches.

Predeclare sequences of at least three completed matches, including the same
match repeated, a character/opponent/stage change, and Yoshi's Story after an
earlier Marth match. Across the inventory cover the four supported fighters and
four stages, return-to-CSS handoff, menu-audio continuity, source teardown,
respawn, final draw/ending and bounded post-teardown memory. Record precisely
which pairs/costumes/sequences are covered; a 4×4 selection inventory does not
claim every opponent/costume permutation.

Generate two independent original-reference captures of each **whole sequence**
from the same declared initial context, preserving all intervening menu inputs,
source allocations and teardown. Compare per-match inputs, declared post-draw
state, RNG, enabled timer and completion; collect separate uninstrumented timing
for the entire scene chain, with preparation reported independently.

The current v2 fresh-process recipe cannot express prior source heap history.
Do not replay it repeatedly, clear source heap bytes, force a Yoshi pattern, or
use unload as a substitute for reference context. Extend the sequence input /
reference transport and lifecycle ownership as needed, then keep this gate
separate from fresh-process holdout acceptance. The earlier Shy Guy pattern
divergence demonstrates why this is a required public-loop gate.

Implementation API references: [Playwright persistent browser contexts](https://playwright.dev/docs/api/class-browsertype#browser-type-launch-persistent-context)
and [Chrome tracing and loss metadata](https://chromedevtools.github.io/devtools-protocol/tot/Tracing/).

## First fixed matrix — 2026-09-12

Plan `651e27d2e546c04c959035b1064a0a5971c23ef520d1856eaff4b779753f828f`
consumed exactly twelve slots, with no retries or timing resumes. All twelve
replays completed their input and source draw counts. The eight unprofiled
attempts cover 37,920 source ticks and 37,915 native/browser callbacks:

| Attempt | Native >16.67 ms | Native >33.3 ms | Browser gaps >33.3 ms | Native max ms | Browser max ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| a822 cold r1 | 2 | 1 | 2 | 94.115 | 106.645 |
| a822 warm r1 | 2 | 0 | 0 | 19.370 | 32.375 |
| dca cold r1 | 0 | 0 | 0 | 11.420 | 27.535 |
| dca warm r1 | 2 | 0 | 1 | 25.500 | 37.610 |
| a822 cold r2 | 0 | 0 | 0 | 9.560 | 26.480 |
| a822 warm r2 | 0 | 0 | 0 | 10.625 | 26.655 |
| dca cold r2 | 0 | 0 | 0 | 10.895 | 27.995 |
| dca warm r2 | 0 | 0 | 0 | 12.240 | 27.705 |

All nine abnormal native/browser events are retained; the independent counters
match the event inventory and no collector overflowed. Two native misses are
multi-tick catch-up callbacks (six and three source ticks), not single-tick
execution costs. The first cold run also had 86 audio-underrun frames. All
eight have zero live pipeline creation, Wasm growth, preparation pauses and
visibility loss. Six raw timing reports pass the existing hard gates, but all
twelve **attempts remain aborted** because Chrome logged missing `/favicon.ico`
requests. The saved server log identifies those 404s. They do not excuse either
timing failure; the explicit empty favicon fix applies only to future builds.

The four separately profiled warm attempts cover another 18,960 source ticks.
They report zero native deadline misses/hard failures and browser hard gaps;
native/browser maxima are 10.480/28.005 ms. These clean profiled runs neither
provide an enclosing trace for the unprofiled reds nor close them.
The two a822 traces are complete. Both dca trace sidecars explicitly report
buffer loss; their decompressed files (554,143,378 and 566,992,541 bytes) also
exceed the fixed 512 MiB parser bound. The raw streams and bounded-read failure
reports are preserved. They are incomplete diagnostics, not negative evidence
for any proposed cause. Future trace collection must use a smaller category
inventory or a declared shorter window that can retain a full failed interval.

Two useful boundaries are now identified:

- The first cold a822 failure spends 91.795 ms in `begin_frame` within a
  94.115 ms callback. Frame acquisition/backpressure and browser/driver work
  need separate correlation; wall duration alone cannot classify scheduling.
- Warm a822 recording intervals are 14.185 and 13.110 ms; its worst callback
  repeats the earlier source frame 1746. Warm dca spends
  20.240 ms recording within a 25.500 ms callback at the same source frame 279
  named in the earlier dca red. No pipeline is created in these intervals.
  Code inspection identifies a plausible path: `end_pipeline_frame()` runs
  the SQLite cache writer on the single browser thread; Emscripten `fd_sync`
  on its IDBFS mount awaits IndexedDB even though the page explicitly saves
  only at teardown. This is a supported **hypothesis**, not attribution of
  any particular failed interval.

The next experiment should correlate cache sync calls and begin-frame waits
with actual failed intervals, using a new predeclared bound (initially one
cold/warm diagnostic pair per development workload: four attempts). Record
the relevant wait's start/end, call path and source callback identity. A
supported cache-I/O fix should keep optional persistence outside the source
clock while preserving pipeline availability, draw order and teardown saves;
verify it with separate source-state and unprofiled checks. Do not merely add
more clean repetitions or label the cold stall external by exclusion.

This matrix used headed Chrome 153.0.8010.36, Apple M4, macOS 26.6.2, AC power,
640×480 framebuffer and DPR 2. It does not replace earlier Chromium 152 data.
Its Wasm hash is
`09717d148a0231262afbe9868c58828f4c7aea098f9b602e290396be587f229f`;
the 507-pipeline data seed remains
`cdf157ee0f1850884f07a71165fd2192177acb23c2c777313b719e70ea67546f`.
The matrix predates the HTTP identity check added during review: the owned
server command/log and a post-run hash check establish its served-build
provenance, not per-attempt response attestation. All frozen source/build
bytes, raw reports, traces, errors, seals and final status are retained under
`work/hitch-capture-2026-09-12/`; `attempt-index.json` lists every report digest.
Subsequent harness fixes are not retroactively assigned to these runs.
A later startup-only browser check verifies all fourteen served artifacts and
the enabled diagnostic UI with no console or HTTP errors. Its screenshot also
retains a 26.15 ms native startup callback before any disc/game was entered;
this is separate from the gameplay matrix and is not a startup timing pass.

The three earlier reds and these new failures remain open. Neither fresh
holdout has been opened. Gold/content admission and public 4×4 readiness remain
false; the separate retained-heap sequence track above is still required.

The integrated change passes all 550 regression tests (256.363 seconds) and
the affected Release build. Focused checks cover strict timing boundaries,
immutable snapshots, bounded markers/history, unknown trace clocks, trace loss,
atomic interrupted-record recovery, evidence-size limits, fixed plan thresholds
and real HTTP served-build identity. No source gameplay change or accuracy
relaxation is included in this diagnostic implementation.

## Causal experiment protocol — four slots

The next diagnostic extension observes the actual render-cache mount sync
boundary and partitions begin-frame acquisition. It changes no scheduling,
cache writes, frame-slot policy, source input or source draw ordering. There is
no performance fix or causal conclusion yet.

Freeze a new build/profile and a new plan after this extension passes its
checks. Do not reuse the twelve-attempt plan or overwrite its evidence. Execute
only these four full development replays, sequentially, once each:

| Order | Existing development input | Cache | Mode |
| --- | --- | --- | --- |
| 1 | `a822`, Fox/Marth Dream Land, 3,892 ticks | Cold | Profiled causal diagnosis |
| 2 | `a822`, same input | Warm | Profiled causal diagnosis |
| 3 | `dca`, Marth/Falco Yoshi's Story, 5,588 ticks | Cold | Profiled causal diagnosis |
| 4 | `dca`, same input | Warm | Profiled causal diagnosis |

Use the existing verified recipes, development-role proof and original
reference results. Bind the new profile, runner, harness and every served
artifact; retain the old plan digest, its final status and every earlier red
as evidence. Keep the 300-second per-slot bound, fresh document/Wasm per
attempt, one retained browser context, ordinary audio and scheduling, no
hidden warmup, no timing resumes and no replacement slots. All four rows are
diagnostic; none can count as unprofiled acceptance.

The runner enables `hitch-causal=1` only for profiled rows. Cache diagnostics
must show that the render-cache mount hook was installed. Record sync starts,
completions, errors, pending operations and call stacks on the page's monotonic
clock. Distinguish explicit populate/clear/save requests from the native
`fd_sync` route by the recorded stack; unlabelled calls remain `unknown` until
that evidence is inspected. Stack text is bounded to 4,096 characters and may
be truncated. Auxiliary sync records have their own finite bound and never
increment native deadline or browser gap counters. Overflow, an uncompleted
sync or a missing hook remains visible; absence of records alone cannot prove
that the suspected path was absent.

Begin-frame timing must include both frame-slot and staging acquisition,
actual progress-wait counts, and the surrounding setup. Preserve failures and
the outer API residual, including work before/after the graphics function.
For callbacks with multiple source draws, report sums and counts as sums;
do not present them as contiguous per-draw trace slices. `begin_phases.start_ms`
and `end_ms` bound the last begin invocation; the maximum wait interval is the
largest individual wait across all begin invocations in that callback.

The reduced Chrome categories retain top-level tasks, the normal DevTools
timeline, User Timing, V8 CPU samples and renderer scheduling. Broad GPU,
compositor and detailed timeline categories are omitted to reduce trace loss.
The existing 128 MiB recording, 256 MiB stream and 512 MiB decompressed-reader
bounds remain in force. A smaller category list is not proof of completeness:
check the stream sidecar and actual failed interval. Missing GPU/thread CPU or
scheduling evidence remains unknown.

Correlate the same failed callback's begin/end partition, sync overlap and
trace. Overlap supports investigation but does not by itself prove causality.
If the four slots do not reproduce or explain the red, preserve that outcome
and propose a different discriminating experiment; do not keep repeating until
green. A demonstrated fix then needs the existing original-reference state,
RNG/timer/completion comparison and a separately frozen unprofiled timing
matrix. Continue reporting >16.67 ms native misses separately from >33.3 ms
native failures and browser callback gaps. Both holdouts remain closed, and
the retained-source-heap sequence track still follows holdout validation.

## Causal capture results — 2026-09-12

Plan `3154a2c5740809fe2a36b68e0a3e75fc6a82410e514ddc0919e18b8dcbf2300d`
consumed exactly those four profiled slots, with no retries or timing resumes.
All 18,960 input ticks and source draws completed. All four traces pass the
existing loss and bounded-parser checks; the smaller category inventory retained
the full long-workload traces this time. The build is Chrome 153.0.8010.36 on the
same recorded machine, Wasm
`959a715d3ae878f213e88f4e854a74e138ca31047eabf10c8e11a87974f512a2`,
with the unchanged 507-pipeline data seed. Source/build bytes, served-response
hashes, prior-plan/role proof, six prior red reports and all raw attempt evidence
are preserved under `work/hitch-causal-2026-09-12/`.

| Attempt | Native >16.67 ms | Native >33.3 ms | Browser >33.3 ms | Native max ms | Browser max ms | Live cache syncs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| a822 cold | 0 | 0 | 0 | 10.585 | 25.875 | 0 |
| a822 warm | 1 | 0 | 1 | 20.580 | 37.610 | 4 |
| dca cold | 0 | 0 | 0 | 10.860 | 28.290 | 0 |
| dca warm | 0 | 0 | 0 | 10.790 | 28.015 | 0 |

The a822 rows each contain 3,892 callbacks; each dca row contains 5,589.
The three rows without timing breaches record `focusLost` and remain failed
runs. Warm a822 has the browser-gap failure and no focus loss. The original
ledger's pass-only acceptance validator rejects all four reports with
`Browser report disagrees at pass`, as expected for their recorded failures;
this does not mean the diagnostic event records are malformed. Neither a
complete trace nor well-formed failed evidence
turns a failing attempt into a passing measurement.

Warm a822 repeats native callback 1,883/source frame 1,746. Its 20.580 ms callback
spends 16.555 ms in recording. The actual mounted-cache probe records three
sequential, completed `fsync` waits wholly inside it: 10.525, 0.440 and 3.315 ms
(14.280 ms total wall time). The fourth sync occurs later in the replay.
All eight cache boundary marks match the failed callback's renderer thread and
page/trace clock alignment. There are zero begin-frame progress waits, new
pipelines, texture uploads or Wasm growth in the failed callback.

The correlated CPU samples show `pipeline_cache_writer` → transaction commit →
SQLite's automatic WAL checkpoint → `unixSync`/`fsync` → Emscripten `fd_sync` →
Asyncify → IDBFS. They also show checkpoint file reads/writes and buffer
expansion between the waits. This is direct evidence of optional cache
persistence blocking the source callback, not a classification as unrelated
OS scheduling. Samples establish call-path presence, not precise CPU duration;
the 14.280 ms comes from independent, nonoverlapping page-clock intervals.
The raw report is
`a2cec931b2b86be573c3faf00b66fa3b5b495ebbfb6922e2985ec2d1682f2235`;
`trace-analysis.json` and `failed-interval-inspection.json` retain the alignment,
individual task CPU fields, raw sampled stacks and their interpretation.

The supported fix boundary is optional cache transactions/persistence outside
live source callbacks, preserving pipeline creation/use order and source math.
The following experiment implements and verifies that boundary. The earlier cold a822
91.795 ms begin-frame stall did not recur and remains unresolved; this cache
cause does not explain it. Both holdouts remain unopened. No public 4×4 or
retained-source-heap acceptance is claimed.

## Deferred cache fix and verification — 2026-09-12

The native-menu browser now opts into deferred Aurora pipeline-cache writes.
Pipeline creation/use and source tick/draw order stay unchanged. Pending writes
coalesce by SQLite key (shader type/hash), retain earliest first use and newest
payload/version, and preserve original enqueue order. The queue is capped at
16,384 unique rows / 64 MiB of config bytes. Exceeding either bound or failing a
transaction invalidates optional persistence; it never forces a gameplay flush
or evicts a required pipeline.

Flush only after all source owners, pending preparation and pipeline compilation
are gone. The Asyncify-capable SQLite call runs at the top-level native loop,
not inside a nested JavaScript command/export or between CSS and SSS. Its time
is reported separately. Public unload/save and export require native readiness;
a failed COMMIT is propagated instead of reporting a stale successful save.
Other browser targets retain Aurora's default immediate-writer policy. Native
threaded flush releases its wait mutex before querying status. Focus reporting
now retains one first-loss record without changing the visibility failure gate.

The reviewed Release build and all **554 tests** passed, including real SQLite
COMMIT rejection/rollback, queue coalescing/bounds and public save/export failure
paths. Evidence is in `work/hitch-cache-fix-2026-09-12/`, with source/build
snapshots, original-reference bindings, every attempt, exported DB/WAL files and
the complete profiled trace. Frozen Wasm:
`66a3709265d7c5c465700946b1125f91458bba7688992bd8fb454f848d7e94b2`.
The 507-pipeline data seed is unchanged. The profile records headed Chrome
153.0.8010.36 / Apple M4 / macOS 26.6.2, battery power with low-power mode off,
640×480 framebuffer and DPR 2. The preceding causal profile also records battery
power; the first twelve-attempt matrix recorded AC power. These are distinct
recorded conditions, not a power-normalized comparison.

Two state captures match the existing independent original A/B references:
3,892 Fox/Marth Dream Land ticks and 5,588 Marth/Falco Yoshi's Story ticks. The
existing strict evidence join passes for declared state, RNG, all-port input,
timers, source draws and original elimination/winner/completion. This is exact
agreement for the captured fields, not pixel/PCM/hardware equivalence. Each
actual exported DB/WAL pair passes SQLite integrity and config-size checks,
contains 508 cache rows, and reloads through ordinary public page controls.
The separately reported native flushes take 2.240 and 2.580 ms in those state
captures; those instrumented durations are not gameplay timing evidence.

Frozen timing plan
`d95b451d533a0ba9eadc97c99146ec8a5fcba743e8cdc7965c37234b0365d1e0`
consumes exactly four unprofiled slots and one profiled slot, with zero retries
or timing resumes. It binds seven prior red reports and the prior causal plan.

| Attempt | Native callbacks | Native >16.67 ms | Native >33.3 ms | Browser >33.3 ms | Native max ms | Browser max ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| a822 cold, unprofiled | 3,893 | 0 | 0 | 0 | 11.285 | 26.800 |
| a822 warm, unprofiled | 3,892 | 0 | 0 | 0 | 10.325 | 26.140 |
| dca cold, unprofiled | 5,589 | 0 | 0 | 0 | 12.845 | 27.850 |
| dca warm, unprofiled | 5,587 | 0 | 0 | 0 | 11.970 | 30.555 |
| a822 warm, profiled diagnostic | 3,893 | 0 | 0 | 0 | 7.930 | 25.405 |

Browser callback denominators equal the native counts in these rows. The four
unprofiled rows complete 18,960 source ticks/draws across 18,961 callbacks; all
strict timing gates pass, with no focus loss, browser errors, audio faults,
live pipeline creation or preparation pauses. The lower profiled maximum is
not a speedup claim or acceptance evidence.

The diagnostic report
`d2aad54087f2ab827340c0989206b5e5a7715beba46930ea8fdb54ff299e084b`
has an enabled, installed, valid cache-sync probe with **zero live calls**. Its
581,433-event trace passes loss/truncation and bounded-parser checks. A successful
native cache flush (1.005 ms) and storage save (10 ms) occur after source
teardown. Together with the directly traced failing cache transaction and the
reviewed ownership guard, this verifies removal of that optional persistence
path from gameplay. All failed baseline reports remain preserved.

The earlier 91.795 ms cold begin-frame stall remains unresolved. These clean
repetitions neither explain it nor establish unrelated OS scheduling, and do
not retrospectively attribute every prior spike to cache I/O. Both holdouts
remain unopened; the separate retained-source-heap sequence track still follows
holdouts. The public 4×4 loop is not declared ready. Next work should target the
remaining begin-frame wait, using a separately explained bounded experiment
that preserves this matrix and the original failure.
