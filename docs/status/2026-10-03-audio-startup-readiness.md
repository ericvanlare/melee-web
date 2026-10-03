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
this independent production change starts from current main02231e9. Final
commit CI, affected build/package audit and artifact browser validation are
pending. The separate PR150 persistence work and its retained browser failure
remain independent.

The development whole-session replay button directly invokes native launch and
bypasses the public `start()` method. A retained performance overlay that changed
only `start()` therefore did not exercise the gate. The shared owner now exposes
`waitForAudioRender` for that replay handler immediately before native launch;
its extracted-handler test verifies delayed and failed readiness. Legacy
single-match replay construction remains outside this startup boundary.

Native source clocks, simulation, PCM production, queue capacity and manual
Resume behavior are unchanged. A single callback cannot promise future audio
cadence. Physical iPhone, first-load refresh, sustained gameplay, foreground
timing, PCM fidelity and original-state comparison are unclaimed. Failed
reproducers and private evidence remain retained.
