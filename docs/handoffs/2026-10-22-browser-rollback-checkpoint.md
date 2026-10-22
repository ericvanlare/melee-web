# Browser rollback ownership checkpoint

Branch: `codex/browser-no-draw-restore`, based on `130438fd2b86d00cfec98cd2ba0a5f66084ae15c`.
Milestone: [#341](https://github.com/ericvanlare/melee-web/issues/341).
The branch remains a draft until the same-rendered-browser restore, strict
no-draw comparison and present-frame gates pass. No browser restore is exposed.

The [existing evidence boundary](../status/2026-10-12-match-only-rollback-feasibility.md)
and its [structured receipt](../evidence/match-only-rollback-feasibility-v1.json)
bind the owner controls, source manifest and completed read-only browser census.
Use their hashes for producer identity; the capture preceded this commit and
must not be relabeled as produced by the later checkpoint commit.

The current checkpoint has independent source/result review, full regression,
affected Release builds and an actual constructor/context trace. Checkpoint `12f1008e` also has all 14 exact-head CI checks complete: 12
successes and two expected skips. New uncommitted work needs its own validation. The original sample-zero census failure, missing
legacy fixture initialization and source-header compile failure are retained.
The successful census is incomplete ownership evidence, not rollback or timing
acceptance. Do not repeat it without a new hypothesis.

The read-only audio/FX range visitor and browser adapter now pass focused
controls and independent source/result review. The completed third census
attributes live audio/effects allocations with separate lifetime tags at all
three boundaries. Every range remains unadmitted. Its exact runtime, manifest,
map, inputs, raw result and process cleanup are retained separately from the
previous census. Do not repeat either successful capture without a new hypothesis.
The private and ordinary Release builds pass; the extension's full regression
passes in `full-regression-v3.log`. The source-review hash correction is retained
in `audio-range-adapter-source-review-v2-pin-correction-v1.json`; it changes no
source or result, and the original review remains untouched.

The match-context range query now has independent source and raw-result review.
Its descriptor and error outputs refuse aliases into the live owner before any
write, with actual-source nonmutation and subsequent healthy-step controls. The
one read-only browser census completed; it is not an aggregate restore. The
existing status receipt retains all exact producer, validation and remaining-gap
identities. Do not repeat this successful capture without a new hypothesis.

Full regression also passes; exact-head CI for this extension is still pending.
Next: checkpoint this result and review the bounded design for
a pure observation accessor reusing Aurora's existing staging-shadow descriptor.
The existing descriptor pumps events, so it cannot be called unchanged as a
read-only census. This work identifies host-owned bytes for the no-draw boundary;
paused drawn-correction work remains paused. Keep all memory unadmitted until
the owner contract closes. Broad NativeDat traversal and libc/heap rewind remain
deferred or refused; preserve all prior decoder, fixture and review failures.

The machine-local packet is named
`browser-rollback-sprint-20261022-115029-d89e262b`, discoverable through the
coordination checkpoint's `sprint_20261022_current` entry. It retains raw
captures, reviews, failed producers, source hashes and the external candidate.
Owned capture processes are closed and successful Chrome scratch is removed.
Keep incremental builds for the active sprint. The rejected #339 reference
follow-up stays frozen; no service/network workaround is authorized here.
