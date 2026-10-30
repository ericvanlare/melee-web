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

The exact three-file source-main-heap payload component passed independent
review, focused controls, both Release builds and full regression v6. The V4
payload-admission runner/report integration is historical pre-repair evidence:
38 Node controls and full regression 2,456/165 passed on clean ef95e9b1 /
481b68ab, using the exact prior V3 native Release outputs. Do not reuse or
relabel those runtime identities for the repaired source.

The one released browser dry run warmed 300 drawn samples and stopped at sample
299/source frame 176 when CAPTURE returned 517. It did not run sample 300,
draw, owner enumeration, payload storage or restore. The V4 consumer compared
the schema-5 descriptor's total byteSize to a 120-byte header; the actual
producer extent is 2,200 bytes at active counts 4/4 because it includes frame
and submission rows. The raw 517 does not prove the runtime pointer was null.
Root reviewed and applied the three-file fix using the producer's total extent,
canonical uint64 arithmetic and pure size query. Forty actual-producer scenarios
plus pthread passed. The old failed raw report, runtime producer, screenshot and
scratch remain intact. All ten recorded owned PIDs are absent; CLOSE returned
517, while snapshotClose and rendererShutdown separately returned 1.

The descriptor-repair integration passed focused native controls and both
Release builds but its full regression stopped at 2,456 tests/165 skips on one
diagnostics quota assertion (expected 429, got 201). The raw log has no request
timestamps, so a minute-boundary crossing is not proven. Root approved a
one-file test-only clock-pinning candidate; its focused actual-worker and SQLite
checks pass 2/2, including same-minute 201/429 and rollover 201. Sol has applied
the exact frozen file. Focused checked-out diagnostics passed, and the final
integration passed full regression (2,456 tests/165 skips; 540.336 seconds;
log SHA-256 92ff9bbe348693553649737e792cb8629da5248bcfade2c93923346e6d4c3e98) on clean source
7e4fe7817966e3f094cd573dfa9d8541cba1a815 / tree 05f4462bbe4d0ad720429539ded5f35be0d378c2. Native binaries were
built at ef95 plus the reviewed three-file descriptor change; the later commit
adds only the quota-test file. Current runtime artifacts are pinned by the
15-file inventory, with V3 hashes verified against their historical checkpoint.

The root-released V2 packet completed one owner-local payload admission run.
Its 300-sample prefix, sample-299/frame-176 checkpoint, literal sample-300 PAD,
source-frame advance to 177, actual draw, quiescence and cleanup passed. There
are three admissions: command 1 CAPTURE/admit, command 2 STEP_AND_ADMIT and
command 4 POSTDRAW_ADMIT; command 3 draws and command 5 closes. The sample-300
observation, PCM, PAD, draw and GPU fields equal the earlier census-v6 fixture
outputs, although their source/runtime identities differ. V2's raw census still
reports incomplete state and no restore admission. Snapshot capture allocates
diagnostic storage, so its changed-byte totals are not natural-census
attribution. All ranges remain unadmitted and no memory was restored. The raw
report, independent review, release, 34-asset hash verification, five-file
runtime retention and 12-PID cleanup check are pinned in the structured
receipt. Root removed only the empty successful V2 scratch after independent
review; the cleanup receipt is pinned there, and failed evidence remains preserved.
The known magenta/texture artifacts establish no pixel claim.

The machine-local packet is named
`browser-rollback-sprint-20261022-115029-d89e262b`, discoverable through the
coordination checkpoint's `sprint_20261022_current` entry. It retains raw
captures, reviews, failed producers, source hashes and the external candidate.
Owned capture processes are closed; the successful V2 scratch was removed under the cleanup receipt.
Keep incremental builds for the active sprint. The rejected #339 reference
follow-up stays frozen; no service/network workaround is authorized here.
