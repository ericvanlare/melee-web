# Source ownership and license inventory: first pass

**Current disposition:** the owner accepted an internal review without outside
outreach on September 20, 2026. The [follow-up assessment](PUBLICATION_PROVENANCE_ASSESSMENT.md)
and operative [license scope](../LICENSE_SCOPE.md) supersede the proposed/open
decision statuses below. The historical technical inventory is retained as
evidence. Recovered-source and current audio/data publication risks remain
disclosed; acceptance is not a finding of third-party permission. The `fres`
primary-source notice is now verified and native probe dependency texts are
retained. Native binaries remain local pending their separate release work.

This is the first implementation step in [the publication checklist](PUBLIC_REPOSITORY_CHECKLIST.md)
for [issue #2](https://github.com/ericvanlare/melee-web/issues/2). It records
technical provenance and a proposed licensing approach, not a license grant or
a conclusion about ownership or permission to distribute the combined game.

Reviewed source: `11410f31be0930f8a69a3ff358527286a8f19c2a`, based on main
`2c2b687f5bb7dbfcb3c75dc0394c04bb97f2114d`. The
[inventory receipt](evidence/source-license-inventory-v1.json) binds the named
files to their Git blobs and records the scope and limitations of this pass.
Current gameplay evidence remains in [STATUS.md](../STATUS.md).

## Recommended approach

Use **MIT for confirmed project-authored, separable code and documentation**,
with an explicit file/portion scope and preserved third-party notices. Retain
the existing GPL treatment for the separate Dolphin observer and historical
GPL adaptations. Keep recovered Melee/SDK material and the unresolved numerical
and generated-data cases outside any assertion that the project owns or can
relicense them. Describe the repository's mixed licensing accurately.

MIT is a proposal because it provides broad reuse rights with a notice-retention
condition and is already used by Aurora and the B0XX mapping. It does not supply
rights in other parties' material. The [MIT text](https://opensource.org/license/mit)
defines those permissions and conditions. Confirm the rights holder and the
scope before adding an operative root license; Git authorship or a missing
copyright header alone cannot establish ownership.

The initial proposed scope below makes the first license decision reviewable,
alongside the four remaining decisions. A blanket license for
`src/`, `tests/`, or the combined executable is not supported by this inventory.

## Initial proposed MIT scope

This first tranche is deliberately small and consists of files whose complete
current implementations were read in this pass. No copied third-party
implementation was identified in them. It is a proposed allowlist, pending
rights-holder confirmation, rather than an assertion that all other project
files are ineligible. Their reviewed blob identities are in the receipt.

| Proposed file | Authored function and scope limit |
| --- | --- |
| `scripts/browser_tools.mjs` | Resolves local browser/Playwright configuration using Node APIs; does not include or license Playwright or Chrome |
| `scripts/serve.py` | Adds loopback hosting, isolation headers, path checks and bounded local evidence handling using Python's HTTP APIs; does not license served content or the Python runtime |
| `scripts/ci_verify.py` | Partitions this project's builds/tests and records their results; does not license the programs built or tested |
| `scripts/ci_aggregate.py` | Checks this project's partition reports, source identity and discovery coverage |
| `scripts/ci_report.py` | Reads GitHub run/job metadata and reports queue/execution durations |

After D1 is resolved, the implementation is a standard MIT license plus an
explicit scope file naming these paths and any subsequently reviewed additions.
Retain notices for separately attributed material. Extend the allowlist through
source review, rather than converting every file in a directory at once.

## What was inspected

The review enumerated the tracked tree, searched source and build files for
notices, adaptation references and direct implementation includes, and read
the named implementations, generators, patches, and provenance documents.
It inspected the pinned Melee root and Aurora license through GitHub, and the
pinned Slippi license and Dolphin coefficient generator. The three tracked
documentation PNGs were inspected visually and show the project's empty player
shell and toolbar, without game scenes.

There are no `.deps/` checkouts in this worktree. This pass did not bootstrap
dependencies, rebuild artifacts, perform a whole-code similarity analysis,
establish contributor assignments, or inspect every file's full authorship
history. The earlier publication audit's targeted secret/history scan is a
different scope. This inventory is not a post-link SBOM.

Definitions used below:

- **Project-license candidate:** project-maintained implementation or prose with
  no identified copied implementation in this pass. Ownership and any embedded
  third-party material still require confirmation before an affirmative grant.
- **Known adaptation:** origin is identified by source, notices, or generator
  inputs. Preserve that origin and its applicable terms.
- **Mixed or unresolved:** project additions coexist with recovered material,
  numerical data, or incomplete origin evidence. Do not infer a license.
- **Build/reference dependency:** fetched separately; distinguish its own source
  terms from the material actually copied, linked, or distributed by this repo.

## Repository map

Specific exceptions in the next table take precedence over these broad areas.
Calling upstream APIs or reading a documented format is not, by itself, proof
that an implementation is a derivative work; copying or translating an
implementation needs a different review.

| Tracked area | First-pass classification | Publication treatment |
| --- | --- | --- |
| Root project docs/config, `.github/`, `cmake/`, `CMakeLists.txt`, dependency lock | Project-license candidates, with upstream source selection and dependency metadata | Review original additions for the proposed MIT scope; preserve the licenses of the sources they build |
| `scripts/`, `tools/` | Project-maintained orchestration/parser candidates; some consume, extract or compile recovered/GPL/reference source | Review implementation provenance separately from generated output and linked test programs; retain source references and dependency pins |
| `web/` UI, input, disc loading and runtime ownership modules | Project-license candidates with specific B0XX/audio/seed exceptions below | Review separable browser implementations; retain mapping attribution and exclude unresolved generated/data cases from a blanket grant |
| `src/` | Mixed native integration, original-source wrappers, translated routines, source-derived declarations and identified third-party adaptations | Review by file and, where necessary, by portion; a directory-wide originality assertion is unsupported |
| `tests/` | Test harness/synthetic-fixture candidates mixed with source-bound tables and programs that compile original implementations | Preserve fixture provenance; review copied expectations and declarations; a test label does not make every embedded input original |
| `reference-capture/app/`, `reference-capture/schemas/`, standalone Python orchestration | Project-license candidates | Keep separate from the GPL observer's source scope and from native dependencies bundled with a capture application |
| `reference-capture/dolphin/` and Dolphin patches | Identified GPL observer/patch boundary plus support docs/scripts | Preserve declared GPL notices and pin; verify scope of support files instead of assuming the entire capture application has one license |
| `reference-capture/controller-probe/` | Project-maintained native probe plus separately linked SDL/libusb dependencies | Record its own source provenance and the actual static-link dependency notices before distributing its binary |
| `patches/` | Upstream context plus downstream modifications | Identify target repository, exact base and changed units; project additions do not relicense the patch's upstream context |
| `docs/`, `docs/evidence/`, `docs/images/` | Project prose/report/UI-image candidates with quotations, source-derived inventories and evidence metadata | Preserve citations; classify generated game/renderer observations separately from authored prose |
| `docs/licenses/`, `licenses/` | Verbatim third-party notices and their inventory | Preserve the supplied texts and attributions; these are exceptions to a project-authored-documentation grant |

## Concrete origin and exception map

| Files or component | Observed origin and evidence | Existing terms / first-pass disposition |
| --- | --- | --- |
| `patches/melee-gameplay.patch`; generated `build/gameplay-source/` | Patch against Melee `b43912cc78606f96c9569f5d6229bc9d7e265ea5`; [source preparation](../scripts/gameplay_sources.py) verifies the source and applies downstream changes | No Melee root license was identified. Patch context and translated recovered material remain outside a new project grant. The generated source tree remains ignored |
| Original game/HSD, MSL and `extern/dolphin` AX/AXFX units selected by `CMakeLists.txt` and `cmake/FighterRuntime.cmake` | [Build declarations](../cmake/FighterRuntime.cmake) compile Melee and original platform units; [existing inventory](../THIRD_PARTY.md) distinguishes the SDK from Dolphin Emulator | Recovered-source rights remain unresolved. A tool-specific license in the dependency does not license the recovered game/SDK as a whole |
| `src/gameplay_audio_reverb.c`, `src/gameplay_ps_math.c/.h`, `src/gameplay_trig.h`, `src/gameplay_audio_itd.c` | [Reverb](../src/gameplay_audio_reverb.c) explicitly translates original `HandleReverb`; [paired-single math](../src/gameplay_ps_math.c) follows GALE01 instructions; [audio provenance](../src/gameplay_audio_provenance.md) identifies original DSP observations | Mixed project implementation and recovered behavior/translation; review individual portions. Do not classify all current audio or math as independently authored solely because the resampler changed |
| `src/gameplay_collision.c`, selected `src/hsd_*` bridges, `tests/source_address_oracle.c`, `tests/gameplay_registry_trace.c` | Direct `#include` of original `.c` implementations; exact include sites are recorded in the receipt | Tracked wrapper text and the dependency source compiled into a program are separate inventories. The resulting translation unit is not wholly project-authored |
| `src/common_schema.h`, `src/fighter_registry.inc` | [Common-schema generator](../scripts/generate_common_schema.py) and [registry generator](../scripts/generate_fighter_registry.py) derive declarations/initializers from pinned Melee; output headers retain source digests | Generated recovered-source material, not made original by regeneration. Keep generator, source identity and upstream boundary together |
| `src/gameplay_{donkey,koopa,luigi,pikachu,purin}_schema.h` and original fighter/stage/item/effect tables | Typed fields and table bounds derive from original source/layouts; see the named headers and source-consumer tests | Source-derived declarations/data requiring explicit scope; no automatic MIT assignment |
| `patches/aurora-browser.patch`, Aurora compiled by this project | Aurora `749d6ee7a22bdfab78c8ece9047bca5d79aa72ca`; downstream browser changes recorded in [dependencies](DEPENDENCIES.md) | Upstream MIT with [full notice](licenses/aurora-mit.txt). Preserve notice for copied/adapted material and identify downstream changes |
| `src/gameplay_heap.cpp` | [Wrapper](../src/gameplay_heap.cpp) directly includes Aurora's `lib/dolphin/os/OSAlloc.cpp` | Project ownership wrapper plus Aurora source compiled together; retain Aurora provenance/terms |
| `src/boxx_input.h`, B0XX default bindings/UI mapping | Explicit adaptation from `agirardeau/b0xx-ahk` at `7c070f8e0f135c8108cfb0af9a37dc6809070b15`; [mapping scope](KEYBOARD_LAYOUTS.md) | MIT attribution already retained in [b0xx-ahk.txt](../licenses/b0xx-ahk.txt); preserve it for both native mapping and derived defaults |
| `src/gameplay_fres.h` | Header attributes its table and integer steps to Andrew Church's `calc_fres` hardware test at `https://achurch.org/cpu-tests/ppc750cl.s` | Header claims public-domain origin. No pinned upstream copy/digest or retained dedication is recorded here; direct retrieval returned HTTP 403 in this pass. Verify and retain that provenance before calling the row cleared |
| `src/gameplay_audio_resample.c/.h` | Current contract-based replacement; historical versions adapted Dolphin `a2efdf1197be8132674b90fe9cf4761df39752ed`; [replacement record](AUDIO_REPLACEMENT_EVIDENCE.md) describes the actual process | Current implementation-license decision pending; historical GPL-2.0-or-later provenance remains. The record does not claim formal clean-room isolation |
| `web/dsp-coefficients.mjs` | New numerical implementation retains the filter parameters/output and 20 compatibility overlays; [filter specification](AUDIO_FILTER_DESIGN.md) identifies the upstream data | Implementation and retained data need distinct treatment. Mathematical formulas and a rewritten loop do not resolve the status of intentionally preserved third-party numerical material |
| `web/initial_pipeline_cache.db.gz.b64` | Generated Aurora renderer descriptor database; [release review](PUBLIC_RELEASE_REVIEW.md) records its game-related provenance | Treat as generated renderer metadata with its own inventory/rights decision, rather than ordinary authored JS or a raw retail asset. Revalidate the exact seed at publication |
| `src/pipeline_preparation.generated.hpp` | [Generator](../scripts/generate_pipeline_preparation.py) joins certified requirement/coverage inputs; [preparation guide](PIPELINE_PREPARATION.md) keeps those inputs private | Generated descriptor identities bound to an older seed, not the current materialized seed. Regeneration and certification against a new seed require the appropriate inputs; private captures need not become public |
| `reference-capture/dolphin/source/Core/PowerPC/Reference{CaptureObserver,InputStream}.*` and observer/input patches | Dolphin `c77bbaa0f372c3f72281602a8b087206706542cb`; source headers declare GPL-2.0-or-later; [license record](../reference-capture/dolphin/LICENSES.md) identifies reconstruction | Separate known GPL component. Preserve notices, changes and full GPL text; source publication and distributing an observer binary have different deliverables |
| `patches/reference-dolphin-clock-probe.patch`, `patches/reference-dolphin-clock-delay-probe.patch` | Downstream diagnostics modify the GPL observer; the standard builder selects `reference-capture/dolphin/patches/`, not these top-level patches | Historical/optional Dolphin-context patches; identify exact applicable base and modification notices. Do not describe them as automatically included in the current standard reference build |
| `reference-capture/controller-probe/CMakeLists.txt` | Imports SDL `5848e584a1b606de26e3dbd1c7e4ecbc34f807a6` and libusb `15a7ebb4d426c5ce196684347d2b7cafad862626`, the pinned Dolphin submodule revisions | SDL permissive notice and libusb LGPL-2.1-or-later source headers verified upstream. Complete native notice delivery and applicable source/relink materials for a distributed probe; browser SDL's excluded native backends do not establish this probe's obligations |
| `tools/slippi_format.py` and Slippi conformance checks | Parser references the Slippi format specification; `dependencies.lock.json` pins `slippi-js` `9.1.3` at `ff815345e641836a331191320c0f6eae21542a5f` as a reference tool | Pinned `package.json` declares LGPL-3.0-or-later and the license file contains LGPLv3. It is not linked into gameplay. A conformance comparison alone does not assign LGPL to an independently authored parser |
| Dusk research references | `THIRD_PARTY.md` identifies browser-integration research; no Dusk checkout/target is selected by the dependency lock/build graph inspected here | Reference-only according to the current record; no finding of copied Dusk implementation in this pass. Verify any future identified adaptation against its actual source and terms |
| Aurora/Emscripten transitive dependencies and reference-tool native dependencies | Pins/build declarations plus [runtime notices](licenses/PLAYABLE_RUNTIME_NOTICES.md) | Preserve component terms. Existing runtime aggregate is a configured inventory; it does not establish a complete native reference-tool or post-link binary inventory |

The preparation header embeds a different seed identity from the current
[materializer](../scripts/materialize_pipeline_cache.py); the receipt records
both exact hashes. `MELEE_WEB_SELECTIVE_PIPELINES` defaults to `OFF` in
`CMakeLists.txt`. This is a provenance/binding limitation, not evidence of an
active runtime failure. Old seed measurements in the release/notice documents
are now labeled historical instead of being presented as current.

For the native controller probe, the builder hashes the libusb archive but does
not establish the libusb source-tree identity in its manifest. Recording the
expected upstream pin here is only a first step; validating that correspondence
and delivering applicable notices/source materials remain open implementation
work. The pinned [libusb source header](https://github.com/libusb/libusb/blob/15a7ebb4d426c5ce196684347d2b7cafad862626/libusb/core.c)
declares LGPL-2.1-or-later, its [COPYING](https://github.com/libusb/libusb/blob/15a7ebb4d426c5ce196684347d2b7cafad862626/COPYING)
contains the LGPLv2.1 text, and the pinned [SDL notice](https://github.com/libsdl-org/SDL/blob/5848e584a1b606de26e3dbd1c7e4ecbc34f807a6/LICENSE.txt)
supplies SDL's permissive terms. This does not change the license of the
separately authored probe source by assumption.

## Four decisions and proposed dispositions

| Decision | Proposed first-pass disposition | What remains to close it |
| --- | --- | --- |
| D1: License for new project work | MIT for confirmed project-authored portions; the five-file initial allowlist above is the first proposed scope | Confirm rights holder/contributor authority and that scope; then add the standard license and scope document |
| D2: Recovered Melee/SDK source publication | Describe and preserve the recovered-source boundary explicitly; carry forward the known risk record without presenting website authorization as repository permission | Owner decision for repository/source publication, informed by the concrete patch/translation/generated-data inventory and qualified review where needed |
| D3: Current coefficient/replacement and historical audio | Preserve present behavior during the review. Keep historical GPL attribution and assess the replacement implementation separately from retained parameter/table provenance | Resolve the retained data's treatment and any combined-distribution implications; do not remove compatibility values or claim they are independently derived as a paperwork fix |
| D4: Generated/reference materials | Keep reproducible code and non-sensitive metadata reviewable; retain private retail inputs and captures outside Git; document known regeneration limits | Confirm pipeline metadata treatment, verify the reciprocal-estimate attribution, and complete native controller-probe dependency notices |

For D3, use the [coefficient review packet](AUDIO_COEFFICIENT_REVIEW.md) for the
retained-data scope, upstream contribution history, existing-terms review and
decision log. The owner declined contributor outreach; replacement remains a
fallback if the intended use cannot be supported under existing terms. Its
[completed disposition table](AUDIO_COEFFICIENT_REVIEW.md#disposition-table)
recommends retaining the current implementation and methods, preserving known
GPL history, and resolving the retained data's narrow treatment before assigning
publication terms. It records additional historical README evidence about the
replacement DSP output; the rights decision remains open. The
[pinned upstream generator](https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/docs/DSP/free_dsp_rom/generate_coefs.py)
contains the compatibility entries, including duplicate/zero writes that explain
why its entry count differs from this port's 20 effective overlays. Its
[readme](https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/docs/DSP/free_dsp_rom/dsp_rom_readme.txt)
describes their GBA-microcode purpose. This is useful origin evidence, not an
automatic exemption or a reason to change behavior without accuracy validation.

The [GPLv2 text](https://opensource.org/license/gpl-2.0), also
[retained locally](licenses/dolphin-gpl-2.0-or-later.txt), distinguishes source
copying/modification in sections 1–2 from object/executable distribution in
section 3. Keeping a separately licensed GPL tool in the repository does not
by itself establish the license of every independent file. Conversely, a
project-only MIT grant would not settle the obligations or distribution rights
of a combined program containing covered code and recovered material.

## Work this pass makes concrete

- [x] Enumerate the tracked tree and identify broad candidate, adapted, mixed,
  generated and separate-tool boundaries, with named-file evidence.
- [x] Record a scoped MIT proposal instead of implying a repository-wide grant.
- [x] Read and name a five-file initial tooling allowlist for that proposal.
- [x] Identify follow-ups beyond the resampler: original SDK translations,
  reciprocal-estimate attribution, pipeline regeneration limits, and the native
  probe's static dependency closure.
- [ ] Review an affirmative file/portion license allowlist, starting with
  separable orchestration, UI and documentation; confirm ownership before
  applying the proposed project license.
- [ ] Resolve D1–D4 and retain the decision evidence. Keep open portions marked
  unresolved rather than assigning SPDX identifiers by filename or inference.
- [ ] Complete the remaining publication checklist, including repository
  safeguards, contributor/security documents, controls and the final audit.

No runtime implementation, coefficient, dependency pin, deployment, repository
visibility, or historical notice is changed by this documentation pass.
