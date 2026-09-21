# Repository publication cutover

The owner chose an internal provenance/risk review and no outside outreach on
September 20, 2026. [The assessment](PUBLICATION_PROVENANCE_ASSESSMENT.md) and
[license scope](../LICENSE_SCOPE.md) implement that direction. This accepts an
unresolved rights risk; it does not establish third-party permission. It does
not authorize a new hosted player or native-binary release.

Repository visibility is still private. The following sequence makes the
remaining operational steps concrete; it is not a record that they happened.
GitHub returned a plan-related 403 for private main-branch protection and a 404
for private vulnerability reporting. Their activation and verification must be
part of the public transition, or follow a separately chosen plan upgrade.

Repository-level full-SHA pinning for Actions was enabled during this pass and
read back as `sha_pinning_required: true`. Actions remained enabled with the
existing allowed-actions selection. Workflow-token permissions were already
read-only with PR-review approval disabled and were verified unchanged.

## Before changing visibility

1. Integrate the readiness branch through a reviewed pull request with green
   `browser-build` at the exact head. Coordinate with active gameplay work;
   do not reset another worktree or rewrite existing branches.
2. Freeze the publication commit and remote ref inventory. Re-run the
   [history audit](REPOSITORY_HISTORY_AUDIT.md) and
   [GitHub-surface review](GITHUB_PUBLICATION_AUDIT.md) for any new content.
   Complete any specific artifact/log findings before declaring those surfaces
   ready. Retain failures; do not delete them as an audit shortcut.
3. Preserve source notices and the documented audio/data boundaries. Native
   reference binaries are local development tools unless a separate manifest,
   dependency-source inventory and applicable source/relink delivery are ready.
4. Retain existing commit attribution, including its recorded email addresses;
   this pass does not rewrite history. Future publication-branch commits can
   use the verified account's GitHub noreply address. Other worktrees keep their
   own configuration and history.
5. Record the owner's visibility decision against the frozen commit. Changing
   visibility exposes GitHub history/logs as well as the current files; see
   [GitHub's visibility documentation](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/managing-repository-settings/setting-repository-visibility).

## Quiesce Actions and remove private compiler caches

Persistent Actions caches are distinct from downloadable run artifacts. The
cache metadata inventory found 75 entries totaling 8,033,127,560 bytes; their
payloads were not covered by the archive audit. GitHub permits fork PRs to
restore base-branch caches. The chosen public workflow keeps compiler objects
on the runner and uploads only the existing text/JSON CI reports. Verify it
with `publication_mode: true` while private before proceeding.

At the authorized cutover, disable new Actions execution, wait for all active
and queued runs to finish or be deliberately stopped, and retain a fresh cache
inventory. Disabling Actions alone is not evidence that an already running
job has stopped writing caches. Then remove the disposable compiler caches
and verify the API returns zero entries. These commands are prepared cutover
actions; no cache deletion has been performed as part of this review.

```sh
gh api --method PUT repos/ericvanlare/melee-web/actions/permissions -F enabled=false
gh api --paginate 'repos/ericvanlare/melee-web/actions/runs?per_page=100' \
  --jq '.workflow_runs[] | select(.status != "completed") | {id,status,head_sha}'
mkdir -p work/public-readiness
gh api --paginate --slurp repos/ericvanlare/melee-web/actions/caches \
  > work/public-readiness/cache-inventory-before-cutover.json
gh cache delete --repo ericvanlare/melee-web --all --succeed-on-no-caches
gh api repos/ericvanlare/melee-web/actions/caches --jq .total_count
```

Keep logs, reports, failures and the cache metadata receipt. Re-enable Actions
using the policy below only after integrating and verifying the public mode.
The run query must return no non-completed runs before deleting caches; the
cache query must then return zero before visibility changes. Use a fresh
inventory filename if repeating the checkpoint.
Rebase pending work onto that workflow before running it publicly; older
workflow versions must not recreate unreviewed compiler cache uploads.

## Apply controls during the public transition

The prepared policies require pull requests, current green aggregate CI,
resolved review conversations, and no force pushes/deletion, including for
administrators. Zero mandatory independent approvals avoids making a sole
maintainer unable to merge their own change; it does not replace human review
of outside contributions. Raise the count when a second maintainer can review.
Every outside contributor requires approval before their fork workflow runs.
The required check is bound to the GitHub Actions application (ID `15368`,
verified from the current main check run), so an unrelated status producer
cannot satisfy the configured requirement by using the same name.

These commands alter repository settings but do not change visibility. Run
them only during the authorized cutover or after the private plan supports the
features. Stop on any failed call; do not label an unavailable feature enabled.

```sh
gh api --method PUT repos/ericvanlare/melee-web/branches/main/protection \
  --input .github/publication/branch-protection.json
gh api --method PUT repos/ericvanlare/melee-web/actions/permissions/workflow \
  --input .github/publication/workflow-permissions.json
gh api --method PUT repos/ericvanlare/melee-web/actions/permissions \
  --input .github/publication/actions-policy.json
gh api --method PUT repos/ericvanlare/melee-web/actions/permissions/fork-pr-contributor-approval \
  --input .github/publication/fork-approval.json
gh api --method PUT repos/ericvanlare/melee-web/private-vulnerability-reporting
```

Read the resulting state back and retain the responses in ignored local
evidence. A successful write alone is insufficient:

```sh
gh api repos/ericvanlare/melee-web/branches/main/protection
gh api repos/ericvanlare/melee-web/actions/permissions/workflow
gh api repos/ericvanlare/melee-web/actions/permissions
gh api repos/ericvanlare/melee-web/actions/permissions/fork-pr-contributor-approval
gh api repos/ericvanlare/melee-web/private-vulnerability-reporting
```

Verify the protection values against the JSON files, including
`required_status_checks.strict`, `browser-build` and its application binding,
administrator enforcement,
and force-push/deletion restrictions. Check `approval_policy` and `enabled` on
the relevant responses. The required aggregate includes `repository-content`
and rejects a skipped or failed dependency. Preserve SHA-pinned actions,
read-only tokens and the existing separation from deployment credentials.

Open the private reporting form from [SECURITY.md](../SECURITY.md) and verify
that a reporter can start a private advisory. Do not submit a test report or
send a notification. Confirm the maintainer's security-notification setup, then
replace SECURITY.md's private-repository caveat with the verified reporting
state. GitHub documents [reporting availability](https://docs.github.com/en/code-security/how-tos/report-and-fix-vulnerabilities/configure-vulnerability-reporting/configure-for-a-repository),
[branch protection](https://docs.github.com/en/rest/branches/branch-protection#update-branch-protection),
and [Actions policies](https://docs.github.com/en/rest/actions/permissions).

Record the applied controls, publication SHA, visibility and latest CI run in
the checklist. Reconcile issue #2 with the implementation and explicit
deferrals. Do not close it as fully complete while activation, verification or
newly exposed material remains unchecked.
