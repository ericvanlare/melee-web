# Repository history audit

The [September 24 pre-freeze refresh](PUBLICATION_PRE_FREEZE.md) records the
newer ref inventory and finding dispositions separately. The original review
below remains historical evidence; neither snapshot freezes future refs.

The September 20, 2026 pass covered the fetched remote branches and pull-request
head/merge refs, plus readiness commit
`7930221343adab157a2283adc76fab2aec0407a2`. The
[receipt](evidence/repository-history-audit-v1.json) lists all 80 roots and their
exact object/commit identities: 38 remote branch heads, 37 PR heads, four PR
merge refs, and the readiness head. No remote tags were returned by the fetch.
The union contains 285 commits, 249 distinct trees, 3,528 historical path/blob
pairs and 3,525 regular blobs (58,989,331 unique blob bytes).

The [scanner](../scripts/audit_repository_history.py) checks every historical
path/blob pairing, including deleted files and blobs reused under a different
path. It checks commit and annotated-tag metadata, refuses shallow/replaced or
grafted histories, and uses the content policy from an explicit commit. Any
commit/tag metadata or blob above the 8 MiB bound makes the audit incomplete;
an oversized object is never silently skipped. It
prints counts, never matched contents. Reports contain rule/path/object
identities and hash-only email inventory, not raw secret values or author
addresses. A nonzero finding result requires review, even for historical test
fixtures; the scanner does not clear it automatically.

## Findings and disposition

There were no credential-pattern findings in the selected blobs or metadata,
and no prohibited raw disc/extracted-game filenames were found. Forty findings
remained under the current content rules and were individually classified:

| Findings | Inspection | Disposition |
| --- | --- | --- |
| Six older binary PNG versions | Viewed all six: maintenance landing page or empty player/toolbar, no game scenes | Historical project UI documentation; retain |
| 21 encoded renderer-seed versions | Bounded base64/gzip decoding, SQLite schema inspection, decoded credential-pattern scan; each has only Aurora schema and pipeline-cache tables/indexes | Generated renderer metadata; retain under the disclosed data-risk decision, with no root MIT grant |
| 13 personal-path matches across historical test versions | Inspected matched lines; all are the four synthetic home-path rejection fixtures | Retain history; the readiness branch constructs the same test inputs at runtime |

The receipt records every flagged blob and example containing commit. The
current guard's exceptions were not broadened to suppress these old findings.
Raw reports, decoded seed inventories and inspected PNGs remain under ignored
`work/public-readiness/`. Known GPL and recovered-source history is retained;
a secret/asset pattern pass does not establish permission or license compliance.

## Repeat at the publication checkpoint

Use a fresh namespace for each remote inventory so its scope is explicit.
Fetching into this namespace does not check out a branch, rewrite history or
change remote refs. These commands include closed PR heads and currently
available GitHub merge refs as well as branches and tags:

```sh
git fetch origin \
  '+refs/heads/*:refs/publication-audit/next/heads/*' \
  '+refs/tags/*:refs/publication-audit/next/tags/*' \
  '+refs/pull/*/head:refs/publication-audit/next/pull/*/head' \
  '+refs/pull/*/merge:refs/publication-audit/next/pull/*/merge'
python3 scripts/audit_repository_history.py \
  --ref HEAD --namespace refs/publication-audit/next --policy-ref HEAD \
  --output work/public-readiness/history-next.json
```

Choose a new namespace and output filename on a later run; reports refuse to
overwrite existing evidence. Exit `0` means no findings, `1` means a completed
scan with findings to review, and `2` means an incomplete scan. Preserve the
exact refs, policy commit, scanner hashes, findings and review dispositions.
Recheck changed tips before visibility changes; this receipt does not freeze
other tasks' ongoing work.

The focused tests use real Git repositories to cover deleted payloads, the same
blob at multiple paths, unmerged branches, empty ancestors, nested tag messages,
metadata redaction, oversized objects and incomplete-history rejection:

```sh
python3 -m unittest discover -s tests -p test_repository_history.py -v
```

Scope limits: no GitHub-internal unavailable refs, reflogs, unreachable objects,
LFS payload retrieval, deliberate alternative encodings or exhaustive detection
of arbitrary secrets. No LFS pointer payload was identified in this reviewed
tree/history inspection. GitHub discussion, logs and artifacts have their own
[audit](GITHUB_PUBLICATION_AUDIT.md). The final snapshot's content check and the
owner's visibility decision remain separate cutover steps.
