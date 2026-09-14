# Public player seed initialization investigation

September 13, 2026. **Browser exercised; scoped performance diagnostics, not
content admission or a full-match/retail accuracy result. Candidate not deployed.**

## Finding and proposed change

The public shell omitted `/melee-render-cache`. Aurora opens the writable
`pipeline_cache.db` before importing the reviewed seed. That open fails when its
parent directory is absent, sets the native cache error state (`cache_idle=-1`),
and skips seed import. Development's optional IDBFS installer creates this same
directory, masking the omission. The shared runtime extraction is byte-identical
between the compared public/development artifacts and is not the first divergence.

The fix creates only that MEMFS directory in the public Module's `preRun` hook.
It restores use of the existing seed without IDBFS, persistent renderer storage,
new exports, audio modules, or a native/shared-runtime change. Startup cache
health changes from -1 to 1; the tested route creates no new entries live.
Confidence is **high** for this public startup defect and the route-coverage fix.
The exact magnitude of the hosted stalls also depends on browser/driver state;
no exclusive network/hosting or general renderer-performance claim follows.

**Startup cost is retained:** creating the seeded pipelines earlier can be slow.
The first hosted directory controls needed 21.63/23.59 seconds from starting disc
selection to disc-ready, versus about 1.4 seconds unseeded. Their native startup
callbacks reached 1,153.08/1,149.79 ms, predominantly staging completion waits.
The frozen local candidate needed 2.95–3.00 seconds and cold startup maxima of
52.16/43.87 ms at DPR 1/2. The fix prevents this route's live discovery but does
not make the initial renderer work free. Keep the candidate in draft while
reviewing this tradeoff and the renderer handoff below.

## Exact inputs and environment

- Source/base: `e24e296fdcdb3f67a0d371e94732870acefcef96` (fetched main).
  Fresh branch: `codex/public-performance-investigation`.
- Production: `https://webmelee.gg/`; immutable deployment
  `8f59ed0b-e0d9-47c8-b9b7-a57e22b71a79`,
  `https://8f59ed0b.webmelee.pages.dev/`.
  Manifest SHA-256:
  `a50eeba857b512efd77ba6cf26f58a0bd28bded8fd42df582b87f6600c037127`.
  Runtime group `cbc6a0e1aab45a41`; 20 files, 14,576,732 bytes.
- Exact existing `build/reconcile-candidate` served, without rebuilding, at
  `http://127.0.0.1:18981/` using Wrangler Pages 4.131.1 and its existing headers.
- Existing `build/reconcile-development`, whose 24 served files were checked
  byte-for-byte, supplies `http://127.0.0.1:8840/prototype.html` and
  `http://127.0.0.1:8840/runtime.html`. Development Wasm SHA-256:
  `f2cdfc9287a01d0404a732935e85ad7d9c538ef795303db17a6b0ae5b1a247bd`.
  Both use shared `melee-runtime.mjs` SHA-256
  `141610454c63d7434c1c5dd408fa6ee17a62f7657d25b2238c7fd9d6a9bf2322`.
- Apple M4, 10 cores, 32 GiB; macOS 26.6.2 (25G83). Battery power, 98% at
  inventory. Serial headed Google Chrome **153.0.8010.36**, Playwright Chrome
  channel, no in-app browser. Existing user applications stayed open; per-run
  process-load observations are retained. No CPU-heavy tests ran during captures.
- Each table profile identifies an independently created Chrome user-data directory
  at ignored `work/public-performance/<profile>/profile`. No user Chrome profile,
  Gmail, or inbox was used. Cold means empty origin/application storage in that
  new profile, **not** a cold OS/graphics-driver cache. Warm means a new document
  and Wasm heap in that same browser/profile. Browser/driver caches were not
  disabled or globally cleared; serial order and this uncertainty are retained.
- Primary viewport 1280×960, DPR 1 for all five targets. Native canvas backing
  640×480 in all five. Separate DPR-2 controls use backing 1280×960. All are
  secure-context capable, cross-origin isolated, visible and focused.
- The same locally selected owned USA 1.02 CISO was used throughout; the importer
  validates DOL SHA-1 `08e0bf20134dfcb260699671004527b2d6bb1a45`. No disc or extracted
  assets were uploaded or saved by the application. No replay or holdout ran.

## Controlled route and measurement semantics

Ordinary keyboard input selects two human Marios, four stocks, Final Destination.
The same script waits 60 source ticks in CSS, then pulses J, right Shift and
Enter at offsets 0/4/8 (two ticks each). In SSS it waits 30 ticks, holds D+W for
9 ticks, S at offset 10 for 4 ticks, and J at 16 for 2 ticks. Starting with the
first match tick, it requests 1,200 further source ticks (~20 seconds): U at 240,
K at 300, D at 360 for 20, A at 420 for 20, K at 480; repeat with offsets +600.
These DOM keyboard events use the ordinary keyboard adapter, scheduled at native
command boundaries. A callback may consume zero or multiple source ticks; actual
input delivery/source ticks are recorded. This is not a bit-exact retail replay
or a full-match ending test. The normal run ends at unload; it never resumes a
timing pause. Diagnostic continuation is explicitly separate.

External initialization scripts observe existing native timing callbacks while
preserving development handlers. They collect native callback wall durations and
subphases, source ticks/draws, first-use markers, live queue/create counts, WebGPU
creation APIs, long tasks, errors, heap capacity, cache state and geometry.
No instrumentation or new diagnostic exports enter the candidate. Native wall
measurements include waits/yields; GPU API call duration is not GPU execution
time. Browser gaps below are `menuFrame` callback-completion intervals, not a
physical presentation or input-to-photon measurement. Raw transitions and failed
prefixes are retained; no trimmed performance pass is asserted.

The directory controls inject only the MEMFS mkdir externally into the exact
old public artifact. They do not alter served files or enable persistence. The
presentation control changes only public canvas CSS to 900×675. The development
clear control uses its existing `?render-cache=clear` entry on the warm navigation.

## Five-way cold/warm result

Native distributions here select phase-7 callbacks reported as running. Preparation
total sums all reported preparation profiles, including transition and first-use
settling. Incomplete rows have only their observed prefix, not comparable full-run
percentiles. All-phase and startup costs are reported separately below.

| Profile / case | Finished source ticks | Native p99 / max ms | Native >16.67 ms | Match callback gap max ms | Live cache entries | Preparation total ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `matrix2-apex` cold | INCOMPLETE (0) | — / — | — | — | 0 | 464.95 |
| `matrix2-apex` warm | INCOMPLETE (3) | 544.22 / 544.22 | 1 | 873.09 | 5 | 2175.24 |
| `matrix2-immutable` cold | INCOMPLETE (3) | 511.15 / 511.15 | 1 | 880.12 | 5 | 2282.55 |
| `matrix2-immutable` warm | INCOMPLETE (67) | 602.23 / 602.23 | 3 | 900.05 | 15 | 2032.92 |
| `matrix1-public_local` cold | 1201 | 8.24 / 9.14 | 0 | 33.05 | 29 | 622.71 |
| `matrix1-public_local` warm | 1201 | 7.82 / 8.86 | 0 | 27.03 | 29 | 601.52 |
| `matrix2-prototype` cold | 1201 | 4.87 / 7.18 | 0 | 33.06 | 0 | 300.38 |
| `matrix2-prototype` warm | 1201 | 4.84 / 6.36 | 0 | 28.63 | 0 | 266.97 |
| `matrix2-runtime` cold | 1201 | 4.99 / 7.12 | 0 | 33.03 | 0 | 300.19 |
| `matrix2-runtime` warm | 1201 | 4.89 / 7.09 | 0 | 29.36 | 0 | 268.35 |

Apex cold stopped in SSS before match entry. Apex warm and both immutable cases
hit the runtime's timing-disruption pause. Local unseeded production completed
but created 29 live entries and needed roughly twice development's preparation
time. Thus the public-profile divergence exists without Cloudflare, while the
largest waits are intermittent and were most pronounced on hosted cold origins.

## Causal and geometry controls

| Profile / case | Finished source ticks | Native p99 / max ms | Native >16.67 ms | Match callback gap max ms | Live cache entries | Preparation total ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `matrix2-runtime-cleared` cold | 1201 | 4.91 / 7.14 | 0 | 31.92 | 0 | 284.50 |
| `matrix2-runtime-cleared` warm | 1201 | 7.48 / 8.56 | 0 | 28.34 | 0 | 266.64 |
| `matrix2-directory-control` cold | 1201 | 5.00 / 6.70 | 0 | 33.12 | 0 | 299.60 |
| `matrix2-directory-control` warm | 1201 | 5.59 / 7.56 | 0 | 28.84 | 0 | 266.27 |
| `matrix3-immutable-directory` cold | 1201 | 4.91 / 7.47 | 0 | 33.78 | 0 | 299.55 |
| `matrix3-immutable-directory` warm | 1201 | 8.73 / 10.08 | 0 | 29.01 | 0 | 283.31 |
| `matrix3-apex-directory` cold | 1201 | 4.95 / 7.39 | 0 | 33.36 | 0 | 297.79 |
| `matrix3-apex-directory` warm | 1201 | 4.86 / 7.92 | 0 | 28.94 | 0 | 266.59 |
| `matrix3-presentation` cold | 1202 | 8.09 / 16.20 | 0 | 34.23 | 29 | 621.77 |
| `matrix3-presentation` warm | 1202 | 7.57 / 8.81 | 0 | 28.45 | 29 | 603.08 |
| `matrix3-public-dpr2` cold | 1201 | 6.88 / 9.71 | 0 | 32.37 | 29 | 640.90 |
| `matrix3-public-dpr2` warm | 1201 | 7.62 / 8.64 | 0 | 28.09 | 29 | 605.77 |
| `matrix3-runtime-dpr2` cold | 1201 | 4.88 / 7.59 | 0 | 33.93 | 0 | 300.29 |
| `matrix3-runtime-dpr2` warm | 1201 | 4.78 / 5.30 | 0 | 29.08 | 0 | 266.97 |
| `candidate-dpr1` cold | 1201 | 4.84 / 8.12 | 0 | 33.45 | 0 | 300.71 |
| `candidate-dpr1` warm | 1201 | 4.68 / 7.43 | 0 | 28.83 | 0 | 266.61 |
| `candidate-dpr2` cold | 1201 | 6.06 / 8.80 | 0 | 33.41 | 0 | 299.81 |
| `candidate-dpr2` warm | 1201 | 4.95 / 7.33 | 0 | 27.74 | 0 | 266.59 |
| `matrix4-apex-continuation` cold | 1203 / 1 resume(s) | 7.61 / 139.19 | 2 | 155.92 | 29 | 907.57 |
| `matrix4-apex-continuation` warm | 1201 | 7.10 / 8.47 | 0 | 29.29 | 29 | 602.66 |

DPR is 1 except names ending in `dpr2`. Directory controls and both candidate
profiles finish without timing resumes. The 900×675 presentation control still
has the broken native cache and 29 live creations. Canvas layout and backing-size
changes therefore do not account for the seed-consumption divergence.

The corrected `matrix4-apex-continuation` cold run required **one visible Resume**
after a timing disruption; it is a failed timing run even though the scripted
workload completed. Its early interactive maximum was 139.19 ms with 24 new
entries after 5 at first match use; the repeated late inputs reached 8.33 ms and
created zero new entries. Warm navigation still created 29 live entries but its
maximum was 8.48 ms. Warming can mask the stalls in the same session/browser; it
does not repair public startup. Baseline warm hosted runs above still failed.

An earlier `matrix3-apex-continuation` cold diagnostic stopped after 317 ticks:
its harness clicked Resume twice before the first acknowledgement, then timed
out in the ordinary paused state. That tooling failure and raw prefix remain
saved; it is not included as a passing comparison. Its warm case completed with
34 live entries (maximum 9.37 ms). Pilot and first-harness failures are also
retained, including the original public unload observation of `cache_idle=-1`.

## Loading, preparation, first use and memory

Times below preserve navigation-ready, local disc selection/import/preparation,
and UI transition wall time separately. Disc-ready includes chooser automation,
local reads/native construction and concurrent renderer warmup; it is not pure
file I/O. SSS/match entry includes the declared key plan and the game's transition,
not just preparation CPU time. Missing entry means the run stopped before that
measurement completed.

| Profile / case | Page-ready ms | Disc-ready ms | CSS entry ms | SSS entry ms | Match entry ms | All-phase native max ms | Long tasks count / max ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `matrix2-apex` cold | 1348.02 | 1423.05 | 82.40 | 548.91 | — | 201.84 | 1 / 106.00 |
| `matrix2-apex` warm | 136.28 | 1402.63 | 75.01 | 307.89 | 3593.26 | 858.61 | 1 / 95.00 |
| `matrix2-immutable` cold | 976.64 | 1414.73 | 79.98 | 430.45 | 3598.93 | 865.55 | 1 / 108.00 |
| `matrix2-immutable` warm | 99.01 | 1416.62 | 83.58 | 295.95 | 1926.03 | 887.14 | 1 / 101.00 |
| `matrix1-public_local` cold | 412.95 | 1423.63 | 90.20 | 296.26 | 1972.71 | 34.97 | 1 / 115.00 |
| `matrix1-public_local` warm | 67.32 | 1422.10 | 81.26 | 295.00 | 1909.85 | 23.41 | 1 / 103.00 |
| `matrix2-prototype` cold | 382.20 | 3203.08 | 236.83 | 262.26 | 1860.29 | 35.19 | 2 / 147.00 |
| `matrix2-prototype` warm | 372.50 | 2821.64 | 84.42 | 263.88 | 1826.28 | 25.84 | 1 / 82.00 |
| `matrix2-runtime` cold | 162.96 | 3361.31 | 205.81 | 262.74 | 1861.65 | 41.34 | 2 / 127.00 |
| `matrix2-runtime` warm | 77.83 | 3305.86 | 83.49 | 262.60 | 1827.86 | 26.30 | 1 / 97.00 |
| `matrix3-immutable-directory` cold | 938.05 | 21629.80 | 84.06 | 261.38 | 1873.87 | 1153.08 | 2 / 119.00 |
| `matrix3-immutable-directory` warm | 368.13 | 2967.98 | 78.26 | 262.05 | 1840.83 | 26.21 | 1 / 105.00 |
| `matrix3-apex-directory` cold | 1123.24 | 23592.49 | 95.83 | 259.70 | 1860.39 | 1149.79 | 2 / 117.00 |
| `matrix3-apex-directory` warm | 235.13 | 2970.62 | 82.03 | 263.31 | 1842.07 | 22.95 | 1 / 98.00 |
| `candidate-dpr1` cold | 471.50 | 3003.57 | 78.85 | 261.24 | 1858.91 | 52.16 | 2 / 124.00 |
| `candidate-dpr1` warm | 81.18 | 2958.10 | 82.72 | 262.35 | 1823.19 | 26.45 | 1 / 101.00 |
| `candidate-dpr2` cold | 471.10 | 2950.96 | 91.31 | 262.27 | 1874.13 | 43.87 | 2 / 96.00 |
| `candidate-dpr2` warm | 94.53 | 2946.04 | 77.93 | 262.43 | 1828.10 | 26.04 | 1 / 90.00 |

Resource Timing separates the hosted directory-control cold Wasm fetch (about
0.62 seconds at apex) from the 23.59-second disc-ready interval; all application
requests are GETs for the existing same-origin player graph. Warm resource
transfers use the HTTP cache (zero transfer bytes, with ~28–32 ms Wasm resource
handling in the inspected hosted cases). There is no gameplay network endpoint.
Failed baseline prefixes retain request logs but not a completed Resource Timing
export; their page-ready durations are not presented as network-only timings.

The largest baseline immutable warm callback was 887.14 ms; 884.55 ms was a
staging-slot completion wait (201 waits). Apex warm similarly spent 856.83 of
858.61 ms there (195 waits). WebGPU creation calls themselves returned quickly
(sub-millisecond); this is downstream completion backpressure, not evidence that
the JavaScript creation call synchronously compiled for that entire duration.
The hosted seeded cold startup waits move to phase 0: about 19.22/20.95 seconds
of summed native callback wall time at immutable/apex. Browser long-task entries
mostly cover 90–127 ms import work and occasional ~60 ms startup work; native
async waits need not appear as browser long tasks.

First-use callbacks are included: cold unseeded CSS is 17.46–18.62 ms across the
three public origins, versus candidate 13.48/13.60 ms at DPR 1/2. Candidate first
match use is 6.67/5.45 ms cold and 1.53/3.07 ms warm. Transition/construction
callbacks still exceed 16.67 ms in the candidate, so interactive timing alone
must not be advertised as an all-phase performance pass.

Every complete route reaches Wasm capacity 334,102,528 bytes, starting at
161,087,488 and growing during import/preparation through 193,331,200,
231,997,440 and 278,396,928. There is zero measured interactive heap growth.
This is capacity, not a leak proof or repeated-session memory admission.
No measured case reported a browser/WebGPU/CSP error or device loss.

Public CSS canvas rect is 1280×919 with `object-fit: contain`, giving an effective
4:3 image about 1225.33×919. Prototype's iframe viewport is 1280×919 and its actual
canvas rect is 1225.33×918.99 at x=27.34. Runtime's canvas is 900×675 at (190,320),
with its bottom 35 pixels beyond the 960-pixel viewport. These presentation
conditions were measured, not assumed equal. The backing dimensions, DPR and
visibility were controlled; the explicit sizing experiment did not remove the
unseeded behavior. Production headers and immutable caching remain unchanged.

## Cache hypothesis resolved

The production `.data` contains the reviewed 2,113,536-byte seed, SHA-256
`cdf157ee0f1850884f07a71165fd2192177acb23c2c777313b719e70ea67546f`:
one shader row and 507 pipeline rows. It is present at `/initial_pipeline_cache.db`
in all cases. **Present does not mean consumed:** unseeded public startup has
created/queued totals 0/0; development/candidate first callbacks show 10 created
and 498 queued. Source order in Aurora `prepare_pipeline_cache_db()` opens the
writable database before `seed_pipeline_cache()`, establishing the first divergence
before disc selection, input, gameplay, or responsive presentation.

Copies of development cold/warm and cache-cleared SQLite files were queried
without modifying the saved evidence. Their `(type, hash)` key sets are exactly
the seed's 508 entries: zero additional keys and zero missing keys. The saved
~2.7 MB total includes SQLite/WAL bookkeeping and is not proof of newly generated
route coverage. Clearing development persistence does not reproduce production.
The volatile-directory control also ends with those same 508 keys.

Public persistence is **intentionally excluded** and remains excluded. What was
missing unintentionally was the writable in-memory directory needed to consume
the bundled seed. For this route, persistence is unnecessary. Different future
routes may discover entries outside the seed; this investigation does not admit
them or justify persistent game data.

## Candidate and verification

Frozen local candidate: `build/cache-fix-review-01`, served for review at
`http://127.0.0.1:18982/`. Manifest SHA-256:
`fb2b21a531bca7b0a042ed0718c2820ff8f76dd0d6d81f98cabd449c506d2110`.
Runtime group `6060c5b9edbb07fe`; 20 files, 14,577,020 bytes (+288).

Only two normalized contents differ: `player-shell.mjs` adds the startup hook,
and `index.html` references the new runtime hash. Native JS/Wasm/data and their
build identity are reused byte-for-byte from the audited production candidate;
no native rebuild, compiler flag change, renderer patch or seed update occurred.
All other content, including security headers, redirects, legal/operator/contact
text, silent-audio policy and opcode-63 disclosures, is unchanged.

The actual release-browser test now requires a healthy seeded volatile database.
It fails against exact old production locally (`-1 != 1`) and passes against the
candidate. The shell startup unit executes the real shell with a controlled
runtime seam, requiring FS work in preRun before native start and no persistence
or audio installer. The existing gameplay/accuracy tests remain intact.

- `python3 -m unittest discover -s tests -v`: 640 tests, 51 skips, no failures
  (170.55 seconds). Skips include unavailable native build targets, configured SDL
  headers and optional owned-asset/reference fixtures; they are not passes.
  The new shell startup regression ran and passed. No gameplay test was removed.
- Public packaging and final full artifact audit: pass, 20 files / 14,577,020 bytes;
  native identity/provenance, silent compile/link closure and allowlisted graph
  remain checked. Native JS/Wasm/data were reused, not recompiled.
- Actual candidate Chrome UI/network/storage/legal test: all 10 checks pass,
  including real owned-disc import, original CSS/SSS, pause/resume, eject and
  second launch. No audio context, uploads, WebSockets, IndexedDB, CacheStorage,
  cookies or service worker; only the keyboard preference persists.
- Local candidate HTTP: 18 exact served resources, five document aliases,
  30 forbidden routes return 404. Two existing Wrangler reserved-config routing
  limitations (`/_headers`, `/_redirects`) return 502/ENOTDIR locally. They are
  recorded explicitly; a later hosted candidate check must require 404.
- Existing apex, immutable production and rollback HTTP: each passes 18 exact
  resources, five aliases, 32 forbidden routes and security/cache headers against
  its original manifest. No production bytes or configurations were changed.
- Git diff whitespace checks pass. GitHub Verify is requested by the new draft
  PR; consult its checks for remote status. The local skipped checks above are
  not a substitute for that full native build workflow.

Production and rollback were read-only throughout. The live apex, immutable
production and rollback resource/header/forbidden-route checks were rerun against
their original manifests. Rollback remains
`928714aa-b7f1-412e-b8c8-08f7a9c7c0f4`, manifest SHA-256
`833c15bd41513fb4ad9085d91e40288494a5efaa301119e12d3637a52235f94c`.
No DNS, mail, deployment, PR #13, CPU source-address/issue #14 or thread-2 worktree
changes were made. The original opcode-63 limitation, silent audio and unresolved
recovered-code posture remain in force.

## Renderer handoff and remaining uncertainty

The public-shell defect is fixed here; no shared runtime or Aurora fixes overlap
thread 1. Its next smallest experiment is a local investigation-only comparison
of seeded startup under explicit GPU/driver-cache conditions, recording the
phase-0 staging-slot waits and time from navigation to playable CSS. Keep the
same original descriptor seed and source ticks; do not hide a match or consume
extra inputs to warm it. Determine why a fresh hosted-origin/profile seed costs
21–24 seconds while local and warm runs cost ~3 seconds before changing scheduling
or renderer cache behavior. The current native frame timing already localizes
the delay; physical presentation traces would refine the remaining compositor
uncertainty. No hosted candidate was deployed, and browser-only controls do not
substitute for a later exact hosted candidate audit.

Raw instrument versions, plans, actual inputs, timings, saved renderer-only DBs,
resource logs, failed prefixes and browser profiles are under ignored
`work/public-performance/`. This report identifies the profiles explicitly;
evidence is not part of the public artifact. Wider machines, browsers, long
sessions, non-Mario/FD content, full-match endings, physical input and retail
comparisons remain outside this experiment. Keep this PR draft and make any
promotion a separate reviewed action against the frozen manifest.
