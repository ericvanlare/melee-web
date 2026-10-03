# Development audio replacement candidate

The scalar resampler and browser coefficient generator have new implementations
with recorded specifications and provenance. The coefficient output is unchanged
byte for byte, and 7,488,064 scalar comparisons preserve the previous output,
phase, history and source reads. This is regression evidence, not an original
hardware accuracy pass. The [replacement record](../AUDIO_REPLACEMENT_EVIDENCE.md)
also records an unverified original-DSP phase-writeback boundary and the retained
coefficient-data provenance that still needs release review. The separate GPL
Dolphin observer remains intact. The production release path is described above;
repository visibility is unchanged.

The owner-requested [audio listening preview](../AUDIO_PREVIEW.md) now includes
main `979fd09` (PR #49) and its per-scene asset loading. **Browser exercised**:
original CSS/SSS → Mario/Final Destination → ordinary pause/No Contest → CSS →
a second match entry retains nonzero PCM at the connected worklet. Four complete
asset scopes include the generated coefficients; outer pause/resume and Eject
pass. Both Release builds and the full local suite pass. The
[integration receipt](../evidence/audio-main-integration-v1.json) binds the
new hosted bytes and checks. The earlier owner listening feedback remains bound
to the preceding preview. Hardware PCM, broader gameplay and performance gates
remain open.
