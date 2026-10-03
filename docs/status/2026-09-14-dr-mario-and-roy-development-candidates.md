# Dr. Mario and Roy development candidates

The two physical development recordings now pass complete visible browser
comparison **under their recorded controller-queue schedule**: Roy/Doc on Final
Destination matches 6,965 updates and 6,959 draws; Doc/Roy on Yoshi's matches
11,077 updates and 11,067 draws. Fighter/input/PAD/RNG state, CPU decisions,
camera, subject bones, HUD, magnifier and match outcomes all match the original.
MWRC v6 carries independently observed nonempty input-queue snapshots as platform
inputs; it does not claim original CPU interrupt or live scheduling equivalence.

The first Yoshi's run exposed six tiny throw-position differences. Restoring the
original three fused multiply-adds in `ftCommon_8007E3EC` resolves all six without
changing comparator fields or tolerances. A 1.2-second scalar regression covers
original operands and the failing unfused control. Deliberate host stalls also
preserve the tested input-storage cases and both original Dolphin prefixes;
physical capture under stress is not claimed.

The final state runs measured 16.370/31.950 ms native/browser maxima on FD and
20.045/57.730 ms on Yoshi's. Yoshi's has one native budget overrun, one browser
gap and 22 audio underrun frames. Both runs still create first-use pipelines
(27/35) and grow the heap. Performance is **not admitted**. The historical GPU
stall, pixels, PCM and both unopened holdouts remain open. Release browser/native
builds and 765 tests (36 documented skips) pass. No deployment was made.
See [conditional replay scope](../RECORDED_QUEUE_REPLAY.md) and
[exact evidence and retained failures](../evidence/recorded-queue-replay-v1.json).

### Earlier startup-clock evidence and rejection

Dr. Mario and Roy load their own original data through the shared Mario/Marth
family adapters and are selectable through original CSS/SSS. Native lifecycle
checks cover both orientations, five costumes, specials, damage and reconstruction.

The user's physical Roy/CPU9 Dr. Mario capture now repeats exactly in Dolphin
(6,965 source ticks, 37,825 semantic events). The latest complete visible browser
replay matches every declared state domain: core fighter/input/RNG/PAD state,
CPU decisions, camera, subject/bone transforms, HUD, magnifier and match result.
Roy wins with three stocks, exactly as recorded. Repairs restore captured save
unlock state and original music-selection RNG, Roy's six authored dynamics modes,
source-free match preparation, and original fused joint/vector arithmetic.

The latest bounded replay also matches **all 6,959 original source draws**, with
all 6,965 state updates preserved. A passive original probe recovered the opening
PAD/VI phase without changing guest state; MWRC v5 carries compact clock context.
The periodic model predicts the later batches from source ticks 0–3, without a
recorded draw-index skip list. Legacy recipes and live input keep their existing
policy; live controller phase and other startup/VI configurations remain open.

This instrumented run measured 16.050 ms native / 26.240 ms browser maxima, with
zero pauses, overruns or audio underruns. The historical GPU staging stall remains
open. Cold/warm performance, pixels, PCM, other routes and both unopened holdouts
are not admitted. No production deployment was made. Release runtime/native
builds, the 757-test suite (36 skips) and subsequent focused binding/decoder/report
checks pass. See [clock replay evidence](../evidence/roy-dr-mario-clock-replay-v1.json),
[clock format and scope](../RETAIL_DRAW_CLOCK.md), and
[retained failures](../content/ROY_DR_MARIO_PORT_NOTES.md).

**Independent clock validation failed on Doc/Roy at Yoshi’s:** the fresh original
capture repeats exactly (11,077 updates, 11,067 draws, 58,668 typed semantic
boundaries), but the initial-only model puts one batch seven updates too late.
The original queue check at source 8,527 is delayed relative to the assumed
constant VI poll offset and consumes two samples. Equal final draw counts do not
establish exact cadence. Browser validation was deferred at this already-failing
gate; the earlier Roy/Doc result remains valid only for its recorded case.
See [independent rejection evidence](../evidence/doc-roy-yoshis-clock-validation-v1.json).
A subsequent valid original prefix identifies an audio DMA interrupt preempting
bookkeeping before the queue check; the delayed PAD processing is execution-time
dependent. The binder now rejects this incorrect periodic prediction before
packaging it. See [producer evidence](../evidence/original-audio-queue-delay-v1.json).
