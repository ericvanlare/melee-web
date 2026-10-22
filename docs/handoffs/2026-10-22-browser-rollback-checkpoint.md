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

Next: implement the bounded one-row match-context range read specified in
`dynamic-owner-bounded-design-v1.json`. Source proves that this owner changes
during the measured step, and its existing epoch/world/callback guards can be
reused. Defer broad NativeDat wrapper traversal; direct arenas are a fallback,
not the current implementation. No libc traversal or inferred address ownership. Keep restoration disabled until the
owner contract closes. Existing Aurora descriptors remain a later, separate
draw-range diagnostic. The earlier decoder and test-fixture failures remain
retained alongside the independent review that found them.

The machine-local packet is named
`browser-rollback-sprint-20261022-115029-d89e262b`, discoverable through the
coordination checkpoint's `sprint_20261022_current` entry. It retains raw
captures, reviews, failed producers, source hashes and the external candidate.
Owned capture processes are closed and successful Chrome scratch is removed.
Keep incremental builds for the active sprint. The rejected #339 reference
follow-up stays frozen; no service/network workaround is authorized here.
