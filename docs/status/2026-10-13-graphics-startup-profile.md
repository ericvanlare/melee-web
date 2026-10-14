# Public staging graphics startup profiling

**Browser exercised / Source identified — scoped startup improvement deployed**

Issue [#326](https://github.com/ericvanlare/melee-web/issues/326) follows the
owner’s reported 10–15 second “Preparing graphics…” wait on the public Pages
staging player, before disc selection. This is separate from #134’s post-disc
native menu preparation and from multiplayer transport.

The retained foreground Chrome reload used the existing browser profile,
without cache clearing or CPU/network throttling, with DevTools recording.
The deployed audio-player was source `07bcfdfa6f4531ac464eab44ebecb206daa1f0da`,
runtime graph `9c81b55e5bc2366d`, Pages deployment
`19a6ec7e-b035-4361-ad6e-43f275e4d3f5`. It is historical relative to the current
instrumentation candidate. The immutable deployment and public alias served
identical HTML during the inspection.

In sampled trace screenshots, the label was observed from 813.9 through
6,939.4 ms after navigation; the next sampled screenshot without it was at
7,017 ms. This brackets a warm reload observation, not the owner’s exact
reported attempt. Of 28 same-origin responses, 27 were marked cached,
including Wasm/data. Wasm cache-consumption markers finished by about 550 ms.
The renderer main thread was busy for about 8.7% of the sampled label interval.
The trace contains no direct WebGPU pipeline/shader events and cannot identify
which renderer readiness gate consumed the wait. Its 14.573-second recording
length is not startup duration.

The [scoped receipt](../evidence/public-startup-profile-v1.json) retains the
public build identity and derived trace observations.

Retained private run: `public-startup-profile-20261013-211719-304e5e58`.
The original gzip trace is unchanged. Its scoped derived summary is
`foreground-existing-profile-startup-1-summary.json`, SHA-256
`98728c0895b0728f5bedc7d28e0ac18bf688affe19446a277f84ee1946f06972`.
No source ticks or disc import were requested. The Chrome profile, docked
DevTools and instrumented capture are not a quiet-machine performance gate.

The profiling-only baseline was deployed on the ordinary audio-player at
immutable [e3dc446a](https://e3dc446a.webmelee-staging.pages.dev/). The public
staging alias now runs the guarded candidate below. Source `dcf6b06e794597b01bcc5d07c4dfb46f0e843018`, runtime
`2f3efb6678651310`, and producer/package identities remain in the scoped receipt.
The UI works before native startup and after an error, exports locally, and
records User Timing marks. The Release build, package audit, immutable HTTP
verification and two headless report/export checks pass. The first 2,400-test
regression retained one reduced-fixture failure (166 skips); its reviewed repair
passes focused controls. The corrected full regression on the guarded producer
is recorded below.

Two native Chrome UI reloads on that deployed candidate took 6,597.915 ms and
4,627.905 ms from mount. Their native startup intervals were 5,697.145 ms and
4,539.575 ms; renderer initialization was 79.755 ms and 29.350 ms. Each retired
all 890 declared pipeline entries with queue zero and the original cache-idle
state `1`. The second reload recorded no long tasks. Exact exported reports,
cache conditions and observation limits are in the receipt. These observations
identify the pipeline queue as the gate holding readiness; they do not measure
backend GPU compilation completion or reproduce the owner's exact 10–15 s run.

The first timer experiment at source `edece6ceffa8405afdf1350b219d51397049a681`
kept five-entry batches and the 8 ms yield but chained them independently of
rendered callbacks. Focused native controls, independent source review and the
Release build passed; the first local browser startup failed with three
“memory access out of bounds” errors. That candidate is **not deployed**.
The initial helper omitted the error stacks and raw failed profile, so the
failure cannot yet be assigned to a specific stack.

The bounded failure review identified a possible asynchronous native reentry:
an independent timer may enter Wasm while the ordinary render callback has a
live Asyncify continuation. This led to a generated, reduced Asyncify
fixture and a guard before native entry. The fixture passes independent review: five timer attempts with
a live continuation were withheld, followed by one native entry after idle;
stop/change/abort controls prevented entry. This is a component result, not
proof of the original memory-error stack or game safety.
The guarded candidate is now deployed at immutable
[526e0535](https://526e0535.webmelee-staging.pages.dev/) and the real public
staging alias. Source `0258f2fc73613fbfd2d07fe67277bf4f97890c9b`, runtime
`5e6240776085dbf6`. It retains one 8 ms timer, the existing five-entry batch cap,
queue order and native ownership checks; its JavaScript gate refuses native
entry while Asyncify has a live continuation, or after owner stop/abort.

The first new-artifact visit in the existing Chrome profile took 3,556.650 ms;
the ordinary warm reload took 2,621.200 ms, versus the profiling baseline’s
6,597.915 / 4,627.905 ms. The observed warm reduction is about 43%. Both retained
890 created entries, queue zero and cache-idle `1`. The warm candidate recorded
no long tasks. These are scoped same-profile observations, with uncontrolled
OS/driver caches, not a promise about every visit or the owner’s original run.

The matched headless deep probe preserves 916 pipeline and 917 shader API calls
in each run. API wall time stays around 22 ms while time between batches falls
substantially. One candidate run’s p95 animation gap is slightly higher than
its baseline; the maximum gaps and the other run’s p95 are lower. The receipt
retains all values: no blanket responsiveness or GPU-completion claim is made.

Release build, source-bound package audit, immutable HTTP verification, local
report/export checks and all 13 real-disc audio/menu/gameplay smoke checks pass.
The smoke covers a No Contest return and second entry, not a complete stock
match or acceptance. Owned process cleanup is verified. The corrected full
regression passes 2,402 tests with 166 skips in 544.327 s on producer `0258f2fc`.
Final integration requires independent review and complete current-head CI;
that integration state is tracked in [PR #327](https://github.com/ericvanlare/melee-web/pull/327).
Original failed timer evidence is preserved. Game arithmetic, input, source order, readiness
guards and deadlines are unchanged.
