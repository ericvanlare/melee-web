# Jigglypuff development integration

Jigglypuff's source loader, five costumes and original menu entry now execute
on the integration branch. Focused native move checks pass. This is a
development candidate; independent original comparison, complete interaction
coverage and browser performance admission remain open.

## Source and ownership boundary

`CKIND_PURIN` and `FTKIND_PURIN` are both 15, with 327 source action rows.
`PlPr.dat` supplies `ftDataPurin`; `PlPrAJ.dat` supplies its animation clips.
The five model archives are `PlPrNr.dat`, `PlPrRe.dat`, `PlPrBu.dat`,
`PlPrGr.dat` and `PlPrYe.dat`. The neutral model is distinct from the fighter
data archive. The effect owner uses `EfPrData.dat`, bank 11, one entry, and
the English audio owner selects `audio/us/purin.ssm`.

The shared field table in `src/gameplay_purin_schema.h` retains the exact
`0x100` attribute layout, source signed integers, float bits, vector order,
opaque words and padding bytes. C compilation checks the source offsets,
widths and types. The source initializer still owns multijump capability.

The `x48` root is a custom-part table, not an Article table. Its first slot
is null; its second holds the original `FtPartsDesc` wrapper and five-costume
visibility table. Each colored costume publishes its original optional hat
symbol through a typed archive handle. All four visibility categories are
checked against that hat's actual DObj occurrences and source capacity.
The neutral costume has no hat archive.

The original six-slot hat descriptor cache is exchanged when the fighter-kind
asset scope begins and restored after every Fighter has been removed. The
archive handle, descriptor graph and visibility storage outlive those
Fighters. Source `OnUserDataRemove` retains responsibility for live hat
teardown; `OnDeath` only changes visibility.

The source dynamics table stores five rows while its initial active body
count is one. Blue hats select stored rows 1/2 and green hats select 3/4.
The decoder derives the stored table extent from authored references,
hydrates all rows and preserves the active count. Bone indices and child
chain lengths are checked against the selected hat graph before publication.
Invalid initial body counts are rejected. Treating the active count as the
stored table length caused the retained blue-costume crash.

The source crouch Wait table at `x28` retains `(31,80)`, `(32,20)` and the
`(-1,-1)` sentinel. Its original RNG consumer and weighted selection are
unchanged. Both variants receive checked action graphs. Common item commands
42/45 remain explicitly guarded; this slice does not admit the parasol or
sword item consumers.

## Evidence and retained failures

The [focused decoder and ownership run](../work/full-game/purin-focused-decoders-v2.log)
passes seven tests, including real archive values, malformed active dynamics
counts, crouch table bounds, custom-part visibility, archive publication and
source cache restoration.

The [native entry run](../work/full-game/purin-native-entry-v3.log) passes
all five costumes, Ready, pause, No Contest and repeated teardown. The
[reverse native run](../work/full-game/purin-native-reverse-v1.log) also
passes every costume while Mario actively exercises combat against
Jigglypuff; it is not Jigglypuff special-move coverage.
The [forward native move run](../work/full-game/purin-native-actions-v4.log)
observes crouch animation 32, all five authored aerial jump states,
ground/aerial Rollout charge and release, and ground/aerial Pound, Sing and
Rest. Every costume then completes pause, No Contest and teardown. Specials
are actively exercised in the first costume lifetime; the other four check
construction and lifetime, not a repeated full move matrix.
The earlier [entry v1](../work/full-game/purin-native-entry-v1.log) fails
at the unresolved crouch table, and [entry v2](../work/full-game/purin-native-entry-v2.log)
retains the blue-hat dynamics crash before the stored-row correction.

The first [cold/warm browser discovery](../work/full-game/purin-browser-discovery-v1/report.json)
reaches gameplay through original CSS/SSS and passes 22 common action cases.
Both runs fail the five-jump input recipe, which observes only the first
three authored aerial jumps. Source inspection identifies a held-button
animation gate that brief fixed pulses can miss. The failure remains
retained while the input recipe is reduced; it is not a passing full sweep.
The corrected held-button recipe reaches all five jumps natively. Native
actions [v1](../work/full-game/purin-native-actions-v1.log),
[v2](../work/full-game/purin-native-actions-v2.log) and
[v3](../work/full-game/purin-native-actions-v3.log) retain an aerial Rollout
fixture failure: waiting for the fifth jump animation to finish loses the
altitude needed to charge. The passing v4 presses B while that jump is still
rising, as the original IASA permits, and releases only after observing the
air full-charge state. These corrections change test input, not gameplay.

The corrected [cold/warm browser pair](../work/full-game/purin-browser-discovery-v2/report.json)
passes all 31 declared cases and 6,400 source frames per run. Cold/warm
native maxima are 8.015/8.510 ms; browser maxima are 26.185/25.690 ms.
Both measured action windows have zero native target misses, browser gaps
or long tasks, audio underruns, live pipelines, timing resumes, focus losses
and Wasm heap growth. The declared machine is Apple M4/macOS 26.6.2/
Chrome 153.0.8010.50, 640×480 at DPR 1 with development audio enabled.
The browser recipe observes the aerial Rollout start/loop and later release;
the native fixture separately observes full charge while still airborne.
These windows do not admit startup, other costumes/stages or full matches.

The [read-only cache comparison](../work/full-game/purin-cache-review-v1.json)
finds no new descriptors, missing base rows or payload conflicts. The reviewed
795-row seed is unchanged. The [disc manifest budget](../work/full-game/purin-disc-budget-v1.json)
contains 160 files totaling 117,611,596 bytes, below the existing 128 MiB cap;
generated DOL/font ranges are outside that FST-only sum.

Fresh shared headless regressions retain declared state agreement for
[603 Ganondorf updates](../work/full-game/ganondorf-native-purin-v1/refresh-receipt.json)
and [240 Mario updates](../work/full-game/mario-fd-native-purin-v1/refresh-receipt.json).
These do not compare Jigglypuff, source drawing, complete match endings,
pixels, PCM or live timing.

Remaining gates include broader authored move variants and interactions,
other fighter/stage combinations, complete matches, original state/pixel/PCM
comparison and physical input. The deployed public alpha is unchanged.

Both Release builds and the full suite pass; exact build hashes, test counts,
skip reasons and retained failures are in the
[checkpoint receipt](evidence/full-game-checkpoint-purin-v1.json).
