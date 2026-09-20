# Pikachu and Pichu development integration

This checkpoint covers the shared fighter attribute decoder, six checked
Article descriptors, source effects and bounded native fighter traces for
Pikachu and Pichu. Four named shared-content orientation fixtures pass at the
native boundary. Original CSS/SSS reaches both fighters in the browser, where
all 30 action cases pass functionally. The first cold sweeps fail timing and
live-pipeline gates; warm sweeps pass. Original-reference and broader
acceptance remain open.

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
invented. Their guard and metal roots are authored non-null and hydrated by the
fighter asset owner.

## Implemented boundary

`src/gameplay_pikachu_schema.h` is the single field table used by the C++ DAT
decoder and by the C source/portable offset, width and scalar-category checks.
`src/dat_fighter_runtime.cpp` and `src/gameplay_fighter_data.c` decode FT12 and
FT23 into the shared typed `0xf8` record while retaining their distinct source
identity and item values. The native C fighter-data owner accepts the authored
three-slot Article table, while the Article owner hydrates all six checked
source roots. Publication keeps source-null forms explicit and rejects
malformed or partially populated rows; it does not turn an absent source
descriptor into a synthetic object.

Thunder has three source callback entries, but the source callback state IDs
are `[-1, 0, 0]` and the DAT contains one serialized animation descriptor row.
The contract records one serialized row; callback count must not be used as a
DAT animation-row bound. Ground TJolt retains two serialized rows and air TJolt
one.

Thunder uses a 12-byte special record and one serialized row; ground TJolt uses 16 bytes and
two rows; air TJolt uses its 4-byte record and one row. The PlPk roots are
`0x9588/0x95d0/0x9608` with model descriptors
`0x9568/0x95a0/0x95e8`; PlPc uses `0x955c/0x95a4/0x95dc` and
`0x953c/0x9574/0x95bc`. Both ground model descriptors are present, with an
authored null joint, zero bones and zero attachment; their state rows contain
commands without animation, material or shape references. Pika's air shape
descriptor is present while Pichu's is authored null. The bridge preserves
this form and rejects malformed null-model metadata or state dependencies.

The effects boundary admits a complete source-null effect row while rejecting
partial/nonzero-null rows. This preserves the source distinction between an
empty effect entry and malformed ownership. The retained Pikachu effect probe
also checks the original low-byte particle palette format and complete source
word. Pichu's action stream retains opcode 51 as a signed 26-bit operand and
routes it to the original `ftAction_80072BF4` /
`Fighter_TakeDamage_8006CC7C` self-damage consumer; it is not decoded as an
unsigned hitbox field.

## Focused evidence

The focused C++ runtime test covers the real PlPk and PlPc archives, all four
source costume identities, 320 actions, exact `0xf8` bounds, family-specific
ItemKind/effect fields, and zero dynamics. The focused Wasm C test compiles the
source ABI checks and decodes both real `ftDataPikachu` and `ftDataPichu` roots;
it verifies the typed extension values and explicit Article ownership. The
commands are:

```text
python3 -m unittest tests/test_dat_fighter_runtime.py -v
python3 -m unittest tests/test_gameplay_fighter_data.py -v
```

The observed decoder runs passed 4 C++ tests and the Wasm/data trace. The
four named native orientation fixtures pass at shared-content scope. The
forward Pikachu/Pichu runs actively drive their source specials, while the
reverse-orientation fixtures actively drive the shared Mario control path; the
reverse runs do not claim a second Pikachu/Pichu special-action orientation.
The authored costume loop is exercised in each retained run:

- [Pikachu native v2](../work/full-game/pikachu-native-v2.log)
- [Pikachu reverse native v1](../work/full-game/pikachu-native-reverse-v1.log)
- [Pichu native v1](../work/full-game/pichu-native-v1.log)
- [Pichu reverse native v1](../work/full-game/pichu-native-reverse-v1.log)

The retained [C++ test log](../work/full-game/pikachu-pichu-runtime-test-v1.log)
and [Wasm/data test log](../work/full-game/pikachu-pichu-data-test-v1.log)
cover the real archive and ABI checks. The first match attempt,
[pikachu-native-v1](../work/full-game/pikachu-native-v1.log), rejected
EfPkData bank 7 entry 5 because it required a static model. That authored row
is entirely empty; the shared decoder now preserves its index and null
descriptor. The passing v2 run follows that loader correction.

The historical real-archive Article descriptor probe passed six
hydrate/teardown cases, rejected the undersized Thunder schema, and rejected
malformed null-model publication forms without mutating the registered Article.
The retained [native pass log](../work/full-game/pikachu-articles-native-v3.log)
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
and unresolved bits for both families. These are historical descriptor-only
checks; the later shared match fixture covers live source Article spawn, drain
and teardown.

The standalone effect boundary passed in
[pikachu-effects-native-v3](../work/full-game/pikachu-effects-native-v3.log)
after complete-null row support and the real particle-runtime begin/end
boundary. The separate historical effects-v1 fixture incorrectly rejected an authored-valid
static-model/null-animation form, while v2 reached the real Pikachu fixture
without beginning/ending the original particle runtime; both were corrected
before v3. Malformed partial/nonzero-null forms remain rejected in the passing
v3 checks.

## Browser discovery and renderer preparation

The original CSS/SSS route reaches Pikachu/Mario and Pichu/Mario Final
Destination matches with development audio enabled. Both first cold sweeps
complete all 30 declared action cases over 5,600 source frames. Their retained
acceptance failures are:

| Fighter | Native maximum | Browser maximum | Browser gaps | Live pipelines | Audio underrun frames |
| --- | ---: | ---: | ---: | ---: | ---: |
| Pikachu | 42.140 ms | 55.035 ms | 3 | 9 | 0 |
| Pichu | 32.020 ms | 46.285 ms | 1 | 9 | 0 |

Pikachu has three native callbacks over 33.3 ms; Pichu has one over the
16.67 ms target and none over 33.3 ms. Neither action window uses a timing
resume, loses focus or grows the Wasm heap. Each following warm sweep passes
all 30 cases and 5,600 frames, with zero hard failures. These warm results do
not erase the failed cold runs or the separate entry metrics. See the
[Pikachu discovery](../work/full-game/pikachu-browser-discovery-v1/report.json)
and [Pichu discovery](../work/full-game/pichu-browser-discovery-v1/report.json).

The reviewed cold exports add 11 Pikachu entry/action descriptors and two
additional Pichu descriptors, preserving every existing payload. Yoshi's Island
64 contributes another 32. The resulting seed retains all 750 previous rows
and contains one shader plus 794 pipelines. The review excludes driver cache
and game assets; it neither advances source state nor changes draw ordering.
The [fresh Pikachu pair](../work/full-game/pikachu-browser-pair-v1/report.json)
passes all 30 cases and 5,600 source frames per run. Cold/warm native maxima
are 8.205/8.095 ms and browser maxima are 23.140/25.835 ms. Both runs have
zero native target misses, browser gaps/long tasks, audio underruns, live
pipelines, timing resumes, focus losses or Wasm heap growth. This is the
measured action-window scope at 640×480, DPR 1 on Apple M4/macOS 26.6.2/
Chrome 153.0.8010.50; entry and broader gameplay remain separate.

The [fresh Pichu pair](../work/full-game/pichu-browser-pair-v1/report.json)
also passes all 30 cases and 5,600 source frames per run on the same frozen
build and configuration. Cold/warm native maxima are 8.000/8.160 ms and
browser maxima are 26.035/26.425 ms, with the same zero failure and heap-growth
counters. Together these four sweep windows cover 22,400 source frames; they
do not establish independent original behavior or complete matches.

## Remaining gates

Both identities are enabled in the shared content/manifests and 320-row
action tables, and the shared CSS/SSS path is wired. The native match fixture
spawns and drains the ground-jolt parent from both ground/aerial neutral
specials, plus Thunder Articles; the child air-jolt lifetime is not separately
asserted. Pichu's
forward special also observes source self-damage. These are bounded
source-content and teardown checks, not every authored move or interaction.
Complete audio, unexercised model/material paths, ordinary input, a complete
match/menu round trip, pixels, PCM, live timing and independent original
equivalence remain open. Browser action sweeps and renderer preparation have
only the explicitly recorded scope above. No source RNG, save state, input order or gameplay arithmetic
was changed.
