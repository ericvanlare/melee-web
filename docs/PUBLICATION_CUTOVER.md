# Repository publication cutover

The owner chose an internal provenance/risk review and no outside outreach on
September 20, 2026. [The assessment](PUBLICATION_PROVENANCE_ASSESSMENT.md) and
[license scope](../LICENSE_SCOPE.md) implement that direction. This accepts an
unresolved rights risk; it does not establish third-party permission. It does
not authorize a new hosted player or native-binary release.

## Completed public transition: September 25, 2026 UTC

The owner authorized public visibility and required that only `ericvanlare`
(including agents using that account) can change `main`. The repository was
made public at commit `7d3a8a7e1b0d9bd0e2ed840982010abb65ef9213`, after
[Verify run 36080615358](https://github.com/ericvanlare/melee-web/actions/runs/36080615358)
and the final source/history/GitHub delta checks passed within their recorded
scope. The [cutover receipt](evidence/publication-cutover-v1.json) binds the
applied settings, readbacks, cache cleanup and verification limits.

Actions was disabled, no active runs remained, and all 58 private-era compiler
caches were removed. The API returned zero caches before visibility changed.
Logs, artifacts and audit evidence were retained. Actions was then restored
with full-SHA action pinning, read-only tokens and approval required for every
external contributor's fork workflow.

The active [owner-only main ruleset](https://github.com/ericvanlare/melee-web/rules/23976041)
restricts creation, updates, deletion and non-fast-forward changes. Its only
bypass actor is GitHub user ID `1350618` (`ericvanlare`), and that bypass works
only through pull requests. No role, team, app or deploy key receives a bypass.
The separate classic protection still requires a current successful
`browser-build` from GitHub Actions app `15368`, resolved conversations and PRs,
including for the owner. Force pushes and deletion remain disabled. A no-op
direct ref-update attempt using the owner's credentials was rejected with
"Changes must be made through a pull request"; main remained unchanged.
An owner-authenticated merge attempt on PR #80 before required CI passed was
also rejected: "Required status check `browser-build` is expected." This
confirms that the owner-specific ruleset bypass does not bypass required CI.

The collaborator inventory contained only the owner, with no pending invites,
deploy keys or webhooks. Secret scanning, secret push protection, dependency
alerts and private vulnerability reporting are enabled. The owner is subscribed
to repository activity and is not ignoring notifications. The public reporting
button and its GitHub sign-in destination were verified with headless installed
Chrome. No report was submitted; the authenticated form, email preferences and
email delivery were not exercised. See [SECURITY.md](../SECURITY.md).

The GitHub API rejected the old combination of an empty `contexts` list and
`checks` in the prepared protection payload. The applied and retained policy
uses `checks` alone. GitHub also omits the explicitly false
`update_allows_fetch_and_merge` parameter in ruleset readback; the update rule
is active, the repository is not a fork, and no upstream-sync exception is enabled.

The procedures below describe how to repeat these checks. Their earlier
private-plan observations are historical, superseded by the applied readbacks.

The [post-handoff checkpoint](PUBLICATION_CHECKPOINT.md) and its exact
inventories preserve the reviewed baseline after runtime work stopped. The
cutover delta check covered subsequent changes before publication; future
releases need checks against their own selected commits and CI.

Before publication, GitHub returned a plan-related 403 for private main-branch
protection and a 404 for private vulnerability reporting. Both features were
activated and verified during the public transition recorded above.

Repository-level full-SHA pinning for Actions was read back as
`sha_pinning_required: true` after Actions was restored. Workflow-token
permissions remain read-only with PR-review approval disabled.

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
original cache metadata inventory found 75 entries totaling 8,033,127,560
bytes; their payloads were not covered by that archive audit. GitHub permits
fork PRs to restore base-branch caches. The [targeted cache review](COMPILER_CACHE_REVIEW.md)
replaces the initial blanket public-cache restriction: retain ordinary caching
in a fresh namespace, with the existing source/provenance boundaries. Historical
private caches still receive one-time cleanup rather than being inherited by
public CI. Verify the updated cached workflow before proceeding.

At the authorized cutover, disable new Actions execution, wait for all active
and queued runs to finish or be deliberately stopped, and retain a fresh cache
inventory. Disabling Actions alone is not evidence that an already running
job has stopped writing caches. Then remove the disposable compiler caches
and verify the API returns zero entries. The one-time cleanup above completed
these steps; the commands below preserve the procedure for a future transition.

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
using the policy below only after integrating and verifying the reviewed workflow.
The run query must return no non-completed runs before deleting caches; the
cache query must then return zero before visibility changes. Use a fresh
inventory filename if repeating the checkpoint.
Rebase pending work onto that workflow before running it publicly; older
workflow versions must not republish unreviewed local inputs or private-era caches.

## Apply controls during the public transition

The owner-specific access policy is separate from the required-CI policy, so
permission to merge does not grant permission to bypass CI. For this repository,
update the existing ruleset rather than creating duplicates:

```sh
gh api --method PUT repos/ericvanlare/melee-web/rulesets/23976041 \
  --input .github/publication/main-owner-rule.json
gh api repos/ericvanlare/melee-web/rulesets/23976041
```

Verify that enforcement is active, `refs/heads/main` is the only target, and
`User` ID `1350618` with `pull_request` mode is the only bypass actor. Do not
replace this with an administrator-role or integration bypass. The owner can
administer these settings; credentials acting as the owner retain that authority.

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

For future reporting checks, follow the entry point from [SECURITY.md](../SECURITY.md)
and record whether verification reaches the sign-in page or an authenticated
private advisory form. Do not submit a test report or send a notification.
Read back the maintainer's repository subscription and distinguish it from
account-wide email preferences or delivery. SECURITY.md now describes the
enabled reporting route verified at this cutover. GitHub documents
[reporting availability](https://docs.github.com/en/code-security/how-tos/report-and-fix-vulnerabilities/configure-vulnerability-reporting/configure-for-a-repository),
[branch protection](https://docs.github.com/en/rest/branches/branch-protection#update-branch-protection),
and [Actions policies](https://docs.github.com/en/rest/actions/permissions).

Record the applied controls, publication SHA, visibility and latest CI run in
the checklist. Reconcile issue #2 with the implementation and explicit
deferrals. Do not close it as fully complete while activation, verification or
newly exposed material remains unchecked.
