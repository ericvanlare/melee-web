# Audio coefficient provenance review

Working packet for decision D3 in the [source license inventory](SOURCE_LICENSE_INVENTORY.md)
and section 2 of the [publication checklist](PUBLIC_REPOSITORY_CHECKLIST.md).
Prepared September 20, 2026 against source commit
`bbdd675deab05151f7296b2d8c3c449e16dcfa1d`. Updated September 20, 2026 after the
owner declined contributor outreach. The disposition pass below reviews
`e5ebdcf27a43deb61f9bb481c33eda94b4f61d51`. Status: the technical classification
is complete; the final rights/distribution decision remains open. No outreach
was sent, new permission received, or project license applied.
Evidence scope: **Source identified**, including a static comparison of retained
values. This packet adds no runtime or original-hardware validation.

## Recommendation and ownership

Preserve current audio behavior and evaluate the existing published terms first.
The owner has chosen not to contact the upstream contributors. Outreach and
requesting a new permissive grant are removed from the active plan. Existing
licenses can supply permission without an individual request, provided they
apply to the exact material and their conditions can be met. No license or
publication approval is selected by this document.

Codex owns the technical classification, published-source research, compliance
inventory, replacement feasibility work if needed, and recorded recommendation.
The repository owner makes the publication decision. Any unresolved legal
interpretation can be put to a qualified reviewer with this packet; no external
contact is authorized by this plan. Original-hardware fidelity remains a
separate accuracy milestone.

## Exact material and use

The [filter design](AUDIO_FILTER_DESIGN.md) is the complete numerical attachment:
three 128-phase, four-tap banks; Hamming/Kaiser windows; cutoff choices; Kaiser
parameter; endpoint/grid convention; normalization; rounding; and all 20 retained
offset/value pairs. Use that document alongside this packet for the review.
The fourth bank starts at zero and retains four compatibility values.

| Identity | Value |
| --- | --- |
| Reviewed project generator | [web/dsp-coefficients.mjs](../web/dsp-coefficients.mjs) at the source commit above |
| Project generator SHA-256 | `13cbc7d48e7e26f3093a4f6e4f0d3763b56288668ddf54b523b283b6b44f86d1` |
| Pinned upstream generator | [generate_coefs.py][generator] at Dolphin `a2efdf1197be8132674b90fe9cf4761df39752ed` |
| Upstream generator SHA-256 | `4eb9dc22a97a3cdfdf84e794c6756d8840da80aa303543276f9d5e30dbcfb696` |
| Declared generated output | 4,096 bytes; SHA-256 `d7741279c2e8ec5c5fb318f8fbdd6de6bf583520d288e836a5383233a4238179`; existing checks are in the [replacement evidence](AUDIO_REPLACEMENT_EVIDENCE.md) |

The browser [asset adapter](../web/runtime-audio-assets.mjs) generates these bytes
and supplies them as `dsp_coef.bin`. [GameplayAudioBank](../src/gameplay_audio_bank.cpp)
decodes all 2,048 big-endian words. The [audio provider](../src/gameplay_audio.c)
allows coefficient selectors 0 through 2 and passes the selected 512-word bank
to the [resampler](../src/gameplay_audio_resample.c). Its polyphase mode selects
four consecutive words with `(fraction >> 7) & 0x01fc`.

A static comparison of the pinned upstream Python tuple with the current JS
array found exact agreement after removing one repeated assignment at `0x1b8`
and two zero assignments at `0x65b` and `0x66b` in the already-zero fourth bank:

| Bank | Retained nonzero overrides | Reachability in the reviewed audio consumer |
| --- | ---: | --- |
| 0 | 7 | Addressable in polyphase mode |
| 1 | 4 | Addressable in polyphase mode |
| 2 | 5 | Addressable in polyphase mode |
| 3 | 4 | Loaded, but outside the accepted coefficient selectors |

This accounts for all 23 upstream assignments and 20 current overrides. It
does not show which values a particular match actually reads or their audible
effect. The 16 addressable values must not be deleted on the assumption that
their upstream GBA purpose makes them irrelevant to Melee. Dropping the other
four would still leave the retained parameter/table question unresolved.

## Upstream contribution history

GitHub's file-history API at the pinned revision returned three commits for
`docs/DSP/free_dsp_rom/generate_coefs.py`. Their generator diffs were inspected:

| Commit | Identified contributor | Relevant contribution |
| --- | --- | --- |
| [388ab13][initial] | Shane Nelson, GitHub `stgn` | Introduced the windowed-sinc generator, filter choices, normalization and packed bank layout |
| [7e86907][packing] | Michael Maltese, GitHub `ligfx` | Separated integer conversion and byte packing |
| [e3531d1][observations] | Michael Maltese, GitHub `ligfx` | Changed grid endpoint handling and added the compatibility assignments |

The last commit explains that the numerical constants came from observing DSP
memory reads during GBA microcode behavior. The [upstream readme][readme] describes
the generated filters as approximate substitutes for the official DROM. Thus
the whole artifact is a generated replacement, while individual overrides have
an observation-derived origin. Neither description supplies a reuse grant.

The pinned generator has no per-file license header. Dolphin's [COPYING][copying]
describes most original code as GPLv2-or-later and points to per-file terms and
exceptions. The project [README at the pin][pinned-readme] and at both relevant
introducing revisions declares GPLv2-or-later. Existing project records retain
the historical GPL provenance. No coefficient-specific MIT/CC0 grant was
identified in this limited review.
The file history does not establish assignments, complete rights ownership,
or the absence of other relevant contributors. These names record provenance;
they are not a contact list or an assumption of sole ownership.

### Additional published evidence

The README at the [2015 generator introduction][initial-readme] and the
[2017 compatibility update][compatibility-readme], in their Sys Files sections,
describes the included DSP replacement files as written from scratch and says
they "do not contain any copyrighted material". This is an upstream
representation about the bundled replacements, including `GC/dsp_coef.bin`.
It strengthens the basis for treating numerical output separately from the
generator implementation. It was missing from the initial review packet.

That passage is absent from the README at the pinned `a2efdf1` revision. This
pass does not infer why it was removed. The historical statement is not an
explicit MIT/CC0 grant or a finding that every table choice is unprotectable;
it may be describing the absence of proprietary material in the replacements.
The introducing commit's account of observed memory values must remain part
of the record. Preserve both pieces of evidence rather than characterizing the
table as either a full Nintendo ROM dump or wholly new project-authored data.

## Questions the disposition must answer

1. What terms apply to the retained filter choices, observed values, selection
   and arrangement, and generated output? The [GPLv2 text][gpl], section 0,
   makes output coverage depend on its contents; generator provenance alone
   does not answer this question. Determine the treatment for this exact use.
2. Which published license or other supported basis applies to each retained
   contribution and output, and what attribution or other conditions remain?
   An upstream contributor's license must not be presented as a grant of
   third-party rights in original Nintendo material.
3. Does the proposed source and browser/Wasm distribution satisfy the applicable
   terms, including the combined program? Review source publication, generated
   runtime bytes and distributed executables as distinct delivery surfaces.
4. What separate treatment remains for the current replacement implementation,
   historical GPL adaptations/artifacts, and recovered Melee/SDK source? A grant
   for coefficient contributions alone does not close those questions.

The [replacement record](AUDIO_REPLACEMENT_EVIDENCE.md) describes the implementer
and specification author's actual access to the old implementation. Supply it
to the reviewer; do not characterize that process as formal clean-room work.
Original-hardware PCM agreement is separately unresolved, even if reuse terms
are established. See the [accuracy contract](ACCURACY_CONTRACT.md).

## Disposition table

Reviewed by reading the three complete current implementation files, their
historical versions at `e519c2fa4416c1ff8dba4e5467e65a2927b5ad29`, the pinned
upstream generator, mathematical references, upstream notices and contribution
history. The existing [replacement report](AUDIO_REPLACEMENT_EVIDENCE.md)
supplies regression evidence; it was not rerun for this documentation change.

**Keep** below is an engineering recommendation, not approval to publish the
combined program. **Existing GPL** identifies a supported license route for
known covered material. **Decision open** names the specific remaining scope;
it does not assert that a violation or mandatory rewrite has been established.

| Category and exact scope | Established technical / published basis | Proposed disposition and remaining gate |
| --- | --- | --- |
| Current implementation: [resampler C](../src/gameplay_audio_resample.c), [header](../src/gameplay_audio_resample.h), and generator logic in [dsp-coefficients.mjs](../web/dsp-coefficients.mjs), distinct from retained choices/data below | The C file uses separate state-consumption/render helpers and quotient/remainder floor arithmetic; the JS evaluates Bessel's integral by Simpson quadrature rather than the prior series implementation. The [contract](AUDIO_RESAMPLER_CONTRACT.md) and replacement record disclose the specification author's access to prior GPL code and the original DSP program. Changed structure and passing comparisons do not decide derivative-work status. | **Keep; project-license candidate, decision open.** MIT is the existing D1 proposal for portions the project has authority to license. Do not add an unqualified MIT/SPDX grant to these whole files until that scope and any retained protected expression are assessed. No additional rewrite is established as necessary by this source review. |
| Standard mathematical methods: sinc, Hamming/Kaiser definitions, Bessel integral, fixed-point arithmetic | [SciPy Hamming][hamming], [SciPy Kaiser][kaiser] and [NIST Bessel integral][bessel] independently describe the formulas. The current JS generator imports no NumPy/SciPy implementation. The original DSP contract separately identifies the phase/tap interface. | **Keep the methods.** There is no identified reason to replace standard mathematics. A project's source expression still needs its own license; citation to a formula is not an upstream code-license grant. Do not attribute Dolphin-specific tuning choices to these general references. |
| Selected filter design: bank/window order, cutoffs, `9 pi / 4`, endpoint/grid convention, row normalization and rounding | These choices deliberately reproduce the [pinned generator][generator]; the [filter design](AUDIO_FILTER_DESIGN.md) records them. General mathematical references do not independently justify this exact selection as original Melee behavior. | **Retain pending the numerical-data decision.** The proposed keep route treats the choices as functional design facts/methods. Whether protectable selection/expression remains is unresolved. If it does, assess the existing GPL route and combined-program implications; do not silently label these choices MIT. |
| Twenty observed compatibility values and their offsets | Exact correspondence and bank reachability are recorded above. The [introducing commit][observations] identifies observations of DSP memory. Our project inherited the values; it did not independently measure them. | **Retain pending the numerical-data decision.** Functional/interoperability observations are the proposed basis for keeping the values, supported by the historical README representation. This is a proposed interpretation, not a legal finding. Deleting only the four unused-bank values does not resolve the remaining sixteen or the parameter selection. |
| Complete generated 4,096-byte table, plus its expected hash and representative test words | No coefficient binary is tracked. The browser generates the table and the audio consumer reads it. GPL section 0 distinguishes generator execution from covered output. The historical README representation concerns these replacement DSP files; regression fingerprints establish identity, not ownership or hardware fidelity. | **Keep generation; output treatment follows the two data rows above.** Do not assert that every output is GPL merely because of its generator's history, or that generation in the browser exempts the distributed source/data. If the data must be replaced, update generator, specification and dependent tests together after validation. |
| Historical implementations and their copies: pre-replacement `src/gameplay_audio_resample.c/.h`, `web/dsp-coefficients.mjs`; [compatibility checker](../scripts/check_audio_compatibility.py) retrieving the old C implementation | The [historical C source][old-c] and [JS source][old-js] explicitly declare GPL-2.0-or-later; original references and Git history remain. The checker builds the old C code in a temporary local test executable. Its orchestration is a separate source-review scope. | **Existing GPL; preserve.** Retain notices/license and modification provenance. Assess any redistributed old or comparison binaries for source delivery. Replacing current code would not relicense or remove historical obligations. No history rewrite is selected. |

The U.S. Copyright Office distinguishes a program's copyrightable expression
from its functional algorithms and logic in [Circular 61][programs], and
explains the exclusion of underlying ideas/methods in [Circular 33][methods].
These general principles support asking whether protected expression remains;
they do not decide the treatment of this particular table, its arrangement,
or every relevant jurisdiction. GPL section 0 separately conditions output
coverage on the contents. These are the bases for the proposed functional-data
treatment, not a conclusion that the retained artifact is public domain.

## Distribution obligations

The existing GPL route requires applicable notices, modification records and
license terms; covered binaries also require a compliant corresponding-source
delivery arrangement, including relevant build materials. Sections 1–3 and
the distinction between separate and combined works govern the assessment.
The recovered Melee/SDK boundary remains separate: a blanket GPL declaration
cannot supply missing rights in that source.

This is a source/configuration map at the reviewed commit. No hosted package,
external release attachment or native binary was re-audited in this pass.

| Publication surface | What the inspected code actually delivers | Treatment and concrete remaining work |
| --- | --- | --- |
| Source repository and reachable history | Current replacement files coexist with the historical GPL versions accessible through Git. [THIRD_PARTY.md](../THIRD_PARTY.md) retains provenance and the repository contains the [full GPL text](licenses/dolphin-gpl-2.0-or-later.txt). | Preserve the historical terms and identify changes. Apply the six-category disposition to current source. Publishing the repository would expose old versions even when a selected website package excludes them. Complete D1/D2 and the final ref/artifact inventory before public visibility. |
| Audio browser package: source modules and combined Wasm | [stage_audio_preview.py](../scripts/stage_audio_preview.py), `MODULES` and `expected_files()`, includes the generator and audio modules with the native `runtime-audio-preview` producer. Its `license_notice()` and output map deliver historical provenance plus GPL text. [release_audio_player.py](../scripts/release_audio_player.py) selects the production identity. [FighterRuntime.cmake](../cmake/FighterRuntime.cmake) compiles the replacement C alongside recovered AX/AXFX and other sources. | Existing notice delivery is identifiable. It is not a determination that current code/table is GPL, or proof of rights in the combined program. If covered material remains, the inspected packager does not supply a release-specific corresponding-source bundle or offer; choose, implement and verify an applicable source-delivery route. Bind any claimed compliance to the exact release manifest and source/dependency graph. |
| Generated coefficient bytes | [runtime-audio-assets.mjs](../web/runtime-audio-assets.mjs) generates the bytes in memory; the audio package includes the generator, not a hosted coefficient binary. Hosted verification in `stage_audio_preview.py` expects the coefficient-file route to be absent. | The absence of a binary file is intentional, not a missing package input. The source still carries parameters and values. Apply the numerical-data disposition to both source and resulting output; client-side generation is not itself a licensing conclusion. |
| Silent browser package | [build_public.py](../scripts/build_public.py) and the public branch in [FighterRuntime.cmake](../cmake/FighterRuntime.cmake) exclude the resampler and browser audio modules, while retaining the other selected recovered/runtime sources. [public notices](../web/public/notices.html) describe the silent boundary. | Keep its separate inventory. Its audio exclusion does not clear repository history, the audio package, or the recovered code retained in the silent executable. No new silent-package audit is claimed here. |
| Separate Dolphin observer source and any native binary | [LICENSES.md](../reference-capture/dolphin/LICENSES.md) records GPL-2.0-or-later and the pinned downstream composition. [build_reference_dolphin.py](../scripts/build_reference_dolphin.py), `archive_provenance()`, saves a small manifest/patch/overlay/helper archive keyed by binary identity; the full upstream source/build trees remain outside that archive. | **Existing GPL; keep as a separate tool.** The small reconstruction receipt is useful evidence but is not by itself a complete corresponding-source delivery. If distributing the binary, verify the full applicable source/build/dependency materials and recipient access. Do not infer player licensing from this separate tool. |
| Local historical comparator | [check_audio_compatibility.py](../scripts/check_audio_compatibility.py) obtains the old C/header from Git, compiles both implementations with the comparison driver into a temporary executable, and removes its temporary outputs. | Preserve original notices and its local-test scope. There is no comparator package produced by this script. A future distributed comparator would need its own source/notices inventory; local execution does not establish a player distribution obligation. |

The [release review](PUBLIC_RELEASE_REVIEW.md#gpl-corresponding-source-delivery-when-covered-code-is-distributed)
records the source-delivery gap. Reading the current audio packager confirms
that it supplies notices but does not add a corresponding-source mechanism.
Repository availability alone would not demonstrate correspondence for every
published binary, dependency or downstream change. The
[production-audio runbook](AUDIO_PRODUCTION.md) is a procedure; this pass found
no `melee-web-audio-player-package-v1` receipt in tracked `docs/evidence/` and
does not infer the live deployment state from that absence.

## Recommendation after the pass

Keep the working implementation and table while recording the proposed
functional-data treatment. This pass found stronger published support for
that route and did not establish a mandatory audio rewrite. Preserve the
known GPL history/reference-tool terms and current provenance notices.

Two determinations remain before closing the audio publication decision:

1. Whether the current replacement retains protected implementation expression
   or the chosen parameters/observations/table retain protectable expression
   requiring an upstream license. The source comparisons, mathematical
   references and historical README representation are the evidence packet
   for this specific question; they are not legal clearance.
2. If covered material remains, whether the intended combined source/binary
   distribution can satisfy the existing terms given the recovered-source
   boundary, and what exact source delivery is required. Record that decision
   against the real release inventory, with qualified review where needed.

Standard methods can remain; historical covered sources can retain their
existing terms. New project-license assignments still depend on D1 authority.
No outreach, blanket relicensing, history rewrite or coefficient change is
needed to finish the technical classification. Repository safeguards and
contributor documentation can proceed while these rights questions are resolved.

## When a replacement would be justified

Prepare a bounded engineering proposal derived from documented original
behavior and standard signal-processing methods. Justify the required filter
behavior independently; do not use Dolphin's exact output hash, retained
constants or selected parameters as the sole specification of a supposedly
independent replacement. Record actual source access and preserve existing
provenance. Changing names or re-expressing the same table is not enough to
establish independence, and a formal clean-room claim would be inaccurate.

Prototype separately, compare against an independent original-game/hardware
reference, reduce the first PCM divergence, and retain known mismatches.
Changing the shipping coefficients requires the accuracy contract's evidence;
"sounds fine" or agreement with the same Dolphin approximation is insufficient.
Do not replace the current path merely to simplify a licensing label.

Separately supplied local coefficient input is another design option if exact
original coefficients are required. Native harnesses already accept it, but
the browser currently generates its table. A browser input path would require
implementation and verification, a documented lawful source of that input, and
an explicit extra user requirement; do not assume it can be extracted from the
game disc. It would not resolve historical source or combined-program rights.

## Work and decision log

- [x] Bind the reviewed source, upstream generator, numerical scope and use path.
- [x] Identify the generator's recorded contributors and relevant commits.
- [x] Record the owner's no-outreach preference and replace the active inquiry
  workflow with public-record review. The earlier unsent draft remains in Git
  history; no messages were sent.
- [x] Complete the six-category disposition matrix, distinguishing technical
  findings, proposed license treatment and questions requiring legal judgment.
- [x] Map applicable existing-license obligations onto source/history,
  separate reference tools, generated output and combined player distribution.
- [ ] Record a supported disposition with exact files/data, terms, notices,
  affected distribution surfaces, evidence, date and owner decision. Preserve
  unresolved questions explicitly; no response or elapsed time closes them.
- [ ] If existing terms cannot support the intended use, prepare and validate a
  bounded replacement proposal before changing the shipping audio path.
- [ ] Implement the resulting notices/scope or validated replacement and run
  checks appropriate to that change. Preserve historical GPL evidence.
- [ ] Update D3 and the publication checklist only for the scope actually closed.

Repository safeguards and contributor documentation can proceed during this
review. Full hardware-audio acceptance is a separate milestone from publishing
honestly described experimental source. A silent website profile does not
remove source/history from the repository-publication review.

[generator]: https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/docs/DSP/free_dsp_rom/generate_coefs.py
[readme]: https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/docs/DSP/free_dsp_rom/dsp_rom_readme.txt
[copying]: https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/COPYING
[initial]: https://github.com/dolphin-emu/dolphin/commit/388ab13db1c2a10534a9b81acba8fef98d96409c
[packing]: https://github.com/dolphin-emu/dolphin/commit/7e869070e31cf6182de4495163242c049e9712c7
[observations]: https://github.com/dolphin-emu/dolphin/commit/e3531d17d700339828d7bab192c66fba5fcbac86
[gpl]: https://opensource.org/license/gpl-2.0
[programs]: https://www.copyright.gov/circs/circ61.pdf
[methods]: https://www.copyright.gov/circs/circ33.pdf
[initial-readme]: https://github.com/dolphin-emu/dolphin/blob/388ab13db1c2a10534a9b81acba8fef98d96409c/Readme.md
[compatibility-readme]: https://github.com/dolphin-emu/dolphin/blob/e3531d17d700339828d7bab192c66fba5fcbac86/Readme.md
[pinned-readme]: https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/Readme.md
[hamming]: https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.windows.hamming.html
[kaiser]: https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.windows.kaiser.html
[bessel]: https://dlmf.nist.gov/10.32.E1
[old-c]: https://github.com/ericvanlare/melee-web/blob/e519c2fa4416c1ff8dba4e5467e65a2927b5ad29/src/gameplay_audio_resample.c
[old-js]: https://github.com/ericvanlare/melee-web/blob/e519c2fa4416c1ff8dba4e5467e65a2927b5ad29/web/dsp-coefficients.mjs
