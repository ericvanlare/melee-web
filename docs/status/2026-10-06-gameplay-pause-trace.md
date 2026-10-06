# Gameplay timing pause trace: no natural live pause; induced GPU-process load reproduces the cascade

**Compiled / Browser exercised**

Scope: one natural match with no live-match timing pause and one induced run that
establishes a mechanism only. Not Performance passed.

The [scoped receipt](../evidence/gameplay-pause-trace-v1.json) (SHA-256
`a3057f698d8fc3440c16950a66f9971556618d83f205d78d2ddcd0e4112f0154`)
classifies the open question in tracker #158 item H1, issues #116 and #134 and
the unexplained pause that PR #157 recorded at replay cursor 1645, after
source-reported live match frame 63. It does not close any of them. The cause
of that natural pause is **not identified**. A mechanism that is sufficient to
produce the same kind of callback ramp and the same forced pause was
demonstrated under induced load, and one harness-induced pause in Results was
found and set aside.

## What ran

Both runs used the Release runtime at the PR #157 head (`551938d`, Wasm SHA-256
`e17f89d0e26258ad41daf9e4296bdcd5c90e4a646d63173ac1738448d1436a20`), installed
headless Google Chrome 154.0.8037.98 through `scripts/browser_tools.mjs`
(Web Audio processing active, host speakers muted), ANGLE Metal on Apple M4, a
fresh temporary browser context, and the owned disc. The scenario is the
original CSS, original SSS, Final Destination, four stocks, four Mario CPU9
(`--lineup M` of the CPU9 lineup harness). A per-callback timing table (38
scalars per native callback, from the existing timing hooks, wrappers call the
original first) and a bounded Chrome trace ring were retained. No runtime
source, threshold, frame rate, tick, draw, arithmetic or Resume behavior was
changed. The diagnostic harness edits stay local and uncommitted; their hashes
are in the receipt and the files can be committed separately.

## Experiment 1: natural play

The match ran to its natural end: 10,895 source ticks and draws (match frame
counter 10,657), outcome 2, winner P3. There were **no timing pauses in live
play**. Callbacks were one tick (10,669) or two ticks (113); callback total
p50 5.1 ms, p99 8.6 ms, maximum 12.3 ms; the pending debt never reached one
tick (maximum 0.9996); there were **zero staging-slot waits** in live
callbacks. The largest gap between consecutive callbacks was 33.5 ms (one gap
over 33.3 ms). Five holds at match start and six during the match (source
frames 609, 1763, 2247, 4268, 6772, 8840) were `render_preparation` events:
draws suppressed for three to four callbacks, debt reset, GPU return latency
about 71 ms; none accrued debt. In the traced window (page time 140 to 206 s;
the ring wrapped, so the match start is not traced) the renderer main thread's
longest task was 11.2 ms, the longest GC event 1.3 ms, the GPU-process main
thread was 4.7% busy and the audio worklet's longest task was 0.43 ms.

The only audio underrun in live play (86 frames) coincides with the 33.5 ms
gap at source frame 6031: sampled worklet queue depth was typically 16 to 52 ms
of audio, so a callback gap beyond about 20 to 30 ms can starve it. That is a
continuity finding for the uninterrupted-audio gate, not a pause.

The harness did record a forced pause: reason `simulation_debt`, 19 ticks
against the threshold 8, in **Results** at source frame 532. The harness had
stopped the trace and then read a 7.3 MB capture while the Results clock ran.
Callbacks stayed at their normal cadence (no interval over 25 ms) until a
317 ms gap that brackets the capture read. This is attributed to the observer
by timing correspondence; there is no negative control. It is not a
live-gameplay pause and it is why experiment 2 reads only after the clock has
stopped.

## Experiment 2: induced perturbation (one run)

The same natural match was run with labelled, preregistered host perturbations
triggered by source frame, stopping at the first pause. An idle-host stall
ladder (25, 50, 75 and 100 ms main-thread stalls) produced catch-up callbacks of
2, 4, 5 and 7 ticks with staging waits of 0.15, 0.42, 1.3 and 4.6 ms, and debt
recovered within one or two callbacks; none paused. A second headless page
issuing synchronous WebGL batches of 18 ms (measured alone) for 6.6 s produced
16 callbacks with staging waits (at most 8.4 ms, 56.6 ms total), callbacks at
most 16.6 ms and no pause. Raising that load to 35 ms batches (measured alone;
the longest WebGL task on the GPU-process main thread, which also decodes the
game's WebGPU commands, was 49 ms) produced a **forced pause 1.12 s later** (`simulation_debt`,
9 ticks against 8, source frame 2156). The 23 callbacks before it ramped
from 22 ms (1 tick) to 37 to 41 ms (2 ticks), 33 to 72 ms (3), 66 ms (4), 86 to
108 ms (5) and 120 to 149 ms (7 ticks). Staging-slot waiting totalled 568 ms over
the window's 51 ticks, about 11 ms per tick, and was 108 of the last callback's
149 ms (72%), while simulation and audio stayed at most 0.64 ms per tick and
everything else about 5.7 to 5.9 ms per tick. Submit-to-return latency p99 was
40 ms in this window against 18.5 ms in the 18 ms window (coarse, pooled).
Renderer GC was at most 0.34 ms, the longest renderer task 13 ms and the audio
worklet 0.42 ms. The audio queue drained 0.2 s before
the pause. Caveat: two natural first-use pipeline holds fell within 0.2 s of
the load being raised (the 18 ms window contained six without a cascade).
The CPU-spinner window and the load-plus-stall window were not reached.

## Mechanism and classification

`src/animation_clock.hpp` counts the time between callback starts as debt and
forces a pause above 8 ticks (a callback runs at most 8). In the browser build,
Aurora's staging ring has two slots (`StagingBufferCount` equals
`FrameSlotCount` in Aurora's `lib/gfx/frame.hpp`), each source tick draws once,
and a slot is freed only when the GPU reports the submission complete, so ticks
past the second in a callback wait for completion. With completion latency L, two slots sustain a tick every
L/2, so 60 Hz is stable only below about 33 ms (derived from the ring size, not
measured directly). Above it, a catch-up callback costs more
than the time it recovers, debt grows from its own duration, and the guard
fires within about a second. The natural match never entered this regime
(submit-to-return latency while the clock ran was at most 15 ms in the traced
window).

| Hypothesis | Result |
| --- | --- |
| Draw cost rises | Not supported: draw time per tick unchanged (about 3.5 ms) in the cascade |
| GPU completion lag, staging wait, debt, pause | Sufficient under induced load; not observed naturally |
| JS, GC or observer overhead in live play | Not supported; the harness capture read most likely caused the Results pause |
| Live preparation or pipeline compilation | Not supported naturally (holds absorbed); one confound in the induced run |
| Audio worklet starvation | Not a cause; underruns follow callback gaps |
| Host CPU contention | Untested here |

The PR #157 final callback (153 ms, six ticks) had staging waiting of only
11 ms; its per-tick simulation/audio, draw, begin and end costs were about 10,
3.8, 48 and 3.4 times the natural medians observed here, including phases that
cannot wait on the GPU. It mixes GPU waiting with a process-wide slowdown, so
GPU-process occupancy alone does not reproduce its composition and host
contention at that time remains a candidate. The natural cursor-1645 replay (recorded input) was
not available to this checkout and was not run.

## Proposed fix (not implemented)

Raise the desktop browser frames in flight from 2 to 4 (`FrameSlotCount` and
`StagingBufferCount` in the downstream Aurora patch), so a bounded catch-up burst
does not serialize on GPU completion and tolerance rises from about 33 ms to
about 67 ms. Each staging buffer is 63 MiB, so about 126 MiB more GPU
allocation plus per-slot CPU shadows; keep 2 on mobile until #134 memory work
lands, or right-size slots to used bytes. Source ticks, one draw per tick, order,
arithmetic, the clock threshold and audio are unchanged. Validate with a Release
build and suite, identical per-frame submitted bytes for 600 frames at ring 2
and 4, a repeat of the 35 ms load window and the 100 ms stall under load (no
pause for 20 s), a natural four-Mario match to its end, and memory before and
after. This addresses the amplifier, not an identified natural trigger.

## Limits

One natural and one induced run; no repetition and no distribution. Not
foreground, physical-input, audible, mobile or Performance passed. Host CPU
contention, thermal state and other GPU clients were not isolated. The
experiment budget at this boundary (two) is spent; further runs need a new
hypothesis, for example a retained occupancy and completion-latency scalar in
the existing incident recorder to catch a natural trigger.
