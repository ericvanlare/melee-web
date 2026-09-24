# Audio provider provenance and current scope

The SSM decoder uses original bank IDs, sample rates, predictor coefficients,
initial predictor/history, loop predictor/history and nibble addresses. Its
output has been compared exactly against Aurora's separate THPAudioDecode for
all4,230,233 initial channel frames in the two local main/Mario fixtures.

SEM instruction words and bank/program indices come from the local smash2.sem.
The original AXDriver interpreter, HSD synth, SDK AX allocator and SDK voice
setters execute compiled source. No PowerPC or DSP execution engine is shipped.
The native output operates at32000Hz, with the original synth callback every160
samples. The original AX engine divides each callback into five32-sample blocks.

## Resampling and coefficient provenance

`gameplay_audio_resample.c/.h` now implement the scalar conversion boundary
from a documented contract. See the [SRC contract](../docs/AUDIO_RESAMPLER_CONTRACT.md)
and [replacement evidence](../docs/AUDIO_REPLACEMENT_EVIDENCE.md) for the original
DSP observations, compatibility checks and limits. The previous adapter was
GPL-2.0-or-later code from Dolphin revision
`a2efdf1197be8132674b90fe9cf4761df39752ed`; its history and prior artifacts retain
that provenance. This change selects no project-wide license.

The 4096-byte coefficient input is never inferred from an SSM and no coefficient
binary is tracked. Browser disc import uses the project-authored generator in
`web/dsp-coefficients.mjs`. The [filter design](../docs/AUDIO_FILTER_DESIGN.md)
records its standard mathematical definitions and the retained Dolphin-derived
numerical parameters and compatibility values. Its output remains SHA-256
`d7741279c2e8ec5c5fb318f8fbdd6de6bf583520d288e836a5383233a4238179`.
This is the same replacement table used by the local reference application,
not an independent hardware oracle. Its coefficients remain approximately
equivalent to Nintendo's original DROM, not identical.

Standalone harnesses still accept separately supplied coefficient data. Do not
commit a proprietary hardware dump. A new implementation and matching output
are technical evidence; they do not establish licensing clearance for all data
or bit-exact original-hardware audio. The GPL Dolphin reference observer remains
a separate tool and is unchanged by this replacement.

## Interaural delay

This behavior was recovered directly from the pinned Melee SDK's `DSPCode.c`,
not copied from Dolphin's HLE (which currently lacks ITD). In the original DSP
program, the AXPB resides at word address0xB80:

- ITD flag0xB9B, shifts0xB9E/0xB9F, targets0xBA0/0xBA1.
- Instructions0x0216..0220 set channel input to0xCC0 + shift. Fresh input starts
  at0xCE0, so effective delay is32 minus shift.
- Instructions0x02FC..0309 compare left shift/target, increment or decrement
  once, and store the result. Right follows at0x0313..0320.
- These run after each32-sample processing block, in the five-iteration loop
  initialized at0x024F. The new shift applies to the following block.
- The0x40-byte saved history is restored at0x022A..0236 and saved after the
  five blocks at0x035A..0368.

The provider keeps that32-sample history and the original delay-ramp direction.
The analysis-only disassembly used pinned Dolphin opcode metadata, with no
emulator runtime linked into the output.

## Remaining scope

The baseline is dry stereo SFX. The optional `MELEE_WEB_AUDIO_FX` provider
supports the original lbAudioAx STD reverb(time1.88s) and delay through original
AXDriver setup and AXAux three-buffer callbacks. It retains original allocation,
callback, configuration and shutdown routines. The PPC-only HandleReverb loop
is a scalar translation preserving fused single-precision operation order.
Its impulse/latency/restart proof is not a hardware audio waveform oracle.
Unsupported reverb-HI and chorus selections terminate with their exact names.

Envelope and mixer samples use signed fixed-point GameCube arithmetic and
per-channel saturation before accumulation. The optional `MELEE_WEB_AUDIO_STREAM` provider now supports original HALPST
music blocks through lbAudioAx's actual BGM ID/path lookup, original synth
three-slot scheduling, and owned byte transfers. Its DVD entry lookup and
DevCom types0x21/0x22/0x23 are confined to explicitly registered HPS data;
other file/device operations still fail by name. Transfer completion follows
actual owned byte copies, pumped at the original160-sample audio boundary.
This does not reproduce optical-disc latency or provide general audio DMA. Original voice DSP
update lists, depop and complete hardware output comparison remain required
before claiming complete audio fidelity.

Focused tests: `test_dat_audio.py`, `test_dat_audio_programs.py`,
`test_gameplay_audio_resample.py`, and optional built `test_gameplay_audio.py`
(including the separate original AXFX callback trace).
The latter takes local main.ssm, mario.ssm, smash2.sem and dsp_coef.bin and checks
original lbAudioAx→SEM→synth→AX selection, nonzero PCM, callback partition
invariance, and repeated ownership release/restart.

HPS proof: `test_dat_audio_stream.py` validates authored saved-history/loop
fixtures and15 malformed containers, then optionally compares all50 owned
Final Destination blocks with Aurora's independent THP decoder. The built
`test_gameplay_audio_stream.py` runs actual `lbAudioAx_80023F28(78)` twice for
100seconds each, observing58 payload transfers including8 revisited blocks,
identical restart PCM, and finite nonzero output. HPS input is `/audio/sp_end.hps`
selected by the original stage/BGM tables; no media transcoding is used.
The decoded block representation retains each source offset, next pointer,
coefficients, predictor and history. Native source-header hydration replaces
only big-endian field representation and incompatible callback casts.
## Repository publication disposition

The owner accepted an internal publication review on September 20, 2026.
Current behavior is retained and current audio/data remain outside the initial
root MIT grant. Historical GPL provenance remains intact. See the
[assessment](../docs/PUBLICATION_PROVENANCE_ASSESSMENT.md#d3-current-audio-and-gpl-history)
and [license scope](../LICENSE_SCOPE.md). This repository decision does not
establish permission or corresponding-source delivery for a new binary release.
