# Bounded runtime incident recorder

The public player keeps a small optional local recorder for unexpected native
timing pauses and supported runtime failures. This is an observation boundary;
it does not change source scheduling, clock limits, arithmetic, input, audio
processing or save ownership. A triggering condition is evidence, not a root
cause. A callback gap alone cannot establish CPU overload or shader compilation.

## Collection and limits

`web/runtime-diagnostics.mjs` receives scalar values from the existing native
callback. The normal path updates scalar counters and a preallocated numeric
ring; it does not serialize reports, write storage, log to the console or query
native state. Ten seconds at at most 10 samples per second gives 100 rows of 20
Float64 columns (16,000 bytes), plus one 160-byte scratch row. Each row retains
the worst callback interval and timing costs in that sampling window and sums
source steps/draws, pipeline changes and texture-upload bytes. This keeps brief
spikes visible without recording every source frame.
The native bridge uses one fixed 152-byte stack row to stay within the pinned
Emscripten argument limit; it reads only these named numeric fields.

The first [packaged browser receipt](evidence/runtime-recorder-v1.json) measures
collection, a full 100-row capture, serialization and awaited isolated storage.
It justifies these bounded defaults for the observed CSS diagnostic workload;
match workloads and foreground performance still need their separate gates.

Native clock observers capture the reason, triggering value and threshold
before the clock resets its debt. Simulation debt, audio debt, nonfinite clocks,
clock regression and generic runtime failure are distinct. Manual pause/resume,
scheduled diagnostic pauses and rendering preparation are expected events.
Visibility and page lifecycle events provide context; they do not diagnose a
crash. Missing values are null, with explicit observer availability/observation
flags. No arbitrary exception message or stack enters a report.

An incident freezes its preceding ring and at most 32 recent events. The
post-event window is one second with at most 24 events; a recovery outside that
window is not promised. Four incident slots retain the newest triggering
incidents and mark evicted or truncated evidence. Export is limited to 64 KiB;
it trims copied history/events while preserving each trigger and marks that
loss. These are serialized byte limits, not a claim that JavaScript object heap
overhead is zero. Collection, capture, serialization and storage costs must be
reported separately from a same-build enabled/disabled browser measurement.

## Local persistence and privacy

On becoming inactive, a deferred task serializes bounded incidents and uses the
separate `melee-web-runtime-diagnostics` IndexedDB database. Resuming before the
task runs cancels that work. A read/write transaction merges records from tabs,
rejects malformed records, and enforces four records and 256 KiB across retained
records. Current triggers take priority over prior visits even if the wall clock
changes. Storage denial, quota exhaustion, unsupported storage and recorder
errors are optional diagnostic failures; Personal progress remains independent.
Hidden, pagehide and freeze events also request a local checkpoint outside the
native callback, before the ordinary deferred task may be discarded. Returning
to the visible page cancels a checkpoint that has not begun. Orderly teardown
waits at most 250 ms for a fresh local snapshot after native unload. It does not
wait for network delivery or delay Resume. Storage already in flight remains
best effort; a timeout does not cancel an IndexedDB transaction or prove that
the report was saved.
Flags distinguish unavailable storage, failed persistence, malformed retained
data, eviction and truncation. Successful persistence is best effort; a hard
browser/process crash can prevent capture or completion.

Reports contain only allowlisted technical fields: build identity, exact origin
without path/query/hash, explicit deployment environment, coarse browser/platform,
ephemeral random identifiers, scalar source/render/audio observations and bounded
lifecycle events. Disc names/contents/user-file hashes, saves, player names,
controller histories, credentials, arbitrary logs and raw memory are excluded.
The recorder itself makes no network requests. The optional delivery adapter
below runs separately while the player is inactive.

Packaging embeds schema version, actual committed source SHA, runtime graph hash
and build profile in safe HTML metadata. Audits bind it to the exact runtime
graph and producer. Environment is derived independently from exact known hosts;
unknown hosts stay unknown. A production-profile package on a staging host is
staging, so promotion does not require changing tested runtime bytes.

## Natural campaign and lifecycle boundary

The [integrated owner-fix receipt](evidence/runtime-lifecycle-clock-v1.json)
binds the current executable checkpoint to its Release builds, audited public
packages, lifecycle controls, native browser-to-local-Pages delivery, cold/warm
natural campaigns, separate controlled-contention attempt, original-state
comparison and full-suite results. It preserves source identities for every
historical failure and reduced experiment. The older
[natural first-pause receipt](evidence/runtime-natural-first-v1.json) remains
historical evidence; its limits and retained output are not rewritten as a
current-build result.

The lifecycle correction retains hidden/page state until native handoff, even
when visibility returns before another callback. The owner supplies neutral
input activity before the actual current state, and a one-use return resets both
menu and audio fixed-tick clocks before polling. Manual pause intent remains
paused. Synthetic hidden-interval recovery and ordinary foreground-stall
controls are separate: a visible stall still trips the original guard. The
earlier controlled hidden baseline and its failed whole ownership predicate
remain indexed separately. Genuine Chrome background/freeze event pairs were
unavailable in the headless capability probe.

A natural local first-use GPU preparation delay also reproduced a timing pause.
The retained packet aligns a native staging wait with Dawn pipeline creation
and nested Metal compilation. A prior source draw created a pipeline; a
subsequent offending callback waited for staging released by queue completion.
The generic fix detects completed source drawing, newly created pipeline
resources and pending staging, then uses the existing bounded source-free
render-settle path. It retains the last image, waits for tracked queue/frame
completion, resets both clocks and gates manual controls. No asset exemption,
guessed table bound, new threshold, skipped source tick or automatic Resume is
introduced. Direct identity between an individual pipeline and a driver
compilation future is unavailable, profiling perturbs timing, and driver cache
is uncontrolled; this is one demonstrated local preparation-wait cause.

The current natural runs use original menus, natural Results and CSS returns,
audio enabled and headless installed Chrome with silent host output. Both
cold/warm pairs observe native cache restoration and explicit owner unload/save
before their disposable profiles are removed. The natural harness uses the
private development runtime: exact served Wasm bytes and clean source identity
are bound separately from the audited public package graph. The public package
checks exercise lifecycle and delivery behavior. Controlled contention uses a
declared worker workload and remains separate from natural shared-host evidence.

The relevant recorded original-state comparison passes its declared match
RNG, frame, PAD and fighter fields. Nonmatch scalar/PAD state, menu/audio/process
state, live timing, pixels and PCM remain uncovered. Retained audio snapshots
show cumulative underruns increasing between Results observations without
locating their first increase. Quiet-machine/foreground protocols, physical
input, arranged listening, uninterrupted audio, pixel/PCM agreement and the
separate whole-session acceptance gates remain open. Neither induced lifecycle
holds nor the single local GPU wait classify users' remaining varied pauses.

## Automatic delivery and Settings

`web/runtime-diagnostics-delivery.mjs` maps one incident into the strict shared
wire schema. Known HTTPS staging/production hosts default to reporting enabled;
local and unknown hosts retain evidence locally. Settings contains one toggle
and an explicit local export action. The separate preference key is
`melee-web-automatic-diagnostics-v1`; denied preference storage still permits
turning reporting off for the current visit. A storage event propagates an
off preference to other open tabs. Off clears unsent work and aborts in-flight
requests; a request already received by the server cannot be retracted.

The runtime schedules collection for delivery after the one-second post-event
window. Entering gameplay, preparation or another busy operation cancels that
task. Existing pending work is retried only while inactive, with a five-second
request deadline, bounded exponential backoff, three attempts per queued
record, and four total upload attempts per visit. Each request is an exact
same-origin JSON POST to `/api/diagnostics`, at most 64 KiB, with credentials
omitted, redirects rejected and no referrer. No upload is awaited by the native
simulation or drawing callback.

The isolated `melee-web-diagnostics-delivery` database keeps at most four
pending records/256 KiB for seven days from capture and four small completed-incident tombstones
with the same expiry. Source incident IDs remain stable as recovery history
grows; the first queued packet is immutable for idempotent retry. Newest queued
triggers evict older work; flags record eviction, truncation, expiry, rejection,
malformed records and storage failure. Bounded cross-tab merge is best effort;
storage denial or races can lose diagnostic work without touching saves.
There is no persistent user identifier. A hard shutdown before inactive
collection or persistence can still lose an incident.

Manual export is available while gameplay and preparation are inactive. It
includes the bounded current report and up to four bounded local retained
records; this local download can be larger than one 64 KiB wire report. Off
does not disable local retention or export. Neither storage nor reporting is
required to play.

The [Cloudflare backend](../diagnostics/README.md) strictly validates the
allowlist, corroborates origin/environment with the host and an operator-set
release allowlist, and applies transactional rate, intake and retention caps.
Origin and release labels are not authentication. Authenticated developer
retrieval uses [the CLI](../scripts/diagnostics_admin.py), with environment,
source/runtime/profile, reason and time filters. Reports expire from retrieval
at 30 days; the separate bounded maintenance Worker deletes expired rows.
Provider failures can delay physical deletion. The deployment contract and
rollback instructions are in [the integration guide](../diagnostics/INTEGRATION.md).
Functions live in an audited sidecar outside the public static directory;
only the two diagnostics API route patterns reach them. Static security
headers, immutable runtime caching and blocked development routes retain
their existing boundaries.

The source-bound reporting receipt [separates current 4e package, native, and full-suite evidence from prior b251 Pages and 4d5 behavioral evidence](evidence/runtime-reporting-v1.json); its remaining acceptance boundaries stay explicit there.

## Focused verification

Run `tests/test_runtime_diagnostics.py`, `tests/test_diagnostic_package_identity.py`
and the animation-clock boundary check, then the required suite and affected
native builds. The package browser harness serves audited audio-player bytes
over real loopback HTTP and uses headless installed Chrome with speakers muted:

```sh
node tests/runtime_diagnostics_browser_test.mjs \
  --site /path/to/audited-site --manifest /path/to/sidecar.json \
  --disc /path/to/owned-disc --out /path/to/fresh-evidence-child \
  --playwright /path/to/installed/playwright
```

The harness balances enabled/disabled CSS collection order on the same build,
retains screenshots and GPU/isolation checks, then separately induces bounded
simulation/audio stalls and a sanitized supported failure. It checks native
reason/value/threshold, recovery context, persistence bounds and zero uploads.
Induced stalls validate detection/recovery only. Headless CSS evidence does not
establish sustained gameplay, original state agreement, quiet-machine or
foreground performance, physical input, uninterrupted audio or pixels/PCM.
Use [hitch capture](HITCH_CAPTURE.md) to reduce a natural failure and
[the accuracy contract](ACCURACY_CONTRACT.md) for separate acceptance gates.
