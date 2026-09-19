# Browser audio filter table

`web/dsp-coefficients.mjs` creates the 4,096-byte coefficient artifact used
by the browser audio path. It is deliberately a small numerical generator: a
call to `createAudioFilterTable()` allocates and returns a new `Uint8Array`.
The exported `AUDIO_FILTER_SHA256` value is an integrity identifier for the
declared output, not an audio-equivalence claim.

## Layout

Each bank contains 512 signed 16-bit words in big-endian order. A bank has 128
phases, and each phase has four consecutive taps.

| byte range | bank | window and cutoff |
| --- | ---: | --- |
| `0x0000..0x03ff` | 0 | Hamming, `0.5` |
| `0x0400..0x07ff` | 1 | Kaiser, `0.75`, `beta = 9 pi / 4` |
| `0x0800..0x0bff` | 2 | Hamming, `1` |
| `0x0c00..0x0fff` | 3 | reserved, zero before compatibility overlays |

For phase `p` and tap `t`, the source grid position is

```
j = 127 - p + 128 t
x = 4 j / 511 - 2
```

The unscaled sample is `sinc(cutoff * x) * window(j)`, where
`sinc(z) = sin(pi z) / (pi z)` and `sinc(0) = 1`. The Hamming window is
`0.54 - 0.46 cos(2 pi j / 511)`. The Kaiser window is

```
I0(beta sqrt(1 - (2j/511 - 1)^2)) / I0(beta)
```

For each of the first three banks, the generator finds the largest
four-tap row sum and scales that bank so the row sum maps to `32767`. Each
scaled tap uses signed nearest-even rounding, then is written high byte first. This keeps the arithmetic and byte
order explicit rather than relying on a typed-array host-endian choice.

## Bessel evaluation and compatibility values

The order-zero modified Bessel function is evaluated from its integral
definition,

```
I0(x) = (1/pi) integral from 0 to pi of exp(x cos(theta)) dtheta,
```

using a fixed 1,024-subinterval composite Simpson quadrature. The arguments in
this Kaiser window are bounded, so the direct exponential evaluation is stable
and deterministic in the target JavaScript runtime. The definitions of the
Hamming and Kaiser windows follow the standard SciPy window references
([Hamming](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.windows.hamming.html)
and
[Kaiser](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.windows.kaiser.html));
the Bessel integral is the standard NIST definition ([DLMF
10.32.E1](https://dlmf.nist.gov/10.32.E1)).

Twenty word values are applied after numerical generation as compatibility
data. They are reproduced as unsigned hexadecimal words at these word offsets:

```
03b:0065  043:0076  0ca:3461  0e2:376f  1b8:007f
1f8:0009  1fc:0003  229:657c  231:64fc  259:6143
285:5aff  456:102f  468:f808  491:6a0f  5f1:0200
5f6:7f65  66c:06f2  6fe:0008  723:ffe0  766:0273
```

These numerical compatibility values were observed in the Dolphin replacement
DROM. Its [pinned readme](https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/docs/DSP/free_dsp_rom/dsp_rom_readme.txt) describes the corrections as serving GBA microcode. This table is an approximation, not original Nintendo ROM data. That observation documents why the values are
retained; it does not claim new coefficients or eliminate every data-provenance
question.

## Focused check

Run the focused test with the pinned project Node executable:

```sh
.deps/emsdk/node/24.19.0_64bit/bin/node tests/dsp_coefficients_test.mjs
```

The test checks the output size, SHA-256 digest, big-endian signed words,
positive four-tap row sums for the generated banks, the zero-filled reserved
bank outside its explicit compatibility overlays, representative compatibility
words, and independent allocation across calls.

## Implementation provenance

The implementation was authored from the numerical specification above and the
standard mathematical definitions, without providing the previous generator
source to its implementer. The specification author had inspected the earlier
GPL-derived adapter. Its filter parameters, expected output identity and
compatibility values are deliberately retained. This records the actual work
process; it is not a formal clean-room or licensing-clearance claim. No new
project-wide license is selected by this change. Historical versions retain
their original notices.
