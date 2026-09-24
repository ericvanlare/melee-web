# Publication preparation before the final freeze

This September 24, 2026 checkpoint prepares [issue #2](https://github.com/ericvanlare/melee-web/issues/2)
while the final runtime change is still in progress. Its source baseline is
`de048792af1144c23a5d10d28fe2270cab430d82`. It is not the publication commit.
The repository remains private, and the final ref inventory, visibility
decision and cutover verification remain outstanding.

PR [#54](https://github.com/ericvanlare/melee-web/pull/54) supplied the scoped
license, provenance decisions, contributor guidance, content guard, history
auditor and prepared GitHub policies. The [publication checklist](PUBLIC_REPOSITORY_CHECKLIST.md)
remains authoritative for completion; the [cutover procedure](PUBLICATION_CUTOVER.md)
contains the settings and cache-cleanup operations.

## Evidence that can be prepared now

The separate receipts preserve the exact inventory and observation boundary:

- [Source and reachable history](evidence/publication-pre-freeze-history-v1.json).
- [Issues, pull requests and other GitHub discussion surfaces](evidence/publication-pre-freeze-discussions-v1.json).
- [Actions logs and artifacts](evidence/publication-pre-freeze-actions-v1.json).
- [GitHub settings and prepared contribution policies](evidence/publication-pre-freeze-controls-v1.json).

These are publication-content and configuration checks. They establish no
additional gameplay, pixel, PCM, performance or third-party permission claim.
Raw API responses, downloaded archives, scanner helpers and reports are retained
under ignored `work/publication-pre-freeze/`. Receipts identify their hashes;
the raw inputs are not attached to the preparation PR.

The history scan covers 431 commits across 94 selected roots. Its 42 findings
are reviewed historical UI images, renderer seeds and synthetic path fixtures;
no metadata finding was added. The current renderer seed was inspected as
generated metadata under the existing provenance boundary.

The Actions scan covers all 559 inventoried run logs and 1,121 artifact
archives, including retained downloads that have since expired. All inspected
artifact digests match GitHub's metadata. There were no recognized credential,
non-runner personal-path, archive-shape or incomplete-scan findings. Another
392 expired artifact payloads were unavailable and remain outside the result.
The 240 inspected nested TARs contain directory entries only. Persistent
compiler caches are a separate surface: the controls inventory records 70
entries whose payloads are not covered by the archive scan.

The prepared required check still matches the successful GitHub Actions
`browser-build` check and its application identity. Read-only workflow tokens
and full-SHA action pinning are enabled. Main protection, external-fork workflow
approval and private vulnerability reporting remain unavailable or unverified
while private. Their prepared policy files remain ready for the cutover.

The baseline's [Verify run](https://github.com/ericvanlare/melee-web/actions/runs/35970499795)
passed. A fresh committed-tree content check passed for that source baseline;
the content/history audit regression suites also passed. This documentation-only
preparation does not replace the final runtime change's tests and builds.

## Remaining work after the final change lands

1. Integrate the final runtime change and this preparation through reviewed
   pull requests. Agree on the actual publication commit, then pause merges and
   branch/tag updates for the audit window. Record the exact SHA and every
   remote branch, tag and available PR head/merge ref. A green `main` alone does
   not cover the other history that becomes public.
2. Run the committed-tree guard and history audit against that commit and a
   fresh ref namespace. Compare identities with the pre-freeze receipt; inspect
   every added or changed finding. Retain historical findings and their
   dispositions instead of broadening exceptions to hide them.
3. Refresh the GitHub inventories. Scan new or changed issue/PR text, comments,
   reviews, attachments, releases, run attempts, logs and artifacts, including
   this preparation PR and its CI. Reuse an earlier byte scan only when its
   recorded identity still matches. Record unavailable expired payloads and
   any scan failure explicitly.
4. Bind the final runtime validation and successful aggregate `browser-build`
   check to the publication SHA. Reconcile README, STATUS and source/provenance
   boundaries with the final change. New source or generated data needs its
   own review even when a content scan passes. Preserve the experimental
   description and the scoped root license.
5. Record the owner's visibility decision against that frozen inventory.
   Then follow the existing cutover procedure: stop new Actions execution,
   wait for active/queued jobs to finish or be deliberately stopped, retain a
   fresh cache inventory, remove private-era caches and verify zero remain.
   Keep retained logs, reports and failure evidence.
6. During the authorized public transition, apply and read back main-branch
   protection, required aggregate CI, fork-workflow approvals, read-only token
   permissions and full-SHA action pinning. Enable private vulnerability
   reporting, verify its form without submitting a test report, and confirm
   maintainer notifications. Update SECURITY's availability statement from
   observed evidence. Re-enable Actions with the reviewed workflow and record
   the final settings/visibility receipt before closing issue #2.

Recheck remote refs and GitHub inventories immediately before changing
visibility. Any new commit, run attempt, upload or discussion edit extends the
delta to inspect. If work resumes after the freeze, establish a new checkpoint;
elapsed time and an earlier successful audit do not freeze remote state.

## Scope decisions carried forward

The [owner-accepted provenance assessment](PUBLICATION_PROVENANCE_ASSESSMENT.md)
and [explicit MIT file scope](../LICENSE_SCOPE.md) remain unchanged. Outside
review is optional under the recorded decision. Additional independently
authored files can receive a separately reviewed license grant later; neither
publication nor a new contribution silently extends the current allowlist.

Keep native reference binaries local and keep hosted-player releases under
their separate release procedures. Repository publication is not a new binary
or website release. Full-game completion, broader accuracy admission, issue
templates, a contributor backlog and local agent-preference cleanup remain
separate follow-ups. Existing commit attribution is retained under the prior
owner decision.
