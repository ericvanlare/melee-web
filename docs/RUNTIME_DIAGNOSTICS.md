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

The [natural first-pause receipt](evidence/runtime-natural-first-v1.json) is
bounded **Browser exercised** evidence from source `18e4d54`: two CPU9 Final
Destination four-stock matches completed through Results and CSS with 26,862
source steps, zero forced pauses and zero automatic recoveries. The host was
shared and uncontrolled, and the headless run kept speaker output silent. It
does not establish quiet-machine or foreground timing, warm-profile restore,
original-state/pixel/PCM agreement, physical input, audible quality, or a
natural root cause.

The controlled lifecycle baseline uses the 4e audio-player identity and keeps
the hidden-interval case separate from the failed whole ownership predicate.
The hidden case observed the expected native guard (`22 > 8`); the overall
case failed because the browser manual-control predicate used a Node-scoped
hold value. Its retained receipt is
`/Volumes/AgentStorage/melee-web/runs/lifecycle-hidden-callback-baseline-20261001-202956-aab6e6ec/clock-boundary-evidence-v1.json`
with SHA-256
`0526ee7f2022779fca025c5bd3928685f97794c39a30ad1758a31d2ada856c23`.

The lifecycle hypothesis is that a hidden/page lifecycle interval remains
sticky until native handoff, neutral input does not overwrite the actual
visible state, and a one-use return value lets C++ reset menu and audio fixed
tick clocks before polling. The prepared two-file fix changes no thresholds,
ticks, RNG, manual resume, audio ownership, or save behavior. It remains
unverified until the sealed package, browser, and sustained checks pass.

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
