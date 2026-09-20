# Pikachu and Pichu attribute and Article boundaries

This checkpoint adds the shared fighter attribute decoder and six checked
Article descriptors for Pikachu and Pichu. It does not enable either fighter in content, world, browser, manifests
or gameplay lifecycle fixtures. Decoding a source identity's fighter DAT is
separate from constructing or rendering its costume model; the latter remains
unverified.

## Source contract

Pikachu is `CKIND_PIKACHU = 0x0d` / `FTKIND_PIKACHU = 0x0c`; Pichu is
`CKIND_PICHU = 0x18` / `FTKIND_PICHU = 0x17`. Each has four authored material
costumes and 320 action rows. Their `ftData` extension regions are exactly
`0xf8` bytes. The complete hash-bound extraction and source pointer contract
is in the ignored [Pikachu/Pichu source contract](../work/full-game/pikachu-pichu-source-contract-v1.md).

The source `ftPikachuAttributes` table preserves the original scalar types:
`Vec2` and `Vec3` components, `ItemKind` words, signed integers, `u32`, and
the final `ftCollisionBox`. Pichu's local header is sparse, but its load path
calls the shared Pikachu initializer and special consumers read the complete
`ftPikachuAttributes` ABI. Pichu's source-owned `can_walljump` initialization
is not copied into the DAT attribute record. Both authored dynamics counts are
zero with null bone, auxiliary and mode pointers; no dynamics table is
invented. Their guard and metal roots are authored non-null and remain owned
by the later asset hydration boundary.

## Implemented boundary

`src/gameplay_pikachu_schema.h` is the single field table used by the C++ DAT
decoder and by the C source/portable offset, width and scalar-category checks.
`src/dat_fighter_runtime.cpp` and `src/gameplay_fighter_data.c` decode FT12 and
FT23 into the shared typed `0xf8` record while retaining their distinct source
identity and item values. The native C fighter-data owner accepts the authored
three-slot Article table but leaves those roots unhydrated and bit 18
explicitly unresolved. This is a checked registration boundary, not silent
success.

Thunder has three source callback entries, but the source callback state IDs
are `[-1, 0, 0]` and the DAT contains one serialized animation descriptor row.
The contract records one serialized row; callback count must not be used as a
DAT animation-row bound. Ground TJolt retains two serialized rows and air TJolt
one.

The Article owner now hydrates all six checked source roots. Thunder uses a
12-byte special record and one serialized row; ground TJolt uses 16 bytes and
two rows; air TJolt uses its 4-byte record and one row. The PlPk roots are
`0x9588/0x95d0/0x9608` with model descriptors
`0x9568/0x95a0/0x95e8`; PlPc uses `0x955c/0x95a4/0x95dc` and
`0x953c/0x9574/0x95bc`. Both ground model descriptors are present, with an
authored null joint, zero bones and zero attachment; their state rows contain
commands without animation, material or shape references. Pika's air shape
descriptor is present while Pichu's is authored null. The bridge preserves
this form and rejects malformed null-model metadata or state dependencies.

## Focused evidence

The focused C++ runtime test covers the real PlPk and PlPc archives, all four
source costume identities, 320 actions, exact `0xf8` bounds, family-specific
ItemKind/effect fields, and zero dynamics. The focused Wasm C test compiles the
source ABI checks and decodes both real `ftDataPikachu` and `ftDataPichu` roots;
it verifies the typed extension values and the explicit unresolved Article
boundary. The commands are:

```text
python3 -m unittest tests/test_dat_fighter_runtime.py -v
python3 -m unittest tests/test_gameplay_fighter_data.py -v
```

The observed runs passed 4 C++ tests and the Wasm/native trace, including both
real Pikachu-family cases. The retained [C++ test log](../work/full-game/pikachu-pichu-runtime-test-v1.log),
[Wasm/data test log](../work/full-game/pikachu-pichu-data-test-v1.log), source
probes and raw command/article evidence are retained under
`work/full-game/pikachu-pichu-*.log`.

The real-archive Article probe passed six hydrate/teardown cases, rejected the
undersized Thunder schema, rejected malformed null-model publication forms
without mutating the registered Article. The retained [native pass log](../work/full-game/pikachu-articles-native-v3.log)
and [build log](../work/full-game/pikachu-articles-build-v6.log) are bound to
the extracted inputs `PlPk.dat` SHA-256
`4befcf5d68af080ad6a18364d126b62f852e3c7f0f3ee3070168aa156bda184b` and
`PlPc.dat` SHA-256
`b4683893f3face7492dfbb1141c0f17e8cb8a4cb9599657a4a997c462119f169`.
The earlier [native v1 failure](../work/full-game/pikachu-articles-native-v1.log)
retains the generic required-descriptor rejection; [native v2](../work/full-game/pikachu-articles-native-v2.log)
retains the exact slot-1 null-joint boundary. The [build-v2 log](../work/full-game/pikachu-articles-build-v2.log)
retains the rejected C++ source-type include, and [build-v3](../work/full-game/pikachu-articles-build-v3.log)
retains the rejected first C helper wiring; these were corrected before the
passing build.
The [refreshed owner checks](../work/full-game/luigi-fountain-owner-tests-v1.log)
pass after rebuilding the probe. They verify immutable DAT bytes after hydration
and teardown, and reject publication when the source model descriptor itself
is missing. Registration now retains that presence independently from the
unresolved-field mask, so an authored null joint cannot silently stand in for
a missing descriptor. The negative case preserves all registration pointers
and unresolved bits for both families. These checks construct descriptors;
they do not yet spawn live Pikachu-family items in a source match.

## Remaining gates

Neither fighter is enabled by this checkpoint. Effects publication, action
admission, audio, model/material construction, native lifecycle, browser
upload/preload, original CSS/SSS round-trip, gameplay comparison, pixels, PCM
and live scheduling remain explicit gates for a later integration. No source
RNG, save state, input order or gameplay arithmetic was changed.
