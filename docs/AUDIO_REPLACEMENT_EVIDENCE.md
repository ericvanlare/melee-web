# Development audio implementation replacement

September 19, 2026. Scope: the scalar resampler and browser coefficient generator.
The separate Dolphin reference observer and its patches/licenses remain intact.
The public profile remains silent and excludes these audio modules. This change
neither deploys audio nor changes repository visibility or the project license.

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
| Independent arithmetic cases | All fractional phases for four-tap row selection and signed linear endpoint interpolation; negative floor, both saturation signs, accumulator sums exceeding signed 32-bit range, zero-to-four source reads, carry, mode changes, direct-mode behavior and the unchanged ITD ramp. |
| Affected build | Emscripten checked builds of `gameplay_audio_trace`, `gameplay_audio_fx_trace` and `gameplay_audio_stream_trace` succeeded. |

Reproduce the scalar comparison with:

```sh
python3 scripts/check_audio_compatibility.py
```

It retrieves the historical GPL adapter from the pinned local Git object,
retains its notices, and compiles it only inside an automatically removed
temporary directory. No reference object or generated table is added to a
player target. Missing Git history is an explicit failure. The regular test
suite exercises independent boundary cases without requiring historical source.

Owned SSM/SEM/HPS fixtures are local inputs only. Their original-source voice,
effects and stream checks are recorded separately from scalar compatibility;
passing either does not establish original-hardware waveform equivalence.

## Accuracy limits

The original DSP direct path clears saved fractional phase at a 32-sample block
boundary. The existing port preserves it, and this replacement preserves that
behavior. It is a newly recorded existing deviation, requiring a focused
original-DSP mode-transition comparison and a separate fix. DSP extreme-product
scaling and accumulator extraction remain unverified against hardware.

The coefficient approximation, outstanding audio update/depop work and broader
audio-fidelity gates remain open. Same-output comparisons against the old port
are **behavioral regression evidence**, not a retail or hardware accuracy pass.
