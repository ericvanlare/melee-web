# Public repository readiness checklist

This is the working checklist for [issue #2](https://github.com/ericvanlare/melee-web/issues/2).
It prepares the repository for public visibility and contributions. Changing
visibility remains a separate owner action; this checklist does not authorize
deployment, history rewriting, or deletion of evidence.

The starting review was conducted on September 20, 2026 against GitHub `main`
at [`2c2b687f5bb7dbfcb3c75dc0394c04bb97f2114d`](https://github.com/ericvanlare/melee-web/commit/2c2b687f5bb7dbfcb3c75dc0394c04bb97f2114d).
Observations below describe that checkpoint. Recheck the eventual publication
commit and live repository settings before closing the checklist.

The [post-handoff publication checkpoint](PUBLICATION_CHECKPOINT.md) refreshes
history, GitHub-content, source provenance and controls after PR #79 merged and
runtime work stopped. It continues the [September 24 preparation](PUBLICATION_PRE_FREEZE.md).
Its receipts are a baseline for the remaining delta audit; they do not check
off the final freeze, visibility or activation steps below.

Public package audits cover selected deployment files. Repository publication
also exposes source, patches, reference tools, generated material, reachable
history, and GitHub discussion and Actions material. Keep those scopes explicit.
Full gameplay accuracy and roster completion remain separate milestones under
[the accuracy contract](ACCURACY_CONTRACT.md). Use [STATUS.md](../STATUS.md)
for current gameplay evidence and limitations.

## Start here

The owner accepted a thorough internal pass without outside outreach on
September 20, 2026. The [assessment](PUBLICATION_PROVENANCE_ASSESSMENT.md) records
the source/audio/generated-data disposition and accepted uncertainties. The
[root license](../LICENSE) now grants MIT only to an explicit
[file scope](../LICENSE_SCOPE.md). Recovered and upstream material is not
relicensed. Qualified outside review is optional, not a remaining workflow gate.
The [earlier review brief](PUBLICATION_REVIEW_BRIEF.md) remains available if the
owner later wants it.

The [history review](REPOSITORY_HISTORY_AUDIT.md),
[GitHub-surface audit](GITHUB_PUBLICATION_AUDIT.md), and
[cutover procedure](PUBLICATION_CUTOVER.md) distinguish completed preparation
from activation and final verification. No visibility change is recorded here.

Suggested sequence: ownership and license inventory; audio disposition;
repository safeguards and contributor documents; preparation of GitHub controls;
final checkpoint audit and owner-controlled visibility transition. Record an
evidence link or decision when checking an item off. Record any deliberate
deferral and its rationale rather than silently treating it as complete.

## 1. Project license and source publication decisions

Owner decisions, supported by the recorded internal assessment and an explicit
initial license scope. Start with [third-party provenance](../THIRD_PARTY.md),
[dependency boundaries](DEPENDENCIES.md), and [the release review](PUBLIC_RELEASE_REVIEW.md).

- [x] Complete a [first-pass technical inventory](SOURCE_LICENSE_INVENTORY.md)
  distinguishing project-license candidates, identified adaptations, recovered
  Melee/HSD and original SDK material, patches, generated data and reference tools.
- [x] Adopt an affirmative scope for reviewed project-authored files on the
  owner's authority, supported by their recorded authorship and this branch's
  new work. Unlisted material receives no new grant; this is not an ownership
  warranty or a substitute for a contributor assignment where one is needed.
- [x] Choose and add a root `LICENSE` with an explicit scope and documented
  exceptions. Preserve upstream notices and separately licensed components.
- [x] Record the owner-accepted repository decision for recovered-source
  adaptations, patch context and generated declarations/data in the
  [assessment](PUBLICATION_PROVENANCE_ASSESSMENT.md). Rights uncertainty remains
  explicit. New executable distributions need their own concrete inventory and
  applicable source delivery; this pass does not authorize one.
- [x] Complete the repository-facing provenance map, including dependency pins,
  adapted versus reference-only material, and original changes within patches.
  Identify the generated pipeline seed and preparation header, their inputs,
  rights treatment, and any limits on public regeneration.
- [x] Retain identified full license texts and copyright notices with
  the material being published, including the separate Dolphin reference tools.

Making a repository public does not itself provide an open-source license;
see [GitHub's explanation of unlicensed code](https://choosealicense.com/no-permission/).

## 2. Audio provenance and historical material

The replacement implementations are merged, so the old issue wording about a
pending implementation choice needs updating. The [replacement record](AUDIO_REPLACEMENT_EVIDENCE.md)
documents unchanged coefficient output, 20 compatibility values originating in
Dolphin's replacement table, and the limits of the implementation's independence.
Functional compatibility does not resolve the retained-data licensing question.

- [x] Prepare the [coefficient review packet](AUDIO_COEFFICIENT_REVIEW.md), binding
  the retained data, source consumers and upstream contributions. The owner
  declined outreach; its active work log now tracks existing-terms review and
  a replacement fallback. Preparation does not close the rights question.
- [x] Complete the packet's [six-category disposition table](AUDIO_COEFFICIENT_REVIEW.md#disposition-table),
  distinguishing standard math, current implementation, retained parameter/data
  choices, generated output and historical GPL adaptations. Record the upstream
  historical README representation without treating it as a new license grant.
- [x] Map the packet's [distribution surfaces and source-delivery gaps](AUDIO_COEFFICIENT_REVIEW.md#distribution-obligations)
  across repository history, silent/audio player packages, generated output,
  the separate Dolphin observer and the local comparator. This source review
  does not certify a hosted artifact or close the combined-program decision.
- [x] Decide and document the license/provenance treatment of the replacement
  implementations and retained numerical values; preserve the factual history
  in [the source provenance record](../src/gameplay_audio_provenance.md).
- [x] Include historical GPL-derived player sources and the separately licensed
  [Dolphin observer](../reference-capture/dolphin/LICENSES.md) in the publication
  inventory. Their exclusion from a website package does not exclude them from
  a public repository and its history.
- [x] Inventory repository binary surfaces: the [GitHub audit](GITHUB_PUBLICATION_AUDIT.md)
  found no releases or nonempty compiled-output artifacts among available
  artifact downloads. A separate inventory found 75 persistent compiler caches;
  their payloads were not covered by that scan and must be removed during
  cutover. Reference binaries remain local; new binary or hosted-player
  distributions require their own notices/source-delivery review and are not
  authorized by this repository pass.
- [x] Keep retail DSP/ROM and other owned-game inputs local. Document their
  loading/provenance requirements and distinguish them from the generated
  approximation described by [the filter design](AUDIO_FILTER_DESIGN.md).

## 3. Repository-wide asset and secret safeguards

The existing [packager](../scripts/build_public.py) and
[artifact auditor](../scripts/audit_public.py) protect selected output graphs.
They do not scan all tracked repository content or all historical branches.

- [x] Expand [.gitignore](../.gitignore) for relevant extracted-game formats,
  including `.usd`, `.ssm`, `.hps`, `.mth`, `.thp`, `.sem`, `.dsp`, and `.dtm`,
  and known sensitive input paths. Avoid blanket exclusions that prevent
  legitimate documented fixtures or images.
- [x] Add a CI check of tracked file names and content for prohibited game
  assets, sensitive inputs, and secrets. It must reject force-added files;
  ignore rules alone are insufficient.
- [x] Document explicit exceptions for authored synthetic fixtures, properly
  licensed material, and generated renderer metadata such as the pipeline seed.
  Do not treat a renamed extension as proof that a file is safe to publish.
- [x] Exercise the guard with synthetic rejection and permitted-fixture cases,
  then make it part of required verification. Keep real credentials and game
  payloads out of test fixtures.
- [x] Define a repeatable final history/ref scan in addition to the ongoing
  tracked-tree guard. Preserve scan scope and findings without exposing secrets
  in reports or CI logs.

The [guard and exception guide](REPOSITORY_CONTENT_CHECK.md) documents the
implemented snapshot boundary, exact hash policy, focused real-Git tests and
limits. [Verify](../.github/workflows/verify.yml) includes the guard in the
`browser-build` aggregate; requiring that aggregate in GitHub is still open in
section 5. A passing content exception does not establish licensing clearance.
The earlier missing-dependency failures are retained under ignored
`work/public-readiness/safeguards-first-pass/`. After bootstrapping the pinned
dependencies, gameplay and default builds passed, and the full suite completed
with 1,155 tests and 74 skips. The final focused audit suite passed all 28 tests,
including the last two oversized-object regressions. The
[validation receipt](evidence/publication-validation-v1.json) binds code and log
hashes; raw logs remain under `work/public-readiness/final-validation/`.

## 4. Contributor and security-reporting documents

The initial reviewed tree had no contributor or security-reporting documents.
[CONTRIBUTING.md](../CONTRIBUTING.md) and the
[PR template](../.github/pull_request_template.md) now cover the contribution
workflow. [SECURITY.md](../SECURITY.md) selects GitHub private vulnerability
reporting and accurately states its current availability limit. Activation and
verification remain public-cutover steps.

- [x] Add `CONTRIBUTING.md` with supported prerequisites and setup/build/test
  commands, linking to [build and play](BUILD_AND_PLAY.md) and
  [the developer entry](DEVELOPMENT.md). Recommend a normal local checkout
  outside cloud-synced folders.
- [x] Document generated-source and patch workflows, coordination on shared
  runtime/generated files, source-grounded behavior changes, meaningful
  validation, and the prohibition on uploading game assets or secrets.
- [x] Add a concise PR template requesting the behavior change, relevant
  source/provenance basis, and validation results and limitations.
- [x] Prepare `SECURITY.md` for native/Wasm asset-parser and other vulnerabilities,
  selecting GitHub's private reporting form without inventing an inbox.
- [ ] Verify the reporting mechanism is enabled and usable; the prepared policy
  is not evidence that the private reporting form is available.
- [ ] If selecting GitHub private vulnerability reporting, prepare the policy
  and enable/verify the feature during public cutover. See
  [GitHub's reporting setup](https://docs.github.com/en/code-security/how-tos/report-and-fix-vulnerabilities/configure-vulnerability-reporting/configure-for-a-repository).

## 5. GitHub controls for public contributions

At review, GitHub reported `main` as unprotected. Protection/ruleset queries
returned the private-plan restriction; fork-approval settings could not be
queried for a private repository. Read-only default workflow permissions and
SHA-pinned actions were already configured.

- [x] Prepare [concrete main/fork/workflow policies](PUBLICATION_CUTOVER.md).
- [ ] Enable `main` protection: require the aggregate
  `browser-build` check, restrict force pushes and deletion, and choose an
  appropriate reviewed-change policy.
- [ ] Verify protections through GitHub after configuring them. Use an eligible
  plan beforehand or make activation and verification explicit cutover steps.
- [ ] Choose and verify approval requirements for external fork workflows once
  the public-repository settings become available.
- [ ] Verify the [reviewed cached workflow](COMPILER_CACHE_REVIEW.md), then
  quiesce Actions and remove old private compiler caches before visibility
  changes. Preserve cache metadata and retained logs/reports; verify the cache
  inventory is empty before re-enabling public CI. Future compiler caching
  remains enabled in a fresh namespace.
- [x] Preserve read-only workflow defaults, SHA-pinned actions, and isolation
  of untrusted PR jobs from secrets, privileged execution, deployment, and paid
  external services.
- [x] Verify the guard is included in the `browser-build` dependency gate and
  that the gate rejects failed/skipped dependencies. Making the aggregate
  required through GitHub still needs the activation steps above.

## 6. Final publication checkpoint and cutover

The September 20 internal review covers 285 commits across 80 selected Git
roots, all 498 retained Actions run-log archives, all 626 downloadable artifacts,
and the API-visible issue/PR discussion surface. The linked receipts record no
credential-pattern findings, the 40 reviewed historical content findings, and
182 expired artifacts whose bytes were unavailable. Persistent compiler caches
have a separate metadata inventory and required cutover disposition above;
their payloads are outside the zero-findings claim. This is a bounded review,
not comprehensive legal/security clearance. Recheck new material at the final
publication checkpoint, including CI generated after this inventory.

- [ ] Freeze the intended publication commit and list all branches, tags,
  reachable history, and other repository surfaces that will become public.
- [x] Audit the recorded source/history and GitHub checkpoint for game inputs,
  secrets, personal paths and provenance gaps; scope, unavailable expired
  artifacts and findings are in the linked receipts. Recheck changed material
  after the final publication commit is frozen.
- [x] Preserve existing commit attribution rather than rewrite active history.
  Use the verified account's noreply identity for this pass's new commit without
  changing another worktree's configuration. Existing addresses remain in
  history as documented in the cutover decision.
- [ ] Validate the final code checkpoint using its applicable tests/builds and
  retain CI evidence. Keep README and evidence links accurate for that commit;
  preserve experimental status and the separate accuracy/performance gates.
- [ ] Reconcile issue #2 with this checklist, link completed work and decisions,
  and record deliberate deferrals. Keep [STATUS.md](../STATUS.md) as the current
  gameplay evidence index rather than duplicating its measurements here.
- [ ] Record the owner's separate visibility decision. During cutover, activate
  and verify the protections, fork approvals, and reporting features that were
  unavailable while private.

GitHub makes Actions history and logs public with the repository; review
[the visibility-change consequences](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/managing-repository-settings/setting-repository-visibility).
Do not delete retained failures merely to simplify publication.

## Work already completed or materially improved

These checked items describe the reviewed checkpoint and do not close the
remaining decisions above.

- [x] Expanded [third-party provenance](../THIRD_PARTY.md) and retained full
  notices in [the runtime license inventory](licenses/PLAYABLE_RUNTIME_NOTICES.md).
- [x] Documented dependency pins and downstream patch roles in
  [dependency boundaries](DEPENDENCIES.md); removed the nonexistent
  `patches/README.md` reference.
- [x] Added generator/source digests and regeneration checks for
  [common declarations](../src/common_schema.h) and
  [the fighter registry](../src/fighter_registry.inc).
- [x] Replaced the player audio implementations and retained
  [compatibility evidence and provenance limitations](AUDIO_REPLACEMENT_EVIDENCE.md).
- [x] Added separate public package identities and artifact auditing; see
  [production audio](AUDIO_PRODUCTION.md) and [public deployment](PUBLIC_DEPLOYMENT.md).
- [x] Preserved read-only, SHA-pinned CI; the reviewed main commit passed
  [Verify run 35541381897](https://github.com/ericvanlare/melee-web/actions/runs/35541381897).

Issue templates, a contributor backlog, local agent-preference cleanup, and
broader formatting are optional follow-ups. Track remaining gameplay, original
comparison, audio fidelity, and performance work separately; publishing an
accurately described experimental project does not require completing them all.
