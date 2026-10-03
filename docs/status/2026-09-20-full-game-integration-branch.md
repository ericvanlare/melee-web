# Full-game integration branch

`codex/full-game-integration` tracks the remaining offline vanilla game through
[a versioned feature and source inventory](../FULL_GAME_PORT.md). The report
retains all source functions and missing features; it does not turn compilation
or declared feature status into an accepted-game percentage.

The development player now imports scene-specific assets from one validated
local disc session, closing source owners and releasing outgoing files before
loading the next scene. A real browser check completes Mario/FD four-stock
ending, Bowser/Mario Fountain pause and No Contest, both returns to original
CSS, unload and restart. Delayed handoffs keep source steps stopped and retain
zero outgoing file bytes. Menus use 18,452,982 native input bytes; Mario/FD uses
19,077,674 and Bowser/Fountain 21,401,641. Unload clears all imported files.
Wasm capacity still grows across the sequence; this is functional lifecycle
evidence, not a memory plateau or performance pass. A fresh drawn Mario/FD
replay matches all 240 declared updates and PAD-history fields against the
independent original v2 pair. The earlier missing Bowser voice-bank unload
crash and two driver/recipe failures remain retained; see
[scene asset ownership](../SCENE_ASSET_LOADING.md).
The public player now uses the same scoped import while excluding DSP
coefficients. Four-player residency and broader mode/lifetime checks remain open.
New additions are paused for a production checkpoint with sixteen public
fighters and seven stages. Donkey is re-enabled in public selection after the
platform shield-drop repair; the remaining
[issue #50](https://github.com/ericvanlare/melee-web/issues/50) verification
stays open.
Both Release builds pass. The 1,096-test run took 366.767 seconds with
41 skips and one stale artifact-count assertion; correcting that test yields
four passing checks in the affected module. The original failed suite log is
retained. Commit `3103b65` subsequently passed full GitHub verification in
6m 04s across all jobs. See the
[scene-loading checkpoint receipt](../evidence/full-game-checkpoint-asset-scope-v1.json).
The refreshed legacy native transition comparison fails on a rumble flag and
match-entry RNG; a HEAD-derived driver reproduces the same event rows with
current libraries. Its setup/source reconciliation remains open separately;
see [the retained transition limitation](../TRANSITION_EQUIVALENCE.md#september-20-refresh-limitation).

Ganondorf is the first added development candidate. Fresh native checks pass
both player orientations and all five costumes on Final Destination, including
original combat, pause and teardown. The browser reaches original CSS/SSS and
advancing Ganondorf gameplay. A reviewed 37-pipeline preload correction clears
the 30-case, 5,200-frame drawn sweep with no hard timing/audio/pipeline failures;
earlier cold failures remain retained. A following cold/warm pair passes all
10,400 source frames with the same zero-failure counters at 640×480, DPR 1.
An independently repeated original pair now matches all 603 declared native
state updates after binding the actual save unlock profile. The legacy unbound
recipe failure and headless camera mismatch remain retained; drawn comparison,
complete ending and broader acceptance are open. See [Ganondorf scope and remaining gates](../content/GANONDORF_PORT_NOTES.md).
Captain Falcon now passes native source lifecycles in both Captain/Mario player
orders and all six costumes on Final Destination, including specials, stock
loss/respawn, pause and repeated teardown. The integration preserves his
original English costume resolution, indexed vertex colors and particle palette
metadata. The browser completes all 30 action cases over 5,200 drawn frames;
the first diagnostic run fails timing/audio/pipeline gates. After a reviewed
35-descriptor preload correction, both cold/warm action sweeps pass all 10,400
frames with zero hard failures. The first original CPU9 source/browser comparison
has an exact Falcon-position divergence, and the human-input browser replay
diverges at Results routing. No page/process crash was reproduced, so the user
report remains unattributed; device/browser-specific checks are unrun. See the
[investigation receipt](../evidence/captain-falcon-correctness-investigation-v1.json)
and [Captain Falcon's measured scope](../content/CAPTAIN_PORT_NOTES.md).
Hyrule Temple passes two native stage-owner lifetimes, including scaled geometry,
original map callbacks, light identity overrides, music candidates and teardown.
Two original Ready/pause/No Contest match lifetimes also pass. The browser
reaches advancing Temple gameplay, retaining an entry timing failure; a reviewed
16-descriptor preload correction clears both cold/warm entry reruns without
timing resumes. A cold startup long task remains visible, and the full action
matrix and original comparison remain open; see
[Temple evidence](../content/HYRULE_TEMPLE_PORT_NOTES.md).
Luigi now passes native source lifecycles in both player orders and all four
authored costumes. The selected-player fixture observes ground/aerial Fireball
Articles, special-move entries, pause and repeated teardown. All 30 browser
action cases pass over 5,200 drawn frames, retaining timing, audio and four
live-pipeline failures. A reviewed six-descriptor preload update clears both
5,200-frame cold/warm sweeps with zero hard failures. A fresh pair after the
shared stage-light lifecycle correction also passes all 10,400 frames with
zero hard failures; see [Luigi's evidence](../content/LUIGI_PORT_NOTES.md).
Fountain of Dreams now reaches original map, reflection and star construction.
Its shared changes preserve animated-light storage, exact reflection image
identity and multiple map objects sharing one camera. Two 7,200-tick native scheduler
and teardown lifetimes now pass, including moving collision and animated
lights. Cold/warm browser entry now completes Ready/Go and 30 gameplay frames
after a reviewed 28-pipeline preload correction. The cold run retains two
browser long tasks; this is a functional-entry result, not a performance pass. See
[Fountain's scope](../content/FOUNTAIN_OF_DREAMS_PORT_NOTES.md).
Pikachu and Pichu are now enabled as development candidates, bringing the
branch to thirteen fighters. Native fixtures pass both player orders and all
four family costumes, including source jolt/Thunder creation and teardown,
Pichu self-damage, pause and No Contest. Both browser discovery sweeps complete
30 action cases over 5,600 frames; their failed cold timing/pipeline gates
remain retained. A reviewed preload correction clears all four fresh cold/warm
sweeps: 22,400 source frames with no hard failures, native target misses or
heap growth. Shared fixes preserve complete-null effect
rows and the signed self-damage command. See
[their measured scope](../content/PIKACHU_PICHU_PORT_NOTES.md).
Yoshi's Island 64 is the seventh development stage. Its native owner passes two
5,000-tick source scheduler lifetimes, including cloud collapse/collision
removal/reappearance, guest selection and teardown. Five Mario/Mario entry,
pause and No Contest lifetimes also pass. The original browser menu route
reaches advancing gameplay, retaining a cold entry stall and one diagnostic
resume. Fresh cold/warm entry passes after the preload correction without
resumes, gaps or audio underruns; one 69 ms cold browser long task remains.
This is functional entry, not a stage-performance pass.
The shared material loader now preserves the source base TLUT for
TIMG-only animation; see [stage evidence](../content/YOSHIS_ISLAND_64_PORT_NOTES.md).
Jigglypuff is the fourteenth development fighter. Native checks cover all five
costume lifetimes, including original hat archives and dynamics, crouch
animation variation, five aerial jumps and ground/air special states. The
original browser CSS/SSS route passes 31 action cases in each cold/warm run:
12,800 source frames with no action-window timing, audio, pipeline or heap-growth
failures. Shared fixes retain the complete stored dynamics table separately
from its active body count, the custom-part owner/cache and original crouch
Wait selection. Earlier loader, blue-hat and input-recipe failures remain
retained; see [Jigglypuff's measured scope](../content/JIGGLYPUFF_PORT_NOTES.md).
The Jigglypuff checkpoint passes both Release builds and the 1,077-test suite
in 346.034 seconds with 41 explicit skips. Fresh shared headless comparisons
still match 603 Ganondorf/FD and 240 Mario/FD declared source updates. Its
pipeline seed is unchanged; see the [Jigglypuff checkpoint receipt](../evidence/full-game-checkpoint-purin-v1.json).
Donkey Kong is the fifteenth development candidate. Native checks pass both
player orders and all five costumes, with first-lifetime ground/air specials
and raw cargo grab/walk/throw. Shared fixes preserve branch visibility,
source dynamics modes, mixed indexed/compressed texture animations and the
carried fighter's original command graphs. Both browser discovery rounds
complete 33 action cases and 7,000 frames; cold fails timing/audio/pipeline
gates while warm passes. After a reviewed 15-descriptor preload correction,
both fresh rounds pass all 14,000 action frames with zero hard failures.
A separate Battlefield shield-drop probe exposed a real crash at the
undefined terminal SPL0 animation output. That boundary is now repaired: the
host defines the terminal single-datum output as the authored last value (the
documented FD terminal-CON rule extended to the whole class), and the retained
shield-drop reducer completes Pass motion 244 on the upper platform for all
five costumes with pause and repeated teardown. Donkey is re-enabled in public
character selection. Independent original consumer capture, ceiling lifetimes,
original comparison and broader acceptance remain open; see
[Donkey Kong's measured scope](../content/DONKEY_KONG_PORT_NOTES.md) and
[issue #50](https://github.com/ericvanlare/melee-web/issues/50).
The Donkey checkpoint passes both Release builds and the full 1,080-test suite
in 358.963 seconds with 41 explicit skips. Fresh shared headless comparisons
still match 603 Ganondorf/FD and 240 Mario/FD declared source updates. The
receipt retains the failed cold discovery, the corrected pair, and the separate
unresolved shield-drop crash; see the
[current checkpoint receipt](../evidence/full-game-checkpoint-donkey-v1.json).
Bowser is the sixteenth development fighter. Native Final Destination checks
pass both player orders and all four costumes, including Flame creation and
cleanup, ground/air Fortress and Bomb, and real ground/air capture and throws.
The shared boundary now admits the victim's original command graphs and the
source visibility cleanup command. Both browser discovery runs finish all
30 cases; the cold timing/audio/pipeline failures remain retained. A reviewed
36-descriptor preload correction clears the fresh cold/warm pair: 11,201
source frames with zero action-window timing, audio, pipeline or heap-growth
failures and no entry timing resumes. Independent original Bowser comparison,
drawn capture interactions and broader acceptance remain open; see
[Bowser's scope and evidence](../content/BOWSER_PORT_NOTES.md).
The Bowser checkpoint passes both Release builds and 1,083 tests in 357.995
seconds with 41 explicit skips. Fresh shared headless comparisons still
match all 603 Ganondorf/FD and 240 Mario/FD declared source updates. The
[Bowser checkpoint receipt](../evidence/full-game-checkpoint-koopa-v1.json)
binds the builds, browser pair, review and retained failures. The Bowser
checkpoint's full GitHub Verify completed in 6m 16s across all jobs;
the preceding Donkey checkpoint completed in 6m 20s.
Mewtwo is the seventeenth development fighter on `codex/mewtwo-integration`.
Native Final Destination checks pass both player orders and all four
costumes, including ground and aerial Shadow Ball charge with fresh-B-edge
release and Article teardown, Teleport, Confusion and Disable lifetimes. The
source contract preserves the 0x88 `ftMewtwoAttributes` extension, the
two-slot Article table (Disable 0x6e, Shadow Ball 0x70 with twelve
serialized special words and ten animation rows), effect bank 13 with four
static rows and the authored four-TObj costume texture map: the match-stats
eye telemetry now requires the runtime collection to match each fighter's
authored texture map while keeping the two declared recorded slots and all
existing comparison fields. The versioned `mewtwo-visible-actions-v1`
inventory has 32 cases; a fresh-origin cold export supplied 12 new portable
pipeline descriptors (zero payload conflicts, all 846 previous records
preserved), producing the reviewed seed of one shader and 857 pipelines
(SHA-256 `f85858ff99f368d38ca16fa18727a12f778d57543e2898d29507f92585db2c2b`).
After that preload correction both fresh cold/warm rounds pass all 32 cases
over 6,400 frames with zero hard-gate failures, native/browser maxima
6.645/24.720 ms cold and 5.920/21.710 ms warm, and zero timing resumes or
heap growth. The local 1,127-test suite passes in 278.909 seconds with 60
documented optional skips. Independent original comparison, pixels, PCM,
physical controllers, complete matches and broader performance remain open;
the failed v5 cold discovery and its provenance, review and passing pair are
retained under `work/full-game/`. See
[Mewtwo's scope and evidence](../MEWTWO_PORT_NOTES.md) and the
[current checkpoint receipt](../evidence/full-game-checkpoint-mewtwo-v1.json).
After merging main (Donkey's re-enablement and the Captain Falcon dive
catch), an independent review corrected the telemetry bounds to the original
five-slot collector and the merged tree re-passes: 1,131 tests OK in final
suite form with 57 documented skips, both native orientations, and a fresh
cold/warm browser pair (all 32 cases, native/browser maxima 7.625/23.265 ms
cold and 6.910/25.460 ms warm) with zero hard-gate failures.
The preceding Pikachu/Pichu/Old Yoshi checkpoint passes both Release builds and 1,075
tests in 334.490 seconds with 41 explicit skips. Fresh headless comparisons
still match 603 Ganondorf/FD and 240 Mario/FD declared source updates; the
new content has no independent original comparison yet. See the
[Pikachu/Pichu/Old Yoshi receipt](../evidence/full-game-checkpoint-pikachu-old-yoshi-v1.json).
The preceding Luigi/Fountain checkpoint passed both Release builds and 1,070 tests
in 298.028 seconds with 41 explicit skips. The receipt names the exact browser
build, the later Article validation guard, reference scope and retained failures;
see [checkpoint validation](../evidence/full-game-checkpoint-luigi-fountain-v1.json).
Results PR #44 and audio PR #42 remain separate draft dependencies. This branch
work has not changed the deployed public alpha.
