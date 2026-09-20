# Donkey Kong integration boundary

Donkey Kong's typed fighter data, all five costumes and bounded native move
lifetimes now execute on the full-game branch. A corrected cold/warm Final
Destination action sweep passes. **Platform shield drop still crashes** at an
undefined original animation-output boundary; the candidate is not complete.
Independent original comparison remains open.

## Source data and shared behavior

`CKIND_DONKEY` is 1 and `FTKIND_DONKEY` is 3. `PlDk.dat` supplies
`ftDataDonkey`, 337 serialized action rows and the exact `0x74` attribute
extension. The shared field schema preserves the source signed state/counter
words and float bits; C assertions check offsets, widths and types against
the pinned original declaration. `PlDkAJ.dat` supplies the animation clips.
The five costumes are `PlDkNr/Bk/Re/Bu/Gr.dat`. The effect owner uses
`EfDkData.dat`, bank 8, seven authored entries, and English audio uses
`audio/us/dk.ssm`. The source `x48` Article table is null.

The dynamics descriptor has one active body, six chain parameters and one
authored mode with cutoff 1. These are distinct counts. The adapter derives
the mode extent from all source blend selectors and checks cutoffs against
the actual chain length. The original OnLoad still reads the three heavy-walk
animation lengths and copies the resulting attributes.

Native fighter animation hydration now retains original `HSD_A_J_BRANCH`
visibility tracks as well as `HSD_A_J_NODE`. The original JObj callback owns
their different subtree/single-node behavior. The generic inspection pose
bridge still rejects those channels. All packed streams remain structurally
checked. Well-formed singleton streams can be retained for native playback;
the existing FObj guard stops before any reached undefined interpolation
output. This is not proof that every action or seek is executable.

In particular, Pass animation 209 and StopCeil animation 214 each contain a
singleton SPL0 branch track whose delay equals its clip end (25 and 8).
The original non-loop AObj interprets that endpoint before stopping. Synthetic
original-consumer tests retain the explicit failure at both endpoints; no
fallback value or skipped callback was added. The actual Pass lifetime now
reproduces this failure on Battlefield. StopCeil remains unexercised. The
existing terminal-CON compatibility deviation remains documented in [original comparison](ORIGINAL_COMPARISON.md).

The neutral costume's texture-animation table contains C8 and CMPR images
with synchronized TIMG/TCLT tracks. The original TObj renderer consumes a
selected TLUT only for indexed images. The shared material adapter must keep
every authored palette descriptor and validate its storage and selection,
while applying texel-index capacity checks only to indexed images.

Cargo also requires the carried fighter's common Shouldered and ThrownF*
command rows 267–275. These graphs are now decoded for each possible victim;
the original source callbacks continue to own capture, carry, walking and
release. No new opcode behavior was added.

## Evidence and retained failures

Both development and public Release builds pass. The full suite passes 1,080
tests in 358.963 seconds with 41 explicit skips. An earlier full-suite run
retains one stale menu fixture that expected Donkey to be unavailable; the
corrected check validates his final costume index and still rejects invalid
selections. Fresh shared headless regressions match 603 Ganondorf/FD and 240
Mario/FD declared source updates. These are not Donkey original comparisons.
The [checkpoint receipt](evidence/full-game-checkpoint-donkey-v1.json) records
the exact build hashes, scoped browser measurements, skips and known failures.

The focused fighter decoder checks pass in
`work/full-game/donkey-focused-decoders-v3.log`; the manifest/server checks
pass in `work/full-game/donkey-manifest-v1.log`. Action loading passes in
`work/full-game/donkey-animation-action-v2.log`, whose separate native guard
subtest retained an allocator-initialization failure. The corrected guard
checks pass in `work/full-game/donkey-native-output-guard-v4.log`, including
the two endpoint failures above. The branch visibility and delayed CON
consumer checks pass in `work/full-game/donkey-native-visibility-v3.log`.

Native entry attempts v1–v5 are retained under
`work/full-game/donkey-native-entry-v*.log`: the first boundaries were branch
visibility channel 12, a delayed singleton CON, a singleton SPL0, and finally
the mixed indexed/CMPR material-animation assumption. They are failed entry
attempts, not gameplay evidence.

The corrected [entry run](../work/full-game/donkey-native-entry-v6.log)
passes all five costumes, Ready/Go, pause, No Contest and repeated teardown.
The [focused animation/material suite](../work/full-game/donkey-animation-material-v3.log)
passes nine tests, including mixed image formats, malformed palette storage,
out-of-range palette selection and indexed-image overflow.

The [forward native run](../work/full-game/donkey-native-actions-v4.log)
passes grounded Giant Punch damage in all five costumes. Its first lifetime
also observes aerial full Giant Punch, ground/air Headbutt and Spinning Kong,
ground Hand Slap, and raw-input grab → cargo wait → cargo walk → cargo throw.
The [both-orientation check](../work/full-game/donkey-native-lifecycle-v1.log)
passes both player orders and the shared action store. Reverse orientation
is Mario combat against Donkey, not Donkey special-move coverage.

Move runs v1/v2 retain a test-recipe failure: the source preserved a fully
charged Giant Punch and correctly entered air state 378, while the test
expected only partial punch state 377. The corrected fixture accepts the
source-selected variant without changing charge history. Run v3 retains the
cargo crash caused by the victim's guarded ShoulderedWait command row; run
v4 follows the corrected common command ownership through source teardown.

The first cold browser discovery completes all 33 declared action cases and
7,000 source frames through original CSS/SSS on Final Destination. All eleven
Donkey-specific cases pass, including partial/full/cancel Giant Punch variants,
ground/air Headbutt and Spinning Kong, and Hand Slap. Its measured action window
fails with three native callbacks over 33.3 ms, three browser gaps, 235 audio
underrun frames, seven live pipelines and one diagnostic timing resume. Worst
native/browser intervals are 237.155/248.355 ms. Retain
`work/full-game/donkey-browser-discovery-v1/cold/report.json` as failed
performance evidence; all-case completion does not convert it into a pass.

The reviewed export adds 15 portable pipeline descriptors and preserves all
795 prior shader/pipeline records without payload conflicts. The new seed has
one shader and 809 pipelines, 3,366,912 bytes, SHA-256
`5d8a4b2bff7dcf12da67ffb576895fbf786f0898e8df04e2c5d2393027b2650b`.
The [fresh corrected browser pair](../work/full-game/donkey-browser-discovery-v2/report.json)
passes all 33 cases and 7,000 frames in each round. Cold/warm native maxima
are 8.265/8.610 ms; browser maxima are 23.005/23.095 ms. Both action windows
have zero deadline misses, hard gaps/long tasks, audio underruns, live
pipelines, timing resumes, focus losses and Wasm heap growth. The machine is
Apple M4/macOS 26.6.2/Chrome 153.0.8010.50, 640×480 at DPR1, development audio.
This is scoped FD action-window evidence, separate from startup and broader
fighter/stage/interaction admission.

The [Battlefield platform probe](../work/full-game/donkey-platform-pass-v2.log)
uses the normal P2 spawn and raw shield/down input. The authored shield-drop
range lies between `.66` and `.70`; raw Y=-54 enters Pass motion 244. Neutral
input then reaches frame 25 with branch channel 12, SPL0 opcode, interpolation
NONE, zero duration and the entire three-byte stream consumed. The FObj guard
aborts before undefined callback output. The earlier v1 probe used full down
and failed to reach Pass. These failures are retained; the passing FD sweeps
do not cover platform drops. An independent original consumer capture is the
next discriminating check, before any compatibility rule is considered.

The first independent capture attempt is explicitly unsuccessful:
`work/full-game/donkey-pass-original-consumer-v1/reference-v2.jsonl` records
Mario versus Mr. Game & Watch (FT kind 24), not Donkey. Its 40 frames remain
inside the locked intro, and it has no FObj consumer observations. The setup
confused CharacterKind with FighterKind and used a copied menu receipt. Those
artifacts cannot justify an animation fallback or close the reproduced crash;
a correctly observed Donkey selection and post-Ready capture are still needed.

Two further attempts retained the correct FT kinds (Mario 0 / Donkey 3) on
Battlefield, but still did not observe Pass. Attempt v2 captured 120 intro
frames with `match_frame=0`; its L/down edge was before Ready. The v3 plan
moved that edge to tick 140 and corrected the FObj function-entry breakpoint,
but the bounded run ended after indices 0–120, again before an advancing match
frame or any consumer probe. Their collector completion/status fields do not
establish the requested scenario. Raw data and receipts remain under
`work/full-game/donkey-pass-original-consumer-v2/` and `-v3/`. No compatibility
fallback is justified by these captures, and the reproduced crash remains open.

The requested-file manifest contains 169 FST paths totaling 122,864,276 bytes,
leaving 11,353,452 bytes under the existing 128 MiB cap. Generated font and DSP
coefficient ranges are outside this FST-only sum. See
`work/full-game/donkey-disc-budget-v1.json` and the broader
[scene asset boundary](FULL_GAME_PORT.md#asset-residency-boundary).

All move interactions, complete matches, original state/pixel/PCM comparison,
physical input and performance admission remain open. The deployed public
alpha is unchanged.
