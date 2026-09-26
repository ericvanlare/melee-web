# License scope

This is a mixed-provenance source repository. The root [MIT license](LICENSE)
applies to the following project-authored files at this revision and their
project-authored subsequent changes. It supplies no rights to their inputs,
dependencies, generated third-party outputs, quoted material or programs they
build or serve. Preserve the license and this scope when redistributing them.

- `scripts/browser_tools.mjs`
- `scripts/serve.py`
- `scripts/ci_verify.py`
- `scripts/ci_aggregate.py`
- `scripts/ci_report.py`
- `scripts/check_repository_content.py`
- `scripts/audit_repository_history.py`
- `scripts/audit_compiler_cache.py`
- `scripts/summarize_browser_failure.py`
- `tests/test_repository_content.py`
- `tests/test_repository_history.py`
- `tests/test_compiler_cache_audit.py`
- `tests/test_browser_failure_summary.py`
- `.github/repository-content-policy.json`
- `.github/pull_request_template.md`
- `.github/publication/actions-policy.json`
- `.github/publication/branch-protection.json`
- `.github/publication/fork-approval.json`
- `.github/publication/workflow-permissions.json`
- `CONTRIBUTING.md`
- `SECURITY.md`
- `LICENSE_SCOPE.md`
- `docs/REPOSITORY_CONTENT_CHECK.md`
- `docs/PUBLIC_REPOSITORY_CHECKLIST.md`
- `docs/PUBLICATION_REVIEW_BRIEF.md`
- `docs/PUBLICATION_PROVENANCE_ASSESSMENT.md`
- `docs/PUBLICATION_CUTOVER.md`
- `docs/REPOSITORY_HISTORY_AUDIT.md`
- `docs/GITHUB_PUBLICATION_AUDIT.md`
- `docs/COMPILER_CACHE_REVIEW.md`
- `docs/BROWSER_FAILURE_TRIAGE.md`
- `docs/DEVELOPMENT.md`
- `tools/browser_failure_summary.py`

The first five implementations were read and recorded in the
[source inventory](docs/SOURCE_LICENSE_INVENTORY.md); their recorded authorship
uses the repository owner's identity. The remaining entries are the publication
work authored in this branch. The owner authorized proceeding with this scoped
approach after the internal review. Git authorship is supporting provenance,
not a warranty that third parties cannot assert a claim.

## Exclusions and existing terms

An unlisted file receives no new permission from the root license. This is an
explicit initial scope, not an assertion that every unlisted file is third-party
work. Review and add further separable project files individually.

- Recovered Melee, HSD, original SDK material, translations, patch context,
  game-derived declarations/tables and compiled game outputs are excluded. This
  project cannot grant Nintendo's or another third party's rights.
- Current and historical audio implementations, coefficient parameters/data and
  generated audio outputs are excluded from this MIT grant. The
  [audio review](docs/AUDIO_COEFFICIENT_REVIEW.md) records their distinct treatment.
  Existing historical GPL notices are preserved.
- The separate Dolphin observer, its patches and covered historical sources
  retain their declared GPL-2.0-or-later terms. See its
  [license inventory](reference-capture/dolphin/LICENSES.md).
- Aurora, B0XX adaptations, native probe dependencies and other identified
  third-party components retain their own terms. See
  [THIRD_PARTY.md](THIRD_PARTY.md) and the linked full notices.
- Generated renderer seeds, preparation metadata, screenshots, evidence payloads
  and all other unlisted generated/data files receive no new license here.
  A content-scan exception is not a license grant.

The project is not offering a blanket open-source license for a combined Melee
player. Source availability, permission to reuse individual files and permission
to distribute a binary are separate questions. Public contributions should
identify the authority and license for any material they introduce; adding a
file does not silently extend this allowlist.
