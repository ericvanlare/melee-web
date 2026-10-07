# Exact source-step and draw accounting for local lockstep

**Source identified / Browser exercised: local loopback / Not retail compared or performance admitted**

Two fresh headless Chrome peers completed original CSS → SSS → four-stock
Mario/Final Destination → original Results → original CSS. Each consumed exactly
5,084 inputs, performed 5,084 simulation steps and completed 5,084 corresponding
source draws. Their declared per-tick checksum streams matched. This fills the
draw-accounting gap in [the earlier A2 run](2026-10-06-local-lockstep-a2.md) and
[#163](https://github.com/ericvanlare/melee-web/issues/163).

The [portable receipt](../evidence/local-lockstep-source-accounting-v1.json) binds
the clean Release producer and runner `6c5d6c7`, all 32 packaged build files,
local/HTTP identity checks before and after both runs, callback records,
checksums, screenshots, logs and cleanup reports. Fresh terminal controls use
runner `3f48bf4` with the same native producer; the receipt records that source
difference. Historical controls retain their identities in the earlier receipt.

## Observation boundary

The harness observes existing native `menuRuntimeTiming` fields. It installs at
the native start-identity barrier with cursor zero, before peer confirmation or
input publication. The original callback still runs normally. Each peer retains
a bounded table of callback ID, validity, source steps and source draws; missing,
repeated, reordered, invalid or overflowing observations fail verification.
Every retained callback must have equal step/draw counts, and both sums must
equal the terminal native cursor. No native scheduling or rendering code changes.

These are the input-associated draws counted by `SourceFrameSequence`.
Preparation-only draws, GPU completion, submitted bytes and pixels are outside
this counter's meaning. The peers need not have equal numbers of browser
callbacks.

## Observed results

The reduced eight-tick CSS probe passed first. Both peers accounted for all eight
steps and draws; withholding a remote sample held cursor 3 for 120 ms before the
existing protocol resumed exactly once. The probe retained original CSS images
and WebGPU observations on both peers.

The subsequent full route retained 5,174 contiguous native callbacks on alpha
and 5,187 on beta. Each callback had equal source-step and draw counts, with no
invalid rows, observer errors or overflow. Both streams contained 5,084 matching
records and SHA-256
`4ac68c125ef7de127db9ab6c56e3fd40665bd42f02bff7ad8a75519b461d82a0`.
There were no automatic timing resumes. Both peers retained ready-draw images
and GPU checks at all five route boundaries. Scene phases stayed stable during
screenshots; cursors continued advancing, so these are not fixed-tick images.

All twelve probe/full-route screenshots were inspected. The existing magenta
Final Destination geometry remains visible on both peers and is tracked in
[#175](https://github.com/ericvanlare/melee-web/issues/175). SSS and Results images
include their entry animations. This result makes no pixel-fidelity claim.

Fresh input-flip and disconnect controls passed with exact source accounting.
The flip first differs at delayed source tick 12, channel 1; both native peers
then hold cursor 16. Disconnect also holds both cursors at 16. Each peer accounts
for all 16 steps and draws, with no automatic resume or fallback.

The first fresh flip attempt exposed a harness race: checksum delivery reached
the correct terminal while the browser drain awaited, then the runner tried to
update the ended peer. That failure is retained. A post-drain terminal check
fixes the runner; the subsequent flip and disconnect runs passed. Native and
packaged runtime sources are unchanged.

## Validation and remaining gates

The full local suite passed 1,928 tests with 141 skips at `f4968e7`. The later
probe screenshot/GPU addition passed the focused contracts and both real-browser
cases; the clean Release runtime build passed at `6c5d6c7`. Final PR-head CI
remains required for integration. The bounded observer's focused controls reject
lost/reordered callbacks, uneven draw counts, overflow, an already-consumed start,
original-observer errors and replacement of the observed callback.

All owned browsers, the loopback relay and HTTP servers closed. Incremental
builds and evidence remain retained for active work. Internet lockstep remains
gated by H1; retail state/PCM comparison, pixels, physical input, foreground
timing and audible-output acceptance remain separate.
