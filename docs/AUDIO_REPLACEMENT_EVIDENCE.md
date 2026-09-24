# Development audio implementation replacement

September 19, 2026. Scope: the scalar resampler and browser coefficient generator.
The separate Dolphin reference observer and its patches/licenses remain intact.
At this replacement checkpoint, the production public profile was silent and
excluded these audio modules. The later owner-requested [staging listening
preview](AUDIO_PREVIEW.md) and [production audio](AUDIO_PRODUCTION.md) use separate
audio-enabled profiles. Consult those boundaries for current packaging; this
historical replacement report does not record a deployment. The
[coefficient disposition review](AUDIO_COEFFICIENT_REVIEW.md) tracks the remaining
implementation/data licensing questions without changing repository visibility
or selecting a project license.

## Implementation and provenance

- `src/gameplay_audio_resample.c/.h` now implement the existing port contract
  from a written behavior specification. The [SRC contract](AUDIO_RESAMPLER_CONTRACT.md)
  separates observations of the original AX program from preserved port behavior.
- `web/dsp-coefficients.mjs` now evaluates the filter directly from phase/tap
  positions and standard Hamming/Kaiser definitions, using a numerical integral
  for the modified Bessel function. The [filter design](AUDIO_FILTER_DESIGN.md)
  records the formula, parameters, rounding and output representation.
- The coefficient **data is deliberately unchanged**, including 20 compatibility
  word values originating in Dolphin's replacement DROM. Removing the prior
  generator implementation does not erase that data provenance. Its table is an
  approximation, not a Nintendo hardware dump or an independent audio oracle.

The implementers were not provided the old adapter/generator source. The lead
had inspected it, supplied compatibility specifications, reviewed and edited the
replacements, and compared the outputs with the historical implementation.
This is a factual provenance record, not a claim of formal clean-room isolation
or a conclusion that all GPL/data licensing questions are closed. Select and
review the license for project code and retained numerical data before widening
release scope. Historical GPL source and artifacts retain their notices and
applicable obligations; the [GPL text](licenses/dolphin-gpl-2.0-or-later.txt) stays.

## Compatibility evidence

The baseline is commit `e519c2fa4416c1ff8dba4e5467e65a2927b5ad29`.
The source-only tests contain synthetic signals, not extracted audio.

| Check | Observed scope |
| --- | --- |
| Coefficient output | All 4,096 bytes retain SHA-256 `d7741279c2e8ec5c5fb318f8fbdd6de6bf583520d288e836a5383233a4238179` in pinned Node 24.19.0 and the in-app Chrome 153 browser. Browser generation took 6 ms in the single observed check; this is not a performance acceptance claim. |
| Scalar comparison | 7,488,064 exact comparisons of output sample, fractional phase, all four history words and source-read count/data under AddressSanitizer and UndefinedBehaviorSanitizer. Includes all 65,536 phases, 11 boundary ratios from zero through `0x40000`, all three modes, all three actual coefficient banks, and one million deterministic changing-ratio/mode calls with synthetic signed coefficients. |
| WebAssembly compiler modes | The same 7,488,064-vector driver passes when both implementations are compiled with pinned Emscripten at `-O0` and `-O3`, then executed in pinned Node. These repeat the native vectors on the shipping instruction set; they are not additional unique input cases. |
| Second JavaScript engine | The historical and replacement generators produce directly equal 4,096-byte arrays in the host JavaScriptCore framework, with the same expected SHA-256. This evaluates the generator bodies through the engine C API; it is not a Safari player-integration test. |
| Independent arithmetic cases | All fractional phases for four-tap row selection and signed linear endpoint interpolation; negative floor, both saturation signs, accumulator sums exceeding signed 32-bit range, zero-to-four source reads, carry, mode changes, direct-mode behavior and the unchanged ITD ramp. |
| Affected build | Emscripten checked builds of `gameplay_audio_trace`, `gameplay_audio_fx_trace` and `gameplay_audio_stream_trace` succeeded. |
| Owned sound effects | Five source sound IDs (`0`, `74`, `443`, `180000`, `180001`), each run twice with different callback partitioning: all reported PCM fingerprints, energy, sample selection and restart results match the baseline. |
| Owned effects/stream | AXFX allocation, callback and three-buffer latency/restart checks pass. Two 100-second HPS runs each cross 58 payload transfers with 8 revisits; both retain PCM FNV-1a `e8957ede28b9ad45` and the baseline energy/result records. |
| Integrated full suite | `python3 -m unittest discover -s tests -v`: 1,002 tests, 46 optional skips, no failures (180.137 s), after integrating main `7d02d4c` in source commit `1604186`. The three affected audio targets also build on that combined tree. |
| CI | [Verify run 35472491056](https://github.com/ericvanlare/melee-web/actions/runs/35472491056) passed for audio implementation commit `ce42469`. |

Reproduce the scalar comparison with:

```sh
python3 scripts/check_audio_compatibility.py
```

It retrieves the historical GPL adapter from the pinned local Git object,
retains its notices, and compiles it only inside an automatically removed
temporary directory. No reference object or generated table is added to a
player target. Missing Git history is an explicit failure. The regular test
suite exercises independent boundary cases without requiring historical source.

Owned SSM/SEM/HPS fixtures are local inputs only. Voice fingerprints (FNV-1a,
not cryptographic content identities) match before and after:

| Sound ID | PCM fingerprint |
| --- | --- |
| 0 | `42fc66af0c7e5ac2` |
| 74 | `e0b89ce5e03b3709` |
| 443 | `b16baf96b5f92367` |
| 180000 | `23895959ef95e6d5` |
| 180001 | `469631f296cab776` |

Run the optional owned-fixture voice/effects and stream checks after building
the targets and supplying local `assets-local/next-gate/` inputs:

```sh
python3 -m unittest discover -s tests -p 'test_gameplay_audio*.py' -v
```

These are original-source integration and regression checks. Neither their
fingerprints nor scalar compatibility establish original-hardware waveform
equivalence.

## What exact compatibility means

A separate source review found no behavioral difference over the caller's valid
domain. The [contract's equivalence argument](AUDIO_RESAMPLER_CONTRACT.md#equivalence-to-the-historical-port)
covers arbitrary signed-16-bit sample/history/coefficient values, not just the
sampled vectors. It is a code/arithmetic review, not a machine-checked formal proof.

The numerical test count also has limits: the exhaustive phase matrix starts
from one extreme history pattern, and the million-call sequence varies mode
and ratio but restarts the source callback's RNG at the same seed on each call.
It does not enumerate every possible history, coefficient table or stream.
The equivalence argument is therefore important alongside the differential
checks, independent arithmetic cases and real-asset PCM fingerprints.

Together these establish strong support for a drop-in replacement of the old
port. They do not establish original-hardware fidelity, complete browser audio
performance, or licensing independence of the retained numerical table. Those
are separate questions; a passing functional test cannot settle the latter.

## Accuracy limits

Direct-mode fractional phase writeback still needs verification against the
original DSP. The port preserves phase. The local disassembly's accumulator
clear and phase store use different registers, so that instruction pair does not
establish a zero-phase reset or a confirmed port defect. A focused original-DSP
mode-transition trace is needed. Extreme-product scaling and accumulator
extraction also remain unverified against hardware.

The coefficient approximation, outstanding audio update/depop work and broader
audio-fidelity gates remain open. Same-output comparisons against the old port
are **behavioral regression evidence**, not a retail or hardware accuracy pass.
