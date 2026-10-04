# Audio renderer readiness before source startup

**Source identified / Browser checked — bounded layered original CSS startup**

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
