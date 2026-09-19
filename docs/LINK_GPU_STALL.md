# Link first-use GPU compilation

Issue [#33](https://github.com/ericvanlare/melee-web/issues/33) now has a
correlated cause for the newly reproduced Link/Young Link stall: renderer
descriptors missing from the bundled seed trigger synchronous Metal shader
compilation during live play. The historical Fox/Marth staging wait remains a
separate, unresolved observation; this diagnosis does not open the holdouts.

## Correlated failure

The exact deployed PR #32 build, `2ee80ab70ac0d697a7abf7cdf337ca216908ea3b`,
entered Link/Young Link on Final Destination through ordinary keyboard events.
The first live drawing callback created four pipelines. Two callbacks later,
native frame 688 waited **482.785 ms** for a staging lease inside a 484.460 ms
native callback, with a 503.160 ms browser interval and an automatic timing
pause. Its own creation counter was zero: the requests came from the preceding
draw, and their driver work was still pending.

The complete Chrome trace identifies four serial `APICreateRenderPipeline`
calls totaling 496.345 ms on the owned GPU main thread. Their labels are
`de423b8a`, `f3f744bb`, `d1a85940`, and `a38c3036`. Native System Trace covers the
entire failure and records 471.752 ms blocked on that same thread. Its syscall
stacks pass through `MTLCompilerScheduler::buildRequest` and
`newLibraryWithSource`; compiler-backed `kevent_id` waits overlap 467.538 ms of
the failed callback. This identifies port-requested compiler work, rather than
inferring unrelated preemption from low thread CPU time.

The trace finalized with exit code zero. Owned process/thread identities agree
across collectors, exported native rows enclose the failed interval, and three
page/Chrome clock anchors agree within 3 microseconds. Wall intervals are not
GPU execution times. JavaScript completion reactions are not hardware timestamps.
The [compact evidence](evidence/link-gpu-compilation-v1.json) binds the raw
local browser/native exports and records the clock and retention checks.

## Candidate correction

The seed preserves every field of all 548 existing records and appends 78
portable GX descriptors: 74 from the previously verified complete Link/Young
Link development replay, plus the four exact ordinary-menu first-draw
descriptors named by the GPU trace. It now contains 625 GX pipelines and the
existing shader record. The decoded database is 2,613,248 bytes, SHA-256
`b935fd741f029cd12afb5ac584ef943e51bb00c34c689ae44b1e75d6ae81a8ce`.

Descriptors were exported only after native teardown and reviewed through the
existing append-only cache tool. Source archives and Dawn driver cache bytes are
excluded. The renderer can prepare these descriptors through its existing
startup path; source simulation, draw order, staging ownership and timing guards
are unchanged. No async-pipeline API or additional warmup draw is introduced.

The descriptor discovery replay matched the prior independently compared state
and draw captures byte for byte: 7,070 updates and 7,064 draws. Its collector
failed after saving the required DB/WAL because its file allowlist rejected an
additional driver-cache export. That terminal failure remains retained; the
required files were reviewed separately, and no driver-cache data was merged.
The first export is now checkpointed. Its original DB bytes were recovered
by an identical hash from another export, but the original WAL bytes are
unavailable. The old receipt is retained with that limitation.
A separate read-only re-review verifies all 74 payloads against the saved review
hashes, the other four against their intact DB/WAL receipt, and every field of
all 548 prior rows. The final seed is a fresh Aurora schema containing exactly
those records; no candidate database pages or Dawn driver-cache bytes were copied.
This establishes the final seed's contents without claiming raw WAL recovery.

The ordinary Link route completed 450 updates with two recorded resumes; this
was discovery, not performance acceptance. The reversed-player discovery timed
out in stage select before gameplay and remains an incomplete attempt.

A focused regression fails on the old seed for all four traced descriptors and
passes with their exact reviewed bytes in the new seed. Both Release targets
build; the full suite passes 957 tests with 44 optional skips. Package audit,
exact local HTTP checks and all ten public-player browser checks pass.

The candidate's complete state replay again matches all 7,070 updates and
7,064 draws byte for byte, now with zero live pipeline creation. That
instrumented capture includes a live screenshot and retains one 82.845 ms
browser gap, two native target misses (20.865 ms maximum), 107 audio underrun
frames and 66,846,720 bytes of heap growth. It is state-comparison evidence,
not a performance pass.

Separate bounded public checks now pass all four predeclared segments: both
Link/Young Link player orders, each cold then warm, using ordinary DOM keyboard
events through CSS/SSS and 450 gameplay updates. Each segment has zero manual
resumes, live pipeline creation, native target misses, browser hard gaps, live
long tasks, visibility/focus loss and browser errors. Native/browser maxima are
8.040/24.975 ms and 6.990/21.875 ms for Link first, and 8.065/24.595 ms and
11.370/25.245 ms for Young Link first. Ordinary unload/reimport also succeeds.
The first live draw creates zero pipelines in every segment; preparation is
reported separately. These are short functional and first-use checks of the
silent public player, with no profilers or active screenshots. They do not
establish full-match timing, audio or physical-controller acceptance.

The earlier reversed discovery route remains a retained stage-select failure.
Its simultaneous directional pulses ended near FD's strict hitbox edge; the
new predeclared route uses source-derived sequential pulses to target the
interior. Runtime menu behavior is unchanged.

## Remaining issue boundary

The older a822 attempt retained an 85.945 ms staging wait and an overlapping
114.404 ms Dawn-worker task, without operation-level GPU/native evidence for
that failure. Two bounded diagnostics on the current runtime did not reproduce
it; their long native recordings failed finalization and are retained as invalid
native coverage. A further frozen two-slot control runs the exact historical 14-file bundle on
Chrome 153.0.8010.50 (the original used 153.0.8010.36). Both complete all 3,892
updates and draws without reproducing the staging wait. Native recordings
finalize cleanly, but the primary's scheduler export is empty. The second's
70,604 owned scheduler rows cover the requested startup window; its one
34.120 ms browser gap occurs around page time 65.2 seconds, outside that window.
An unrelated task's profiler was observed during later replay, without causal
attribution. Both slots are retained; no acceptance is claimed.
Neither a passing repetition nor the Link diagnosis classifies that historical
event. Frozen unprofiled development acceptance and the two
untouched holdouts still require the causal gate in [HITCH_CAPTURE.md](HITCH_CAPTURE.md).
