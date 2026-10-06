# Audio renderer readiness before source startup

**Source identified / Browser checked — startup queue-overflow fix; gameplay timing pause remains open**

## Current-main candidate

Current main and staging baseline are pinned to `07bcfdfa6f4531ac464eab44ebecb206daa1f0da`
(PR152; runtime `9c81b55e5bc2366d`). The candidate waits for
[`AudioContext.getOutputTimestamp().contextTime`](https://www.w3.org/TR/webaudio-1.0/#dom-audiocontext-getoutputtimestamp)
to pass the matching AudioWorklet process time before native playback begins. Native simulation,
source clocks, PCM production, queue capacity and game ordering are unchanged.

The earlier same-commit control/candidate pair on `c3ddd39` isolated the
startup failure: the control launched native audio after a Worklet callback at
process time zero, accepted 30 packets (`16,000` frames), then overflowed on
packet 31 at source cursor 31 while the context clock remained `0.008`. The
output-clock candidate waited about `0.56–0.58 s` for device-clock progress and
continued past that failure boundary without queue overflow. These earlier
comparisons are retained in the [v1 receipt](../evidence/audio-output-clock-startup-v1.json).

On current main, a bounded instrumented four-player Mario/Final Destination
replay passed startup and reached live match source frame `63` without an audio
queue overflow. It then paused after a timing disruption at replay cursor
`1645`; no runtime error or browser error was recorded. The capture posted
1,656 PCM packets / 883,199 frames. After pause, the final Worklet snapshot
reported 64 underrun frames (2 ms) and zero overflows. The run did not reach
Results or return-to-CSS and does not establish uninterrupted audio or
sustained performance.

One timing-correlated render diagnostic reached `153.365 ms` around the
disruption, including `66.35 ms` in draw, `30.08 ms` in simulation/audio,
`28.53 ms` in begin, and `28.35 ms` in end. A browser long task lasted 50 ms in
the same interval. The instrumentation changes timing, and this capture cannot
establish which value triggered the pause or whether this interval caused it;
the renderer timing issue needs a smaller reproducer and remains open.

Focused audio/owner/Worklet checks passed, as did the Release runtime build and
full suite (1,883 tests, 141 skipped). A separate bounded WebKit graph-only
check confirmed the production readiness gate can observe output-clock
progress; it is not gameplay or iPhone acceptance. Current-main evidence,
artifacts, hashes and capture limits are recorded in the
[v2 receipt](../evidence/audio-output-clock-startup-v2.json).

This closes only the demonstrated startup queue-overflow boundary. Whole-match
timing, Results/return/repeat-match, P1–P5 release metrics, PCM fidelity,
retail-state comparison, foreground timing, physical controllers and mobile
acceptance remain open.

## Earlier readiness work

A quiet desktop Chrome control observed the second fresh browser context produce
30 PCM packets (16,000 frames) while its audio clock stayed at 0.012 seconds.
Later rendering kept pace, leaving the output queue near its 16,384-frame limit.
A separate contended run overflowed; an independent current-main quiet capture
also overflowed before leaving CSS. These are retained startup observations,
not an identified cause of the reported iPhone pauses or first-load refresh.

The audio adapter currently accepts a running context and a worklet port
acknowledgement. Neither establishes an output processing callback. The change
requests a fresh nonce after native preparation and waits for its acknowledgement
from `AudioWorkletProcessor.process()` before native launch. A five-second
failure is explicit and permits the player to retry; teardown/failure cancels
pending waits, and late acknowledgements cannot release a newer request.

In the private layered comparison, the second context waited 529 ms with zero
PCM before the acknowledgement. Both four- and eight-second original CSS
windows then completed without overflow or underrun. The second context's
maximum queue decreased from 15,936 to 1,685 frames. These are functional
startup observations on headless Chrome154 with speaker output muted and Web
Audio processing active; added observers exclude performance admission.

The [scoped receipt](../evidence/audio-startup-readiness-v1.json) pins the audited
base package, three private override hashes, runner/report hashes, and focused
negative control. That experiment layers JavaScript over the PR150 candidate;
the initial production branch started from main `02231e9`. Its `938a98d`
checkpoint subsequently passed CI, both affected Release builds, package audit,
two fresh-context four/eight-second CSS checks, and public import/start/Eject.
The final package report is SHA256
`f479cd020e0d148770aedac16c59b076defdb0c0ae522deb735613e0d1965d3d`;
the public smoke report is SHA256
`204669a53a7892e88ea627e99f3e390b410e5f394216d328ac6e8d77d6995467`.
These remain bounded desktop startup observations. PR153's review records the
subsequent integration with PR150 and its exact validation/deployment identity.

The development whole-session replay button directly invokes native launch and
bypasses the public `start()` method. A retained performance overlay that changed
only `start()` therefore did not exercise the gate. The shared owner now exposes
`waitForAudioRender` for that replay handler immediately before native launch;
its extracted-handler test verifies delayed and failed readiness. Legacy
single-match replay construction remains outside this startup boundary.

Integration review also reproduced a stop-during-readiness race: a late
acknowledgement could launch after replay teardown. The continuation now checks
the captured replay owner both after readiness and at the native command
boundary. The production-handler fixture fails without the guard and passes
for pending teardown, completed teardown, and a queued launch cancelled before
execution.

A later route-correct whole-session replay did receive the process
acknowledgement, at frame/time zero, and still overflowed in CSS at source cursor
71 (report SHA256
`861862109e123eab102a6de84f9c1e7254752fff1b5a4756f0f078289acdda4d`).
Frame zero is valid under this protocol. The handshake does not resolve that
failure; producer/consumer clock diagnosis remains open.

Native source clocks, simulation, PCM production, queue capacity and manual
Resume behavior are unchanged. A single callback cannot promise future audio
cadence. Physical iPhone, first-load refresh, sustained gameplay, foreground
timing, PCM fidelity and original-state comparison are unclaimed. Failed
reproducers and private evidence remain retained.
