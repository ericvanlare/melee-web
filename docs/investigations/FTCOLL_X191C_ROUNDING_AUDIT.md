# `ftcoll.c` `x191C` rounding audit

This note records the source-to-DOL basis for the explicit fused operation in
`patches/melee-gameplay.patch`. The target is GALE01 revision 2. The native
build keeps its global `-ffp-contract=off` policy; these three expressions use
`fmaf` because the corresponding retail instructions are explicitly fused.

## Reviewed stores

The three nonzero `dmg.x191C` assignments in `src/melee/ft/ftcoll.c` are all
the same source formula, `int_dmg * x3D0 + x3D4`:

| Source site | Retail operation | Retail store | Patch |
| --- | --- | --- | --- |
| `inlineA0` (fighter collision) | `0x80076B14: fmadds f0,f2,f1,f0` | `0x80076B18: stfs f0,0x191c(r30)` | `fmaf((float) int_dmg, x3D0, x3D4)` |
| `inlineA1` (fighter collision) | `0x80076C50: fmadds f0,f2,f1,f0` | `0x80076C54: stfs f0,0x191c(r28)` | `fmaf((float) int_dmg, x3D0, x3D4)` |
| `inlineItemA0` (item collision) | `0x80077AE8: fmadds f0,f2,f1,f0` | `0x80077AEC: stfs f0,0x191c(r30)` | `fmaf((float) int_dmg, x3D0, x3D4)` |

The other two DOL stores to this offset are resets and are intentionally
unchanged: `0x80067F70` corresponds to fighter initialization and
`0x8006D91C` to the per-hit reset. Neither computes a product-plus-add.

## Exact checkpoint proof

At the accepted 4-player comparison's first core divergence (trace index 264,
match frame 141), P3 Marth enters rebound. The retail speed word is
`0x4034C59D`; the native speed word is `0x4034C59E`.

The observed operands are:

* `int_dmg = 9`;
* `PlCo.dat` `x3D0 = 0x3E99999A` and `x3D4 = 0x40400000`;
* Marth's `clank_animation_length = 0x41800000` (`16.0f`), so the later
  rebound numerator is `f32(16.0f + 0.1f) = 0x4180CCCD`.

The retail `fmadds` first produces
`fmaf(9, 0x3E99999A, 0x40400000) = 0x40B66667`; the separate native
multiply/add produces `f32(f32(9 * 0x3E99999A) + 0x40400000) = 0x40B66666`.
Dividing the same numerator by those words gives `0x4034C59D` and
`0x4034C59E`, respectively. This identifies the one-ULP divergence without
attributing any later headless drawing or RNG behavior to this repair.
