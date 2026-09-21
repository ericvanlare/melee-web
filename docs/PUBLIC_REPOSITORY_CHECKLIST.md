# Public repository readiness checklist

This is the working checklist for [issue #2](https://github.com/ericvanlare/melee-web/issues/2).
It prepares the repository for public visibility and contributions. Changing
visibility remains a separate owner action; this checklist does not authorize
deployment, history rewriting, or deletion of evidence.

The starting review was conducted on September 20, 2026 against GitHub `main`
at [`2c2b687f5bb7dbfcb3c75dc0394c04bb97f2114d`](https://github.com/ericvanlare/melee-web/commit/2c2b687f5bb7dbfcb3c75dc0394c04bb97f2114d).
Observations below describe that checkpoint. Recheck the eventual publication
commit and live repository settings before closing the checklist.

Public package audits cover selected deployment files. Repository publication
also exposes source, patches, reference tools, generated material, reachable
history, and GitHub discussion and Actions material. Keep those scopes explicit.
Full gameplay accuracy and roster completion remain separate milestones under
[the accuracy contract](ACCURACY_CONTRACT.md). Use [STATUS.md](../STATUS.md)
for current gameplay evidence and limitations.

## Start here

Start with the source ownership and license inventory in section 1. Produce a
reviewable map of project-authored code, upstream adaptations, recovered
Melee/SDK material, generated data, and separately licensed reference tools.
The [first-pass inventory and license proposal](SOURCE_LICENSE_INVENTORY.md)
now records that map, its evidence, and four specific decisions to resolve.
Use it to make the project-license decision and identify the precise remaining
audio-data questions. This avoids choosing a root license whose scope is
unclear. The independent engineering work in sections 3 and 4 can proceed while
those decisions are reviewed.

Suggested sequence: ownership and license inventory; audio disposition;
repository safeguards and contributor documents; preparation of GitHub controls;
final checkpoint audit and owner-controlled visibility transition. Record an
evidence link or decision when checking an item off. Record any deliberate
deferral and its rationale rather than silently treating it as complete.

## 1. Project license and source publication decisions

Owner decisions, supported by a technical inventory. No root project license
exists at the reviewed checkpoint. Start with [third-party provenance](../THIRD_PARTY.md),
[dependency boundaries](DEPENDENCIES.md), and [the release review](PUBLIC_RELEASE_REVIEW.md).

- [x] Complete a [first-pass technical inventory](SOURCE_LICENSE_INVENTORY.md)
  distinguishing project-license candidates, identified adaptations, recovered
  Melee/HSD and original SDK material, patches, generated data and reference tools.
- [ ] Confirm authority to license the proposed project-authored files/portions;
  turn the candidate map into an affirmative license scope. The technical
  inventory alone does not establish ownership or contributor assignments.
- [ ] Choose and add a root `LICENSE` with an explicit scope and documented
  exceptions. Preserve upstream notices and separately licensed components.
- [ ] Record the decision for publishing recovered-source adaptations, patch
  context, generated declarations/data, and any distributed executable. The
  accepted website-alpha risk posture does not itself settle repository
  publication or establish upstream permission. Obtain qualified review where
  needed for the intended model.
- [ ] Complete the repository-facing provenance map, including dependency pins,
  adapted versus reference-only material, and original changes within patches.
  Identify the generated pipeline seed and preparation header, their inputs,
  rights treatment, and any limits on public regeneration.
- [ ] Verify that applicable full license texts and copyright notices accompany
  the material being published, including the separate Dolphin reference tools.

Making a repository public does not itself provide an open-source license;
see [GitHub's explanation of unlicensed code](https://choosealicense.com/no-permission/).

## 2. Audio provenance and historical material

The replacement implementations are merged, so the old issue wording about a
pending implementation choice needs updating. The [replacement record](AUDIO_REPLACEMENT_EVIDENCE.md)
documents unchanged coefficient output, 20 compatibility values originating in
Dolphin's replacement table, and the limits of the implementation's independence.
Functional compatibility does not resolve the retained-data licensing question.

- [x] Prepare the [coefficient review packet and unsent outreach draft](AUDIO_COEFFICIENT_REVIEW.md),
  binding the retained data, source consumers and identified upstream contributors.
  Its work log tracks contact, review and disposition; preparation is not permission.
- [ ] Decide and document the license/provenance treatment of the replacement
  implementations and retained numerical values; preserve the factual history
  in [the source provenance record](../src/gameplay_audio_provenance.md).
- [ ] Include historical GPL-derived player sources and the separately licensed
  [Dolphin observer](../reference-capture/dolphin/LICENSES.md) in the publication
  inventory. Their exclusion from a website package does not exclude them from
  a public repository and its history.
- [ ] Inventory any binaries being distributed that contain GPL-covered code
  and verify the applicable notices and corresponding-source arrangements for
  those artifacts. Distinguish source publication from binary-distribution
  obligations; do not infer that every audio implementation has the same license.
- [ ] Keep retail DSP/ROM and other owned-game inputs local. Document their
  loading/provenance requirements and distinguish them from the generated
  approximation described by [the filter design](AUDIO_FILTER_DESIGN.md).

## 3. Repository-wide asset and secret safeguards

The existing [packager](../scripts/build_public.py) and
[artifact auditor](../scripts/audit_public.py) protect selected output graphs.
They do not scan all tracked repository content or all historical branches.

- [ ] Expand [.gitignore](../.gitignore) for relevant extracted-game formats,
  including `.usd`, `.ssm`, `.hps`, `.mth`, `.thp`, `.sem`, `.dsp`, and `.dtm`,
  and known sensitive input paths. Avoid blanket exclusions that prevent
  legitimate documented fixtures or images.
- [ ] Add a CI check of tracked file names and content for prohibited game
  assets, sensitive inputs, and secrets. It must reject force-added files;
  ignore rules alone are insufficient.
- [ ] Document explicit exceptions for authored synthetic fixtures, properly
  licensed material, and generated renderer metadata such as the pipeline seed.
  Do not treat a renamed extension as proof that a file is safe to publish.
- [ ] Exercise the guard with synthetic rejection and permitted-fixture cases,
  then make it part of required verification. Keep real credentials and game
  payloads out of test fixtures.
- [ ] Define a repeatable final history/ref scan in addition to the ongoing
  tracked-tree guard. Preserve scan scope and findings without exposing secrets
  in reports or CI logs.

## 4. Contributor and security-reporting documents

The reviewed tree has no `CONTRIBUTING.md`, PR template, or `SECURITY.md`.

- [ ] Add `CONTRIBUTING.md` with supported prerequisites and setup/build/test
  commands, linking to [build and play](BUILD_AND_PLAY.md) and
  [the developer entry](DEVELOPMENT.md). Recommend a normal local checkout
  outside cloud-synced folders.
- [ ] Document generated-source and patch workflows, coordination on shared
  runtime/generated files, source-grounded behavior changes, meaningful
  validation, and the prohibition on uploading game assets or secrets.
- [ ] Add a concise PR template requesting the behavior change, relevant
  source/provenance basis, and validation results and limitations.
- [ ] Add `SECURITY.md` with a usable private reporting route for issues such
  as native/Wasm asset-parser vulnerabilities. Verify the selected inbox or
  reporting mechanism; an address or link alone is insufficient.
- [ ] If selecting GitHub private vulnerability reporting, prepare the policy
  and enable/verify the feature during public cutover. See
  [GitHub's reporting setup](https://docs.github.com/en/code-security/how-tos/report-and-fix-vulnerabilities/configure-vulnerability-reporting/configure-for-a-repository).

## 5. GitHub controls for public contributions

At review, GitHub reported `main` as unprotected. Protection/ruleset queries
returned the private-plan restriction; fork-approval settings could not be
queried for a private repository. Read-only default workflow permissions and
SHA-pinned actions were already configured.

- [ ] Prepare and enable `main` protection: require the aggregate
  `browser-build` check, restrict force pushes and deletion, and choose an
  appropriate reviewed-change policy.
- [ ] Verify protections through GitHub after configuring them. Use an eligible
  plan beforehand or make activation and verification explicit cutover steps.
- [ ] Choose and verify approval requirements for external fork workflows once
  the public-repository settings become available.
- [ ] Preserve read-only workflow defaults, SHA-pinned actions, and isolation
  of untrusted PR jobs from secrets, privileged execution, deployment, and paid
  external services.
- [ ] Recheck that the new repository-content guard is included in required
  verification and cannot be bypassed by a missing or skipped job.

## 6. Final publication checkpoint and cutover

The September 20 review was targeted, not a comprehensive legal/security
clearance. Its history scan found no obvious credentials or disc/extracted-asset
files in the inspected refs. Current-main CI passed; its log and three sampled
artifacts showed no credential-pattern hits. A complete final artifact/log and
discussion review remains open. Unlike the original issue audit, Actions now
retains many artifacts, including CI reports and compiler-seed archives.

- [ ] Freeze the intended publication commit and list all branches, tags,
  reachable history, and other repository surfaces that will become public.
- [ ] Recheck source/history for game inputs, secrets, personal paths, and
  provenance gaps. Review PRs, issues, comments, attachments, Actions logs and
  artifacts, releases, and any other exposed publication material.
- [ ] Decide whether existing personal commit-email addresses are acceptable.
  Configure a noreply identity for future commits if desired. Coordinate any
  separately approved history rewrite with active worktrees and contributors.
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
