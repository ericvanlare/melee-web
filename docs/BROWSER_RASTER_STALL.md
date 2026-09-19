# Browser raster stall investigation

The new diagnostic reproduction for [#33](https://github.com/ericvanlare/melee-web/issues/33)
identifies a browser-owned raster pipeline blocking the GPU queue. It does not
establish a fix or retrospectively identify the exact older September 12 GPU
operation. The [Link compilation fix](LINK_GPU_STALL.md) is already merged and
deployed; its port-owned pipeline cause is separate.

## Reproduction and retained evidence

A frozen two-slot experiment used the exact historical 14-file runtime bundle,
including Wasm `66a37092…`, and the existing 3,892-update Fox/Marth Dream Land
development recipe. The official Chrome for Testing 153.0.8010.36 package has
the historical engine revision, but is a different distribution from consumer
Chrome. Package hashes and the successful headed identity preflight are bound
in the [receipt](evidence/browser-raster-stall-v1.json). The package's strict
code-signature verification failed; no resigning or verification bypass was
performed. These are profiled diagnostics, not acceptance timing or a
browser-version fix comparison.

The first slot reproduced a **104.370 ms native callback**, including
**94.815 ms across 29 staging waits** and two source updates/draws. It created
zero live pipelines. The full replay retained three native target misses, one
native hard failure, five browser hard gaps and 22 audio-underrun frames.
The worst browser gap was 122.425 ms.

Before its raw directory disappeared during analysis, the complete Chrome
trace was parsed and its clock-aligned event excerpt saved separately. That
excerpt contains a 137.405 ms `BrowserRasterWorker` task, a nested 137.201 ms
`RasterDecoderImpl::DoEndRasterCHROMIUM::Flush`, and a 135.797 ms asynchronous
pipeline-initialization worker. Its Graphite label contains
`CoverBoundsRenderStep[NonAAFill]`, `HardwareImage(0)`, `AlphaOnlyPaintColor` and
`SrcIn`. The GPU main task overlaps 94.453 ms of the failed callback.
These nested wall intervals are not added and are not GPU execution times.
Two page/Chrome markers agree within 5 microseconds.

The original directory's loss has no established cause. The full browser
report and full Chrome trace remain unavailable. The exact previously printed
callback and aggregate metrics were recovered from the durable task log; they
are explicitly partial report evidence. A temporary raw kernel recording from
the same capture was recovered, hashed and imported into Instruments. Its
process identities match the captured browser, GPU and renderer. After
restricting the all-process export to GPU PID 13340, 76,548 scheduler rows cover
page time 35,974.538–48,437.590 ms, enclosing the entire failed callback at
36,758.330–36,862.700 ms. The observed GPU main thread is blocked for 97.045 ms,
and the relevant worker for 103.402 ms. Each has less than 0.05 ms of recorded
preemption within that callback. The wall-clock bridge has approximately 2 ms
uncertainty. Recovery is not a new gameplay recording and does not recover the
missing Chrome/report bytes.

The second and final frozen slot was used because primary raw coverage had
become unavailable. It completed all 3,892 updates/draws with 10.665 ms native
and 23.925 ms browser maxima, no target misses, hard gaps, audio underruns or
live pipelines. It failed the focus gate at page time 70,522.415 ms. Its raw
outputs were backed up before further analysis. It neither reproduces nor
classifies the first failure; neither slot is an acceptance pass.

## Ownership and remaining gate

Chromium's [153.0.8010.36 context factory](https://chromium.googlesource.com/chromium/src/+/153.0.8010.36/content/browser/compositor/viz_process_transport_factory.cc#L525)
creates `BROWSER_RASTER_WORKER` for GPU rasterization of UI tiles, using the
browser's compositor context and UI-priority stream. Its
[context metadata](https://chromium.googlesource.com/chromium/src/+/153.0.8010.36/tools/metrics/histograms/metadata/gpu/histograms.xml#L620)
distinguishes that context from `RendererRasterWorker`. The observed handler
therefore identifies browser-owned UI raster work, not the port's GX pipeline
creation. The exact UI tile is not identified. The pipeline label is a generic
Graphite description, not a Melee asset or DOM-element identity.

No page CSS, canvas dimensions, source input, arithmetic, draw ordering or
staging ownership was changed to hide the stall. The fixed desktop profile's
640×480 logical source surface has a 1280×960 backing store at DPR 2; those
sizes must not be confused with its CSS display rectangle.

The recovered GPU main stack records a 135.767 ms condition-variable wait,
overlapping 93.998 ms of the failed callback. The corresponding worker records
81.611, 7.359 and 6.417 ms `kevent_id` waits and an 8.698 ms Mach-message wait.
These are blocked synchronization/IPC intervals; the owned threads have little
recorded preemption. The raw imported frames are addresses. A separate symbol
resolution in an owned process maps the worker's system frames to
`MTLCompilerScheduler::buildRequest`, `newLibraryWithSource` and
`MTLCompiler::compileFunctionRequestInternal`. The same-boot check, Metal
`LC_UUID` (`493e76d9-74d4-333b-a3b2-e5f9bc86429d`) and load base agree with the
earlier symbolized native capture on this Mac. This supports Metal compiler
request attribution with cross-trace corroboration; the recovered raw XML
itself lacks image metadata. It does not assign the entire pipeline interval
to one compiler call, identify the UI tile, or provide a hardware completion
timestamp.

A fresh audit of the older September 12 trace finds no browser-raster label,
client-ownership field or GPU flow endpoint. Its 115.722 ms GPU task and
114.404 ms Dawn worker therefore retain their generic classification. The
new reproduction cannot supply fields absent from that older capture. The
missing full primary browser artifacts also remain an explicit evidence limit. The historical red, frozen unprofiled development
inventory and both untouched holdouts remain open under the
[causal gate](HITCH_CAPTURE.md#causal-diagnosis-and-holdout-lock). A quiet retry,
a browser upgrade or this separate Link fix cannot turn a retained failure into
a pass.
