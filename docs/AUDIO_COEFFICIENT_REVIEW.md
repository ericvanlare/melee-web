# Audio coefficient provenance review

Working packet for decision D3 in the [source license inventory](SOURCE_LICENSE_INVENTORY.md)
and section 2 of the [publication checklist](PUBLIC_REPOSITORY_CHECKLIST.md).
Prepared September 20, 2026 against source commit
`bbdd675deab05151f7296b2d8c3c449e16dcfa1d`. Status: technical packet and outreach
draft prepared; no outreach sent, permission received, or legal conclusion made.
Evidence scope: **Source identified**, including a static comparison of retained
values. This packet adds no runtime or original-hardware validation.

## Recommendation and ownership

Preserve the current audio behavior while resolving the retained data's terms.
Start with a narrowly scoped provenance/permission inquiry and qualified review
of any remaining questions. A further implementation rewrite would not by
itself resolve the origin of intentionally retained parameters and values.
Keep original-hardware fidelity as a separate accuracy milestone.

Codex owns technical preparation, source correspondence, draft revisions,
tracking responses supplied to this task, and preparing the final recorded
disposition. The repository owner authorizes external contact and makes the
publication decision. Relevant rights holders establish any permission they
can grant; qualified counsel resolves legal questions as needed. Contributor
identification below is evidence of participation, not proof of ownership or
authority to license every part of the data.

## Exact material and use

The [filter design](AUDIO_FILTER_DESIGN.md) is the complete numerical attachment:
three 128-phase, four-tap banks; Hamming/Kaiser windows; cutoff choices; Kaiser
parameter; endpoint/grid convention; normalization; rounding; and all 20 retained
offset/value pairs. Include that document with the inquiry, not just this packet.
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

## Upstream history and candidate contacts

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
or the absence of other relevant contributors; ask the candidate contacts to
confirm or redirect the inquiry. No personal email addresses are collected here.

## Questions the disposition must answer

1. What terms apply to the retained filter choices, observed values, selection
   and arrangement, and generated output? The [GPLv2 text][gpl], section 0,
   makes output coverage depend on its contents; generator provenance alone
   does not answer this question. Determine the treatment for this exact use.
2. If relying on permission, who can grant it, which exact contributions and
   output does it cover, and what attribution or other conditions remain?
   Contributor permission must not be presented as a grant of third-party rights
   in observed behavior or original Nintendo material.
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

## Outreach draft — not sent

Candidate recipients: `stgn` and `ligfx`, subject to confirming an appropriate
contact route. The repository is private; attach the reviewed filter design and
replacement provenance record instead of assuming private repository links are
accessible. This draft asks about the contributions they control and seeks
referrals for anything beyond that scope.

**Subject: Clarifying reuse terms for Dolphin's generated DSP coefficient data**

Hello,

We are preparing Melee Web, an experimental source port of vanilla Melee to a
desktop browser, for public source publication. Its compiled game/audio path
uses a generated DSP coefficient table. Users supply game content locally.

Our current generator uses a newly written numerical implementation, but
deliberately retains the filter parameters and output associated with Dolphin's
`docs/DSP/free_dsp_rom/generate_coefs.py` at revision
`a2efdf1197be8132674b90fe9cf4761df39752ed`, including 20 nonzero compatibility
overrides. The attached specification lists every retained value and numerical
choice. The separate provenance record explains the replacement process and
its limitations; we are not claiming formal clean-room isolation.

The file history identifies your contributions in the [initial filter][initial]
and [compatibility update][observations]. Could you confirm the origin and
intended reuse terms of the contributions you
control, including the parameter choices and compatibility data? We understand
the compatibility commit describes observed memory values; we do not assume
you can grant rights belonging to anyone else.

Where you hold applicable rights, would you be willing to make those specific
contributions, including their use in generated coefficient output, available
under the standard MIT license in addition to existing terms? The intended use
includes publishing our generator and distributing it with browser/Wasm builds
that generate and use the table. We would preserve the required attribution and
license notice. Please identify any excluded material or other contributors
whose confirmation we should seek. An existing coefficient-specific permission
or clarification of the current terms would also help.

This request does not seek to relicense unrelated Dolphin code. Historical
adaptations and the separate GPL Dolphin reference tool retain their notices;
the recovered game's publication rights are being considered separately.

Thank you for helping us document the boundary accurately.

## Work and decision log

- [x] Bind the reviewed source, upstream generator, numerical scope and use path.
- [x] Identify the generator's recorded contributors and relevant commits.
- [x] Prepare the inquiry and concrete questions for a qualified reviewer.
- [ ] Owner authorizes external contact; confirm recipients/channel and send
  the reviewed draft with its two attachments. Record what was actually sent.
- [ ] Obtain and assess responses, including rights-holder scope and any other
  contributors. No response is not permission; keep this item open and assess
  the qualified-review route if contact is unavailable.
- [ ] Record a supported disposition with exact files/data, terms, notices,
  affected distribution surfaces, reviewer/permission evidence, date and owner
  decision. Keep confidential correspondence outside Git; record a publishable
  decision summary and an access-controlled evidence reference where necessary.
- [ ] Implement required notices or a justified replacement, then perform the
  checks appropriate to that change. A replacement requires documented origin
  and original-game validation; preserve historical GPL evidence in either case.
- [ ] Update D3 and the publication checklist only for the scope actually closed.

If usable terms cannot be established, prepare an independently documented
replacement proposal and its original-reference validation plan. Do not change
coefficients, relax accuracy gates, or treat a silent website profile as a
resolution for source/history publication merely to close this review.

[generator]: https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/docs/DSP/free_dsp_rom/generate_coefs.py
[readme]: https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/docs/DSP/free_dsp_rom/dsp_rom_readme.txt
[copying]: https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/COPYING
[initial]: https://github.com/dolphin-emu/dolphin/commit/388ab13db1c2a10534a9b81acba8fef98d96409c
[packing]: https://github.com/dolphin-emu/dolphin/commit/7e869070e31cf6182de4495163242c049e9712c7
[observations]: https://github.com/dolphin-emu/dolphin/commit/e3531d17d700339828d7bab192c66fba5fcbac86
[gpl]: https://opensource.org/license/gpl-2.0
