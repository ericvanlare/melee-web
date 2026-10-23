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

The existing status receipt retains earlier #341 source, control and census
results with their exact producer identities and failures. The Aurora v2 census
remains tied to the clean `4c95db9d` base and its original noncanonical frozen
nine-file patch. Full regression v5 found a single canonical-patch serialization
failure. The separately reviewed repair changed only patch serialization; both
byte sequences produce the same prepared Aurora source tree, and the v5 capture
has not been relabeled. The Aurora v1 refusal also included missing arbitrary-
output alias closure. The structured receipt pins the failure, repair and
independent review.

The exact three-file source-main-heap payload component has passed independent
review, focused controls, both Release builds and full regression v6. Its
freeze, control, integration and application receipts are recorded in the status
evidence. The component covers only exact live payload bytes inside the gameplay
arena; other source state and host effects remain unadmitted. Final exact-head CI
is pending at this cutoff. The corrected read-only plan v2 is pinned in the
receipt, and plan v1 remains preserved. No browser restore or aggregate
admission is claimed.

Base-commit CI run `39856673269` applies to `4c95db9d` only. Earlier sample-zero,
fixture, source-header, Aurora v1 and initial v2 control failures remain
retained. Do not repeat a successful capture without a changed hypothesis.
Keep paused drawn-correction work paused and all observed memory unadmitted.

The machine-local packet is named
`browser-rollback-sprint-20261022-115029-d89e262b`, discoverable through the
coordination checkpoint's `sprint_20261022_current` entry. It retains raw
captures, reviews, failed producers, source hashes and the external candidate.
Owned capture processes are closed and successful Chrome scratch is removed.
Keep incremental builds for the active sprint. The rejected #339 reference
follow-up stays frozen; no service/network workaround is authorized here.

The local source checkpoint `853aae6f36608bd62a4eeb1d2594985178625a38`
(tree `35a2fe2fcc8f231d9ea94d5efca37559b3feda66`) adds an opt-in stage-arena
owner diagnostic. Its sample-300 census is linked from the canonical receipt;
the owner had zero overlap with the observed transition changes, and every range
remains unadmitted. Focused controls, private and ordinary Release builds, and
full unittest discovery pass on that exact source commit. Exact-head CI remains
pending. This does not establish browser restore, complete mutable-state
ownership or rollback performance; the draft stays open and no merge is claimed.

Stop further allocation-owner expansion after this negative result. The accepted
`restore-boundary-design-after-stage-falsification-v2.json` and root review permit
only an external candidate and focused controls for a one-shot source-payload
admission dry run. Capture, admit before/after the existing sample-300 tick and
after actual draw, then destroy; no restore. Any topology/lifetime refusal stops
the experiment. Unknown source globals, dependencies and host effects remain
closed gates. The earlier `bd7a393d` CI is complete (12 successes, two expected
skips); the new checkpoint needs its own exact-head CI.
