# Bounded match input prediction and source-only correction

**Source identified / Node/Wasm source-only correction component / Browser rollback unproved**

[Milestone #356](https://github.com/ericvanlare/melee-web/issues/356) supplies the
input history needed by a future match rollback adapter. The
[structured receipt](../evidence/match-input-timeline-v1.json) pins the source,
runtime, retained failures and validation. The implementation is `c92da4ad`.
It adds no public online mode or browser restore path.

## Input contract

The scheduler reuses the existing 11-byte PAD and four-port frame format. It
copies local samples, predicts the latest known remote input, and permits at
most seven speculative ticks beyond the contiguous authoritative prefix.
Late input marks the earliest changed executed sample and its dependent suffix
for correction. Replay preserves the original local samples. Revision-bound
plans must be acknowledged after execution; a dirty suffix cannot advance
confirmation or forward simulation. History and inbound lookahead are bounded.

Eleven focused Node cases cover delayed/out-of-order paired scheduling,
prediction limits, input preservation, immutable plans, rejected conflicting
or out-of-bounds arrivals, stale plans and bounded long-stream storage. The
paired state oracle is a toy component. A runtime adapter must separately own
snapshots and restore or terminate after failed or stale execution.

## Actual-source witness

The existing synchronous diagnostic fixture runs two four-stock Marios on Final
Destination with the private incomplete v5 profile. On an Apple M4, Darwin arm64,
using pinned Node 24.19.0, input samples 0–299 reach an interactive, quiescent
checkpoint at observed VS match frame 176. Seven fixed-recipe samples provide
literal inputs, then the checkpoint is restored. Both authoritative execution
and corrected execution use the same raw-input source-step API.

With remote input withheld, samples 300–306 predict neutral from the last
consumed remote PAD. This changes the remote fighter's motion from authoritative
60 to predicted 14 in every sample, and produces different state and PCM bytes.
The eighth prediction is refused. Delivering the actual remote samples requests
correction from sample 300. After restoring and replaying the seven stored
frames, every 1,280-byte observation, 4,272-byte PCM capacity buffer and 44-byte
PAD frame agrees exactly with authoritative execution. The full 128 MiB Wasm
memory image also agrees at the endpoint. Match frames advance 177–183 in each
compared lane; caller input indices remain distinct from that source-owned clock.

The run contains exactly 329 source-step calls: 300 startup, seven input
generation, seven authoritative, one guard-only, seven predicted and seven
corrected. Sample 307 supplies only the eighth-prediction guard's local input
and is restored away; it is excluded from the seven-tick comparison. All eight
capture/restore/endpoint quiescence admissions pass. The fixture and snapshot
owner close, and the recorded process group is absent afterward.

## Repairs and retained failures

The first build exposed an ambiguous iterator initializer in the snapshot
fixture after the existing file-byte type change. Explicit construction of
that same byte range repairs the build; no gameplay behavior changed. Both
diagnostic Release artifacts build successfully; no browser was launched.

The first source attempt failed because the new driver equated input index
with the VS match clock, which correctly stays at zero during the introduction.
The second failed because it demanded quiescence during ordinary startup work
at sample 107. The two-attempt stopping rule triggered a bounded independent
review. The repaired driver reuses the existing fixture's execution contract,
records startup clock stalls, and requires quiescence at actual memory
boundaries. It adds no ticks, waits or draining. Nine mock controls include
transient startup work, rejection of every busy admission, clock errors,
ineffective prediction, corrupted lanes, stack/heap changes and cleanup failure.
Both failed source reports and the earlier build/test failures remain retained.

The successful third run retains sample 107's aggregate drive-busy result after
audio processing with source and bank leaves idle. This does not independently
identify the pending owner. Initial samples 0–123 retain a zero VS match clock;
the observed clock then advances through the checkpoint. No scheduler-counter
getter is exported by this fixture, so no separate scheduler-counter observation
is claimed.

Independent raw-result review passes, including recomputed lane hashes, literal
bytes, source/runtime/asset identities and cleanup. The final full regression
passes on executable source `c92da4ad`: 3,003 tests with 211 skips. Its recorded
process group is absent. Final documentation edits are reviewed separately;
publication-head CI and integration are tracked on the milestone issue.

This is same-instance source-only correction. It does not establish drawn
equivalence, browser renderer/audio-sink/async-host restoration, original or
patched-Slippi parity, complete profile behavior, KO/respawn coverage, a combined
frame budget, network integration or a complete match. The unresolved browser
boundary remains [#341](https://github.com/ericvanlare/melee-web/issues/341);
the rejected renderer-owner diagnostic and UCF reference experiment remain
frozen. Human input, foreground timing, audible output and two-machine gates
remain separate.
