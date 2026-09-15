# Captured original PAD/VI draw cadence

MWRC v5 adds 40 bytes of input-clock initialization after the v4 save masks:
four big-endian u64 values (PAD period, VI period, next PAD deadline and first
VI-gated queue check), then u32 startup draw allowance and zero reserved flags.
Epochs are relative to the initial controller-queue check in a common emulated
CPU clock. There are no expected states or expected draw indexes in the recipe.

The current model explicitly supports the observed NTSC two-XFB startup:
one retained PAD sample after `HSD_PadFlushQueue(LEAVE1)`, one free and one
displayed framebuffer, nominal PAD period 8,100,000 CPU ticks, and VI period
8,108,100 CPU ticks. The original waits for framebuffer copy availability
after source drawing; it can therefore draw the retained sample and the next
sample before the first VI-gated queue check. Unsupported contexts fail.

`RetailDrawClock` advances periodic alarm arrivals and snapshots the queued
sample count at each VI-gated check. An empty queue waits for the next PAD
alarm; a nonempty queue consumes its snapshot before one source traversal.
`SourceFrameSequence` retains an open replay batch across browser callbacks.
Legacy recipes and live input retain the existing traversal after each tick.
This does not establish live controller sampling or controller-to-photon timing.

## Original measurement

The passive diagnostic patch applies after the existing pinned reference
observer overlay. It is not installed into the capture app or public build.
Use an isolated source/build/application copy, the frozen original configuration
and fixture, and strict MWRI replay. Never use debugger pauses to collect phase:
earlier debugger probes invalidated the exact SI input timing.

The patch records supplemental `clock_probe_v1` Progress events through the
existing bounded writer. Normal semantic payloads remain unchanged. Hooks cover
the original PAD alarm, queue-count return and HSD post-retrace entry/return,
plus existing observation boundaries. Reads include the PAD cadence record,
queue, HSD video state, retrace count, CoreTiming ticks, time base and the original
OS system-time adjustment. No guest writes or injected ticks occur.

Original `OSAlarm.fire` is in system time, rather than raw time-base time:

```
next_pad_relative_cpu = 12 * (alarm.fire - OS_time_adjust - time_base)
first_vi_poll_relative_cpu = first_VI_queue_check - initial_queue_check
```

CoreTiming timestamps at these JIT hooks are block-clock observations. Do not
claim instruction-level distances from them. The observed source ordering is
exact; the tested recipe's prediction is unchanged across the 11-cycle
time-base conversion uncertainty in either direction.

`scripts/bind_retail_clock.py` binds a hash-verified v4 recipe and private clock
JSONL to a new v5 recipe. Independently validate complete strict MWRI replay and
compare all normal semantic events first. The binder consumes only source ticks
0 through 3 and verifies the supported queue/framebuffer startup. Its sidecar
contains compact context and binding hashes; raw observations stay outside Git
and the build.

## Bounded verification

The Roy/CPU9 Dr. Mario recording is the development case. Two passive prefixes
preserved exact input timing and normal semantic events; the final passive
capture completed all 6,965 ticks and 6,959 draws naturally. All 37,822 typed
semantic boundaries match the original, excluding only the three control
records (handshake, start and end) from the typed-boundary comparison.

The model initialized from the opening measurements predicts all six later
two-update batches exactly. A shifted-phase negative control disagrees, and
conversion-quantization controls retain the prediction. Fast tests verify these
original observations, unsupported-context rejection, immutable input bytes,
hash failures, cross-callback batching and independent draw-count validation.
One complete frozen visible browser replay is the final state/cadence gate.
See [the evidence ledger](evidence/roy-dr-mario-clock-replay-v1.json).

Other startup histories, VI modes, framebuffer configurations, live sampling,
pixels and PCM require their own evidence. Capture-app integration of the
optional passive clock observer is a separate owner handoff. The historical
GPU staging stall and both unopened human holdouts remain open.
