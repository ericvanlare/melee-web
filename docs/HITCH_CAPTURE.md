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
3. Create a local spec with only verified development recipes and the frozen
   twelve slots; use `python scripts/hitch_capture.py plan --spec SPEC --output
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
