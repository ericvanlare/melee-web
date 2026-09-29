# License scope

This is a mixed-provenance source repository. The root [MIT license](LICENSE)
applies to the following project-authored files at this revision and their
project-authored subsequent changes. It supplies no rights to their inputs,
dependencies, generated third-party outputs, quoted material or programs they
build or serve. Preserve the license and this scope when redistributing them.

- `scripts/browser_tools.mjs`
- `scripts/dolphin_audio.py`
- `scripts/serve.py`
- `scripts/ci_verify.py`
- `scripts/ci_aggregate.py`
- `scripts/ci_report.py`
- `scripts/check_repository_content.py`
- `scripts/audit_repository_history.py`
- `scripts/audit_compiler_cache.py`
- `scripts/compare_allocation_traces.py`
- `scripts/summarize_browser_failure.py`
- `tests/test_repository_content.py`
- `tests/test_repository_history.py`
- `tests/test_compiler_cache_audit.py`
- `tests/test_allocation_trace_compare.py`
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
- `docs/ALLOCATION_TRACE_COMPARISON.md`
- `docs/DEVELOPMENT.md`
- `docs/BROWSER_FAILURE_TRIAGE.md`
- `docs/PUBLIC_REPOSITORY_CHECKLIST.md`
- `docs/PUBLICATION_REVIEW_BRIEF.md`
- `docs/PUBLICATION_PROVENANCE_ASSESSMENT.md`
- `docs/PUBLICATION_CUTOVER.md`
- `docs/REPOSITORY_HISTORY_AUDIT.md`
- `docs/GITHUB_PUBLICATION_AUDIT.md`
- `docs/COMPILER_CACHE_REVIEW.md`
- `tools/allocation_trace_compare.py`
- `tools/browser_failure_summary.py`
- `tools/slippi_format.py`
- `reference-capture/slippi/LICENSES.md`
- `reference-capture/slippi/LOCAL_TESTBED.md`
- `reference-capture/slippi/client.lock.json`
- `reference-capture/slippi/local_matchmaker/CMakeLists.txt`
- `reference-capture/slippi/local_matchmaker/integration_test.cpp`
- `reference-capture/slippi/local_matchmaker/pairing.cpp`
- `reference-capture/slippi/local_matchmaker/pairing.hpp`
- `reference-capture/slippi/local_matchmaker/protocol.cpp`
- `reference-capture/slippi/local_matchmaker/protocol.hpp`
- `reference-capture/slippi/local_matchmaker/server.cpp`
- `reference-capture/slippi/local_matchmaker/tests.cpp`
- `reference-capture/slippi/process.py`
- `reference-capture/slippi/run_local.py`
- `reference-capture/slippi/runtime.py`
- `reference-capture/slippi/test_process.py`
- `reference-capture/slippi/test_run_local.py`
- `reference-capture/slippi/test_runtime.py`
- `web/gamecube-save.mjs`
- `web/save-profile-settings.mjs`
- `web/save-profile-store.mjs`
- `tests/gamecube_save_test.mjs`
- `tests/test_gamecube_save.py`
- `docs/SAVE_PROFILES.md`

The first five implementations were read and recorded in the
[source inventory](docs/SOURCE_LICENSE_INVENTORY.md); their recorded authorship
uses the repository owner's identity. The publication safeguards, browser
failure triage, and allocation-comparison files are separately scoped project
work. The owner authorized proceeding with this file-by-file approach after
internal review. Git authorship is supporting provenance, not a warranty that
third parties cannot assert a claim.

The allocation comparison CLI, comparator, synthetic tests, and usage note
listed above were authored in this task from the repository's existing trace
schemas and allocator documentation. The developer-entry link is an authored
change to the existing developer guide. These files contain no retained trace
rows, recovered game or SDK implementation, or generated capture output. This
file-level provenance does not extend to the inputs read by the tool or other
files in their directories.

The browser save/profile modules and their codec tests are separately authored
project code. The GCI reader/writer was implemented from the public card format,
the source card manifest, and local format observations; it contains no copied
Dolphin implementation. The documentation describes behavior and the observed
interoperability boundary. This MIT grant does not extend to the native save
owner/host bridge or upstream-derived source files that those modules call.

The Slippi timeline parser listed above is a separately authored reader of the
public Slippi format specification. Its conformance checks compare behavior
with pinned `slippi-js`; the parser does not import or copy that LGPL
implementation. The tool-specific inventory identifies that distinction.

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
- The local Slippi matchmaking service and its bounded testbed orchestration
  are project-authored MIT code listed above. The client adaptation patches
  in `reference-capture/slippi/patches/`
  modify pinned Dolphin GPL-2.0-or-later and Slippi Rust Extensions GPL-2.0
  sources; they retain those upstream terms and are not covered by the root MIT
  grant. The full upstream license texts in `reference-capture/slippi/licenses/`
  are notices only and are not covered by the root MIT grant. See
  `reference-capture/slippi/LICENSES.md` for exact source pins and boundaries.
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
