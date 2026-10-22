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
affected Release builds and an actual constructor/context trace. CI remains a
separate current-head gate. The original sample-zero census failure, missing
legacy fixture initialization and source-header compile failure are retained.
The successful census is incomplete ownership evidence, not rollback or timing
acceptance. Do not repeat it without a new hypothesis.

Next: review the prepared read-only audio/FX range visitor, apply its five-file
patch only against the pinned base files, and run the focused owner control.
Then add a private census adapter for those live ranges with explicit lifetime,
extent and topology checks. Sample the same source-step and completed-draw
boundaries once after independent review. Preserve every unresolved range and
keep restoration disabled. NativeDat allocation ledgers and existing Aurora
buffer descriptors are subsequent reusable boundaries; neither establishes
ownership of the previous capture's unmapped addresses retrospectively.

The machine-local packet is named
`browser-rollback-sprint-20261022-115029-d89e262b`, discoverable through the
coordination checkpoint's `sprint_20261022_current` entry. It retains raw
captures, reviews, failed producers, source hashes and the external candidate.
Owned capture processes are closed and successful Chrome scratch is removed.
Keep incremental builds for the active sprint. The rejected #339 reference
follow-up stays frozen; no service/network workaround is authorized here.
