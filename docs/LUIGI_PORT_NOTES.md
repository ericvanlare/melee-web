# Luigi attribute boundary notes

Luigi is a development candidate only. This work adds the source-derived
attribute decoder and a real-archive regression; it does not enable Luigi in
the registry, native world, browser roster, content manifests, or full-game
acceptance path.

## Source contract

Luigi is `CKIND_LUIGI = 7` and `FTKIND_LUIGI = 17`. The pinned source uses
`PlLg.dat` / `ftDataLuigi`, `PlLgAJ.dat`, four model costumes, and 312 action
rows. Its extension is the exact 0x98-byte `ftLuigiAttributes` record. The
single source table in `src/gameplay_luigi_schema.h` preserves every float and
the signed integer fields at offsets `0x88` and `0x94`.

The complete extracted source contract, archive hashes, Article/effect/audio
roots, nullable fields, and command graph are retained in the ignored
[Luigi source contract](../work/full-game/luigi-source-contract-v1.md).

## Attribute evidence

The C and C++ owners use the same field table. The C path checks source and
portable offsets, widths, and scalar categories at compile time. The C++ path
stores Luigi in a distinct typed optional and bounds the extension by its
authored 0x98 referenced region.

Evidence:

- [tracked runtime test](../work/full-game/luigi-focused-runtime-test-v2.log)
  passes both the existing six cases and the real `PlLg.dat` case. The real
  case checks 312 actions, a distinct Luigi optional, the 0x98 region, Green
  Missile misfire chance `8.0`, charge values, and signed Cyclone values `3`
  and `0`.
- [tracked native-data test](../work/full-game/luigi-focused-data-test-v2.log)
  passes the Wasm C decoder compilation and existing native data regressions.
- [source/portable ABI check](../work/full-game/luigi-source-abi-check-v2.log)
  compiles the pinned `ftLuigiAttributes` and portable table to Wasm32 and
  passes every offset, width, and scalar-type assertion.

## Remaining gates

The native fighter data path still fails explicitly at Luigi's Article boundary
until the separate Article owner admits `It_Kind_Luigi_Fire = 0x69`, its real
16-byte/four-float special block, one state, model/material resources, and
source command semantics. `PlLg.dat` relocations place the special block at
14964 and the command root at 14980, so the first command word
`0x2c000006` is not a fifth attribute. The source Luigi callback reads
special x0/x4/xC; its shared collision helpers read common ItemAttr data and
do not consume special x8. The source Article table has one authored slot; the
decoder now uses that exact bound and does not fabricate the missing Article.

The fighter asset owner must also preserve Luigi's authored null guard pose,
load effect bank 18 with two entries and its shared effect consumers, and load
the source English/Japanese audio variants. The action owner must admit Luigi's
312-row table only after the complete common/self graph and source consumers,
including opcode 42 and the still-unproven opcode 45 path, are covered.

No Luigi registry/world/browser/content-manifest change, CSS → SSS → gameplay
run, original comparison, reference capture, pixel/PCM check, or live timing
claim was made. Those remain separate gates after Article, asset, action,
effect, audio, and lifecycle ownership is complete.
