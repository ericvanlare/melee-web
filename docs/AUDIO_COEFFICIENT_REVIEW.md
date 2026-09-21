# Audio coefficient provenance review

Working packet for decision D3 in the [source license inventory](SOURCE_LICENSE_INVENTORY.md)
and section 2 of the [publication checklist](PUBLIC_REPOSITORY_CHECKLIST.md).
Prepared September 20, 2026 against source commit
`bbdd675deab05151f7296b2d8c3c449e16dcfa1d`. Updated September 20, 2026 after the
owner declined contributor outreach. Status: public-record review is the active
path; no outreach sent, permission received, or legal conclusion made.
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
exceptions. Existing project records retain the historical GPL provenance.
No coefficient-specific permissive grant was identified in this limited review.
The file history does not establish assignments, complete rights ownership,
or the absence of other relevant contributors. These names record provenance;
they are not a contact list or an assumption of sole ownership.

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

## Path without contributor outreach

### 1. Classify the exact retained material

Separate the current implementation, standard mathematical methods, selected
filter parameters, observed compatibility values, complete generated table,
and historical copied implementation. Each needs a stated basis; neither
"all numbers are GPL" nor "a rewritten generator makes everything MIT" is an
established conclusion.

The U.S. Copyright Office distinguishes a program's copyrightable expression
from its functional algorithms and logic in [Circular 61][programs], and
explains the exclusion of underlying ideas/methods in [Circular 33][methods].
These general principles support asking whether protected expression remains;
they do not decide the treatment of this particular table, its arrangement,
or every relevant jurisdiction. GPL section 0 separately conditions output
coverage on the contents. Do not treat a byte match alone as a legal test.

The next deliverable is a disposition matrix for these six categories, with
exact source links, the proposed existing terms or other basis, and remaining
uncertainties. Escalate specific legal questions for qualified review where
needed; do not present a technical classification as a legal opinion.

### 2. Assess whether the existing terms fit distribution

Where covered material is used under GPL, preserve applicable notices,
modification records and license texts, and identify corresponding-source and
build-material requirements for distributed binaries. Assess the combined
player explicitly. Merely moving a file to another folder does not establish
that it is an independent work.

GPL sections 1–3 provide a conditional reuse/distribution route without a
separate permission request. That route still requires authority to distribute
all included material under compatible terms. The recovered Melee/SDK source
has its own unresolved rights boundary, so adding a blanket GPL license would
not settle the combined program's status. The source repository, historical
versions, reference tool, and browser executable need distinct inventory rows.

If this review supports keeping the current data, document the basis and add
the required notices/scope. No new upstream grant would be necessary for that
supported existing-terms route. Qualified review may still be needed to resolve
applicability; the current public record alone has not closed D3.

### 3. Replace only if the retained dependency cannot be supported

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
- [ ] Complete the six-category disposition matrix, distinguishing technical
  findings, proposed license treatment and questions requiring legal judgment.
- [ ] Map applicable existing-license obligations onto source/history,
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
