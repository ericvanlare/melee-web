# Gameplay replay with recorded input-queue timing

MWRC v6 tests compiled gameplay under the original recording's platform input
schedule. It supplies controller samples and the nonempty queue snapshots that
preceded their consumption. It does not predict original CPU interrupt timing,
prove live controller timing, or establish performance acceptance.

This boundary is necessary because original AI DMA work can interrupt the game
before it snapshots the controller queue. Another PAD sample can arrive during
that interruption, changing how many updates precede the next source traversal.
The Yoshi’s counterexample and startup-only model rejection remain preserved in
[the original diagnosis](RETAIL_DRAW_CLOCK.md). V5 remains a separate startup-only
model; its binder still rejects any disproven queue prediction.

## Format and ownership

After the 16-byte MWRC header and four-byte v4 save masks, v6 stores big-endian
u32 queue-event count, u32 reserved zero flags, then one `(u64, u8)` record per
nonempty queue check. The u64 is the observed emulated CPU block-clock timestamp,
relative to the first check; the u8 is the number of available samples (1–5).
Timestamps must increase strictly. Counts must cover the input frames exactly.
The original setup, PAD history and 44-byte input frames follow unchanged.

The events come from the protected `HSD_PadGetRawQueueCount` return at original
PC `0x803769d4`. The binder verifies source hashes, contiguous sample coverage,
queue capacity and the returned count against the protected queue bytes. It
reads no fighter, RNG, camera, draw-index or result data into the payload.
Raw capture observations remain outside Git and runtime output.

The recorded queue count is explicitly an environmental fixture. It is not an
independent test of the original scheduler. The game consumes one source update
per recorded sample and traverses once when that queue batch closes, following
the original manager's ordering. A batch may span browser callbacks. Relative
emulated timestamps validate the event order; host elapsed time never selects,
drops or merges fixture inputs. The existing host clock still paces execution.

`scripts/bind_retail_queue.py` requires a complete MWRC v4 recipe, a clock JSONL
stream, both SHA-256 identities, and an exclusive output path. Independently
verify the clock observer against the original semantic replay before binding.
A successful bind alone is not game equivalence or gold admission.

## Honest reporting and validation

The browser permits v6 only in instrumented state-capture mode. Its report says
`input_scheduling: recorded_queue` and `scheduling_equivalence: not_evaluated`.
Report validation requires an explicit conditional comparison request and an
independent expected draw count. Full original state and exact source-boundary
comparison remain required; their fields and tolerances are unchanged. The
live per-tick input path is unchanged.

Host stalls can make playback slower without changing which fixture inputs the
game consumes. A diagnostic timing pause/resume is retained in the report and
cannot establish uninterrupted performance. During a physical capture, a press
that the controller backend never samples cannot be reconstructed later.
Dolphin repeatability and deliberate host-stall checks are separate gates: any
emulated input-tick mismatch or observer overflow rejects that evidence.

## Throw-position rounding regression

The first conditional Doc/Roy replay preserved all gameplay, CPU and draw
boundaries but exposed six one-ULP differences in Roy's local throw pose. The
camera bone then amplified those small differences. Read-only probes showed
identical animation-curve operands; the subsequent hip adjustment in
`ftCommon_8007E3EC` was the first differing arithmetic boundary. Original
instructions at `0x8007e4c8`, `0x8007e4dc` and `0x8007e4f0` fuse the multiply/add
after separately rounding the position delta. The downstream patch preserves
those operations on all three axes.

`test_gameplay_throw_smoothing.py` compiles that production function against
three compact original scalar cases. It also checks an unchanged clean joint
and retains the old unfused arithmetic as a failing control. Raw diagnostic
streams stay outside the repository. The complete match comparison remains the
integration check; the scalar test makes later numerical iterations inexpensive.
