# Public staging graphics startup profiling

**Browser observed / Source identified — startup attribution in progress**

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

The next boundary is a source-bound startup report on the ordinary audio-player:
phase timings, cache-idle state and a bounded pipeline-queue history, available
from the UI while loading. The candidate changes no game arithmetic, inputs,
source order, readiness guards or timing deadlines. No speedup or completed
staging rollout is claimed by this baseline entry.
