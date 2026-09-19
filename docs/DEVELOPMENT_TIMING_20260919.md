# Development timing after the Link pipeline fix

The current PR #38 Release runtime completed the declared development workload
coverage with **zero hard hitch failures**, one native 16.67 ms target miss,
and both holdouts still unopened at the time of measurement. The reports use
`per_tick` replay scheduling; live scheduling equivalence is not evaluated.
This is development replay timing, with separate live controller, pixel/PCM,
consecutive-match and historical-cause acceptance gates.
The [receipt](evidence/development-timing-20260919-v1.json) binds the runtime,
profiles, plans, all started attempts, reports and interruption evidence.

## Coverage and interruption

The initial eight-slot plan measured Fox/Marth on Dream Land (`a822`) and
Marth/Falco on Yoshi's Story (`dca`), cold then warm, in two rounds. The first
three runs completed before the observed competing build. During the fourth,
another local task began an eight-worker audio build. That run passed its
timing counters at 14.245 ms native / 32.935 ms browser, but is retained as
contended evidence. The fifth run had started when the overlap was observed;
the owned runner and browser were stopped. Its consumed/interrupted slot has
no final browser report. The final three initial slots remain unstarted.

A separate six-slot recovery plan was frozen before execution, after arranging
a quiet window with the audio task. It retained the completed Fox/Marth pair
and supplied the affected or unfinished cache pairs. The browser session had
closed, so each warm measurement received an explicitly measured new cold
predecessor. The additional earlier Yoshi cold run remains recorded at
9.845 / 25.830 ms. No original result was erased or changed into a pass, and
no occupied slot was retried. The recovery run finished in 8 minutes 49 seconds;
periodic process checks found no competing heavy jobs. These snapshots are not
continuous monitoring of all background activity.

The actual overlap evidence is a retained process observation at 21:56:26 UTC,
showing the build at 71 seconds elapsed. A later interruption attachment
accidentally matched the recording shell after the build had finished. That
sealed attachment remains unchanged; a hashed addendum identifies the error
and retains the earlier real process observation. Reports and ledgers from both
plans were also copied to a separate local evidence backup.

The eight runs supplying the declared current coverage are:

| Workload | Round | Cache | Plan | Native max (ms) | Browser max (ms) | Native target misses |
| --- | --- | --- | --- | ---: | ---: | ---: |
| Fox/Marth, Dream Land | 1 | Cold | Initial | 14.935 | 22.140 | 0 |
| Fox/Marth, Dream Land | 1 | Warm | Initial | 10.595 | 23.890 | 0 |
| Marth/Falco, Yoshi's Story | 1 | Cold | Recovery | 18.300 | 30.675 | 1 |
| Marth/Falco, Yoshi's Story | 1 | Warm | Recovery | 11.215 | 29.420 | 0 |
| Fox/Marth, Dream Land | 2 | Cold | Recovery | 10.225 | 27.485 | 0 |
| Fox/Marth, Dream Land | 2 | Warm | Recovery | 9.590 | 27.130 | 0 |
| Marth/Falco, Yoshi's Story | 2 | Cold | Recovery | 11.160 | 28.740 | 0 |
| Marth/Falco, Yoshi's Story | 2 | Warm | Recovery | 13.415 | 29.680 | 0 |

These cover 37,920 source updates and draws across 37,922 browser/native
callbacks. All eight have zero native/browser hard gaps, long tasks, audio
underruns/overflows, live pipeline creation/queueing, timing resumes,
preparation pauses, focus losses and browser errors. The original interrupted
matrix itself remains incomplete; the table describes coverage supplied by
both frozen plans, not a retroactive passing result for that matrix.

## Configuration and target miss

The unchanged 24-file development Release snapshot contains the PR #38 code
and pipeline seed. It ran in headed Chrome 153.0.8010.50 on an Apple M4 Mac
with 32 GiB RAM and macOS 26.6.2 (25G83), on battery. The source surface was
640×480 logical pixels with a 1280×960 backing store at DPR 2. Focus emulation
was disabled. Cold clears the optional origin render cache; warm retains it
across a fresh application heap and ordinary disc import. Browser/driver
caches are uncontrolled. No profiler, hidden warmup or simulation change was
introduced. The audio replacement being developed separately is not in this
frozen runtime.

The single target miss is the first recovery cold Yoshi run's native callback
13, marked first-use: 18.300 ms for one source update/draw.
Its measured phases are 0.030 ms input, 5.105 ms simulation/audio, 0.080 ms
begin, 11.670 ms drawing and 1.410 ms end. It uploads 4,845,568 texture bytes,
creates zero pipelines and records zero staging/frame-slot waits. This
localizes the cost to simulation/drawing; unprofiled phase counters cannot
identify the inner drawing operation. It remains a target miss, and is not
attributed to the historical GPU wait or declared fixed by faster later runs.

Preparation is separate: 166.375–198.365 ms across the eight runs, with a
2.280 ms maximum preparation callback. Every run grows the Wasm heap by
66,846,720 bytes, from 334,102,528 bytes prepared to 400,949,248 bytes after
teardown. Per-run construction/render-wait phases and post-teardown allocator
live bytes remain in the receipt; this fresh-application inventory does not
establish retained-heap stability across consecutive matches.

## Remaining acceptance decision

The [historical causal gate](HITCH_CAPTURE.md#causal-diagnosis-and-holdout-lock)
requires a supported fix or convincing correlated classification of the same
retained failure before either holdout is opened. The September 12 trace lacks
the operation/client identity needed for that classification. The later
[browser-raster reproduction](BROWSER_RASTER_STALL.md) is a separate event;
another replay cannot reconstruct fields absent from the original recording.

Under the original criteria, issue #33 remained open. No further replay is
justified merely to seek a passing result or relabel the historical failure.
The project owner approved the following scope decision on 2026-09-19:
preserve the old failure as measured and causally unresolved,
while allowing a separately frozen current-runtime/seed/protocol gate to run
both reserved holdouts once with all existing hard thresholds. This decision
would not establish the old cause, erase its failure, waive new failures or
constitute a performance pass. The bounded exception is recorded in the
[holdout protocol](HITCH_CAPTURE.md#approved-current-runtime-scope--2026-09-19).
Issue #33 remains open until the approved current-runtime evaluation passes.
