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
The local recorder makes no network requests. Automatic delivery and Settings
controls belong to the separate reporting prerequisite.

Packaging embeds schema version, actual committed source SHA, runtime graph hash
and build profile in safe HTML metadata. Audits bind it to the exact runtime
graph and producer. Environment is derived independently from exact known hosts;
unknown hosts stay unknown. A production-profile package on a staging host is
staging, so promotion does not require changing tested runtime bytes.

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
