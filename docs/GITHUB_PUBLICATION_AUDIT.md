# GitHub publication audit

This read-only audit covers the live `ericvanlare/melee-web` GitHub
repository as observed on September 20, 2026. It complements the repository
[publication checklist](PUBLIC_REPOSITORY_CHECKLIST.md), which requires a final
review of GitHub history and publication surfaces before visibility changes. The
sanitized machine-readable
receipt is [github-publication-audit-v1.json](evidence/github-publication-audit-v1.json).

The [September 24 pre-freeze refresh](PUBLICATION_PRE_FREEZE.md) has separate
receipts for current discussions, Actions payloads and settings. Keep this
September 20 record as historical evidence; its inventory and control values
are not the latest observation.

The exact API responses, downloaded run logs, downloaded artifacts, and local
scan reports are retained only under the ignored
`work/public-readiness/github-audit/` directory. Archives were inspected with
ZIP and streaming TAR readers. No member was extracted or executed. The audit
did not mutate GitHub settings, send messages, delete content, or change
visibility. The bounded recheck implementation is retained at
`work/public-readiness/github-audit/notes/scan_archives.py`.

## Repository surfaces

The repository is private, uses `main` as its default branch, allows issues and
forking, and has wiki and Discussions disabled. The API returned 38 branches in
one page, no tags, no releases, and two active workflows. There are 53 records
from the all-issues endpoint: 16 issue-only records and 37 pull requests. Of
those, 12 issue-only records and four pull requests are open; four issue-only
records and 33 pull requests are closed. Issue #2, the publication checklist
issue, remains open.

The complete issue, pull-request, and commit-comment discussion surface was
fetched. It contains 7 issue comments, 136 issue events, 0 repository commit
comments, 0 pull-request reviews, 0 pull-request review comments, and 205
pull-request commits. The API has no separate
attachment collection. I scanned all issue and pull-request bodies, issue
comments, reviews, and review comments for known GitHub user-upload URL forms
and found 0 recognized attachment URLs. This leaves a narrow API limitation:
an attachment that is not linked in an exposed text field cannot be enumerated
through these endpoints.

The live default branch is still commit
`2c2b687f5bb7dbfcb3c75dc0394c04bb97f2114d`, and GitHub reports it as unprotected.
The community-profile endpoint reports 28% health and no detected license,
contributing guide, pull-request template, issue template, or code of conduct
on that live commit. Files currently present only in the local worktree are not
part of this live-repository observation.

## Actions history, logs, and artifacts

The workflow-runs endpoint returned 498 completed runs across five pages
(`100, 100, 100, 100, 98`): 347 successes, 73 failures, and 78 cancellations.
The event split is 297 `push`, 196 `pull_request`, and 5
`workflow_dispatch`. All 498 run-log archive endpoints returned successfully;
9 responses were valid empty ZIP archives associated with cancelled runs, so
those runs have no retained log payload in the returned archive.

The artifacts endpoint returned 808 records across nine pages
(`100,100,100,100,100,100,100,100,8`). Of these, 626 were unexpired and all
626 downloaded successfully. The other 182 are expired and could not be
downloaded; their API metadata remains in the ignored raw response set. No
download failed, and no archive exceeded the bounded member scan window.

Across the 1,124 downloaded archives, the scan covered 3,774 ZIP members and
675,953,482 uncompressed member bytes. It found no path-traversal members,
symlinks, parser errors, or scan-size-limit hits. The 150 nested compiler-cache
TAR members all contain only an empty directory entry; none contains a
nonempty source, object, license, or executable member. Run-log archives contain
text/JSON log members. Artifact archives contain JSON reports, text reports,
and those empty compiler-cache TAR shells; no `.wasm`, object, static-library,
or other raw compiler-output member was present.
No `license`, `copyright`, `GPL`, or `MIT license` term occurred in the
artifact-member scan; this describes the retained CI bytes and does not replace
the repository's separate license and provenance inventory.

### Persistent compiler caches

A subsequent cache API inventory returned 75 entries totaling 8,033,127,560
bytes, including nine on `main`. These are persistent Actions caches, separate
from the empty compiler-seed TAR artifacts above. The workflow writes compiled
objects under `.cache/ccache`; the artifact inspection does not inspect those
persistent cache payloads. The receipt retains their IDs, keys, refs and sizes.
Only cache metadata was reviewed; no claim of zero findings covers their bytes.

[GitHub documents](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching)
that fork PRs can restore base-branch caches. Treat those caches as an exposure
surface at public cutover. The initial response disabled public caching; the
[targeted cache review](COMPILER_CACHE_REVIEW.md) supersedes that blanket
restriction. Public and private CI retain ordinary compiler caching in a fresh
namespace. Unreviewed legacy caches are still removed once, after Actions has
stopped and before changing visibility. Preserve logs, reports and the
inventory; cache removal is not a substitute for reviewing retained evidence.
The [cutover procedure](PUBLICATION_CUTOVER.md) contains that sequence. No cache
was deleted during this audit. The metadata-only observation above is unchanged;
it does not claim that the later byte inspection covers every historical key.

The downloaded logs and artifacts contain compiler and source terminology and
CI-generated runner paths. Their high-confidence secret scan found no private
key, AWS/GitHub/Slack token, API-secret pattern, or long credential assignment.
Every detected `/Users/...` or `/home/...` path matched a recognized synthetic
GitHub runner identity; there were no non-runner personal-path findings. The
scan is pattern-bounded and is evidence about these retained bytes, not a
universal secret or rights clearance.

## GitHub controls

The read-only Actions settings report `enabled: true`, `allowed_actions: all`,
and `sha_pinning_required: false`. Default workflow permissions are `read`, and
Actions cannot approve pull-request reviews. The `false` SHA-pinning
setting is a publication finding even though the workflow files currently use
pinned action references; an owner should decide whether to enforce it.

Main-branch protection and repository-ruleset queries both returned HTTP 403
with GitHub's plan/private-repository restriction. Fork pull-request approval
settings returned HTTP 422 because that setting is unavailable for a private
repository. These controls are therefore unverified, not absent by inference.

Private vulnerability reporting returned HTTP 404, vulnerability alerts
reported as disabled (HTTP 404), and automated security fixes were disabled.
Wiki and Discussions queries returned disabled/unavailable responses (HTTP 404
and HTTP 410 respectively). These are explicit unavailable surfaces for the
current private repository state.

## Content and remaining work

The content guard checked 1,047 tracked entries at readiness commit
`7930221343adab157a2283adc76fab2aec0407a2` and returned zero findings.
The GitHub API JSON corpus and all downloaded logs/artifacts also
returned zero high-confidence secret findings. This does not settle ownership,
license scope, recovered-source publication, generated-data provenance, or
binary corresponding-source obligations; those remain governed by the source
license and publication checklist documents.

Before visibility changes, the owner still needs to freeze the publication
commit, reconcile the 38 branch heads and reachable history with that commit,
and repeat this audit against the final state. Once GitHub makes the controls
available, configure and verify main protection/rulesets, fork-workflow
approval and private vulnerability reporting. SHA-pinning enforcement was
subsequently enabled as recorded below. Recheck Actions logs and artifacts after the
cutover because the current private repository's retained CI material will
become part of the public surface. The 182 expired artifacts cannot be audited
from their original bytes without an independently retained copy.

## Subsequent control change

After the read-only inventory, the lead enabled repository-level full-SHA
pinning for Actions and verified the API returned `sha_pinning_required: true`.
Actions remained enabled and the allowed-actions selection remained `all`.
The original observed settings above are retained as the before-state. See
[the prepared cutover policies](PUBLICATION_CUTOVER.md) for remaining controls.
