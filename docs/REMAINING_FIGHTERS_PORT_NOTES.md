# Remaining fighter port notes (PR #86)

## Disposition

These six selectable characters (seven source forms including Zelda/Sheik) are
an unfinished development candidate, not admitted or fully playable. Real-asset
construction, targeted distinctive actions, two rendered four-CPU9 match loops
per requested lineup, and repeatable original references are now available.
The browser matches are functional rendered evidence, not original equivalence.
The historical whole-session replays stopped at scene mismatches; their harness
did not establish the earliest gameplay-state divergence. The new exact
all-entity prefix comparison does, within its bounded scope. The Samus-donor palette now resolves through a checked
cross-archive source-address map; particle-pixel use remains unverified.

Evidence labels follow the
[performance/accuracy playbook](PERFORMANCE_AND_ACCURACY.md). Raw disc data,
extracts, captures, screenshots and run logs remain local and ignored under
`work/` / `assets-local/`; this note contains no personal input paths.

## Baseline and evidence provenance

The pre-reconciliation source checkpoint is
`55f1b7e6c9a0b96b253271d1561aeb7c6225feb5`. It contains the character fixes and
final-suite corrections described below. The retained browser/action runs were
produced during that implementation, before integration of main merge
`aecc3e64def3090bdfc33d0eb66058df998ef14d` (PR #89). The older browser reports do
not record an exact source commit or runtime hash; they must not be retroactively
claimed as clean-checkout validation of `55f1b7e` or the reconciled branch.
The legacy results below are historical until explicitly replaced by a fresh,
commit-and-build-bound reconciliation receipt. Original capture repeatability
remains independently bound to the preserved capture/recipe identities.

### Reconciled baseline (in progress)

Frozen runtime checkpoint `2943d8f` now passes the owned-input full suite:
1,545 tests, 73 explicit skips, zero failures in 557.565 seconds
(`work/pr86-reconciled-full-suite-r1.log`, exit 0). Its A scenario passes two
natural matches at 13,218/14,292 source frames, rendered Results at 182/184
frames and both original CSS returns
(`work/pr86-final-frozen-a-two-match-r1/report.json`). Source documentation
changed during that run, but the retained served-artifact hashes identify the
unchanged frozen runtime. This is rendered functional evidence, not timing.

B's longer original-input replay on the same runtime stops at input 28,298:
original Results versus port Match during the second match. The exact state
comparison, not that downstream scene difference, identifies the first
observable mismatch: second-match tick 2395, source sequence 80313, port input
16561. Yoshi is the captured victim in `CapturePulledLw` (motion 226), while
Samus is in `CatchDashPull` (215). Only the victim's Z position first differs:
original `c010e3fa`, port `c010e3f8`. All 12,095 captured first-match rows agree,
as do the preceding 2,395 second-match rows
(`work/pr86-final-b-state-match{0,1}-r1.json`). These remain bounded all-entity
state comparisons, with CPU decisions recomputed and port draws uncaptured.
The failing replay tears down all source owners cleanly. A's longer replay
completes all three scene/return loops, with 12,040/14,366 exact first/second
match rows. Its third match first differs at tick 6099, source sequence 173013,
input 35555: Popo in screen-KO motion 7, X `4185222b` original versus
`4185222a` port (`work/pr86-final-a-state-match{0,1,2}-r1.json`). This is why
passing scene traversal is not called state equivalence.

The owned-DOL Samus reduction proves missing fused chain-placement and gravity
operations, with 531 of 1,024 synthetic production-function cases failing
before and all passing after `10bb09d`. The standalone normalizer's original
unfused sum and source RNG/collision-history order are unchanged. The broader
owner's separately committed camera-shake depth-scaling repair is adopted as
`ae57018`; its source-function and owned-DOL checks pass. The new frozen browser
checks establish their effect at the captured boundaries. B's second-match
prefix now agrees for all 2,464 observed ticks, including former tick 2395
(`work/pr86-samus-shake-b-state-match1-r1.json`). A completes the entire
42,492-input recipe and all three scene/return loops. Its separate exact
comparisons agree on all 12,040/14,366/12,626 captured gameplay rows in those
matches, including former third-match tick 6099
(`work/pr86-shake-a-state-match{0,1,2}-r1.json`). These are bounded declared-field
state results, still `complete=false`: CPU decisions are recomputed, CPU blocks
and port draws are not compared, and no pixel/PCM/cadence claim follows.

B's extended replay still reaches the downstream input-28298 scene mismatch.
The new first difference is much earlier: second-match tick 6718, sequence
101920, input 20884. Yoshi enters CatchPull with animation frame 44 in the
original versus 42 in the port; the other declared fields agree. The complete
first match remains exact (12,095 rows), as do 6,718 second-match rows
(`work/pr86-samus-shake-b-full-state-match{0,1}-r1.json`). The reduced native
and portable decoder checks both fail before repair: Yoshi's authored `x12C`
grab-frame byte table was mislabeled as three scalar words, reversing each
four-byte group. It is now decoded as the original twelve bytes; all entries
and both focused checks pass (`work/pr86-yoshi-grab-bytes-{before-r2,after-r1}.log`).
Fresh rendered causal validation agrees on all 6,777 observed second-match
ticks, clearing the former tick-6718 mismatch
(`work/pr86-yoshi-grab-b-state-match1-r1.json`). The full 45,226-input recipe
then completes all three natural match/Results/CSS loops on unchanged frozen
bytes (`work/pr86-yoshi-grab-b-replay-r1/report.json`). Exact comparison still
finds a later difference: second-match tick 8353, sequence 110091, input 22519,
Samus in GuardSetOff, X `4145fcc5` original versus `4145fcc6` port. The third
match's first difference is also Samus GuardSetOff, tick 12673, X `bfe3cbb6`
versus `bfe3cbb4`. The first match's 12,095 rows remain exact; the second and
third agree for 8,353 and 12,673 rows before their respective differences
(`work/pr86-yoshi-grab-b-full-state-match{0,1,2}-r1.json`).

The reduced actual shield-SDI callback reproduces the exact second-match X
error. Original slices retain floor normal Y `3f7fffff`; the owned executable
uses two rounded scale products followed by fused X/Y position updates in
both shield SDI callbacks. Explicit `fmaf` restores those writes without
changing the input gates or the scale-product order. Before repair the observed
operand case and 249 additional synthetic cases fail; afterward the reduced
case, all 1,024 synthetic cases, and both owned-DOL instruction profiles pass
(`work/pr86-shield-sdi-{before-r1,after-r2}.log`). An intermediate patch-format
preparation error is retained in `after-r1`, not called a runtime result.
Fresh causal browser validation is pending. The separate functional Results
failure below remains open.

An additional B functional run reaches natural Results after 11,725 gameplay
frames with Sheik winning, then fails the camera-pool ownership guard at
Results frame 560 on return to CSS
(`work/pr86-final-frozen-b-two-match-r1/report.json`). This is an unresolved
runtime failure, not a comparison-only limitation. The new four-CPU,
Sheik-winner native Results confirmation/close control passes
(`work/pr86-sheik-results-repro-r1.log`), so the investigation continues at the
rendered/mode-exit ownership boundary. Neither a Sheik-specific cause nor PR
readiness is claimed.

Native controls for both external identity pairs `{18,7}` and `{19,7}` preserve
the camera pool through the actual VS mode-exit callback. The smaller rendered
Results/full-host fixture also passes 384 frames, confirmation, host route
commit and teardown (`work/pr86-results-rendered-host-r3/report.json`). Those
synthetic standings do not reproduce the natural failure. New fail-fast guards
retain the initial pool independently and identify the first changed tick,
draw, host/close or destructor boundary without correcting the value. A
fault-injection component regression fails before and passes afterward; it
proves guard coverage, not a gameplay fix. The focused three-test ownership and
return-handoff check passes (`work/pr86-results-guard-regressions-r1.log`). A
fresh natural B run passes both matches (14,661/13,431 source frames), rendered
Results (185/184 frames) and both original CSS returns, with no page/native
errors or timing interruptions (`work/pr86-results-guard-b-two-match-r1/report.json`).
Both winners are Samus, so this does not clear the historical Sheik-winner
failure. A reduced Stock Battle control also passes 384 Results ticks, mode
exit and teardown (`work/pr86-results-stock-control-r1.log`); previous controls
used Time Battle. A development-only, read-only Results entry packet now retains
the actual typed terminal/Results payload, entry seed and existing pre-teardown
PAD snapshot, bound by the harness to served JS/Wasm hashes. It is an ABI-labelled
local debug snapshot, not a PPC image or exact replay. Three focused packet
tests pass (`work/pr86-results-packet-main-r1.log`). The retained first
rendered helper's shutdown assertion and second helper's missing-music
preparation failure remain failures, superseded only for that synthetic
fixture by the third run.

Frozen `facbb21` subsequently passes both requested natural two-match loops:
A 15,796/11,917 source frames, B 14,783/14,820, both Results→CSS returns per
lineup, zero page/native-command errors, two timing-interruption entries each
(`work/pr86-entry-packet-{a,b}-two-match-r1/report.json`). Both B winners are
Samus. Hash-bound entry packets and full-canvas Results screenshots are retained
and inspected. This still does not clear the earlier Sheik-winner failure.
Delayed synthetic controls advance 744 Results frames (past the historical
frame-560 failure), including rendered Zelda-origin `{ckind18,ftkind7}` and
external-Sheik `{19,7}` winners. Source ownership, host exit and teardown stay
valid, but those two overall browser reports **fail** their `trace.data`
network/body-retention assertion; they are not clean harness passes. Exact
commands, copied-library hashes and screenshots are in each retained
`work/pr86-results-*-stock-delayed-host-rendered-*/HANDOFF.md`.

The first correctly invoked shield-SDI causal replay stops before its target
at source cursor 10,628 with `Audio output queue overflow`; the worklet queue
had 15,510 frames after concurrent build/render load
(`work/pr86-shield-sdi-b-boundary-r2/report.json`). That is a retained runtime
failure, not a state-comparison pass or evidence attributing it to the new math.
An earlier `r1` invocation rejected a misspelled CLI option before launching.
The next causal attempt runs without concurrent builds or other browser cases;
simulation timing, input recipe and queue bounds remain unchanged.

Latest validation checkpoint: `d5695c9` includes the existing shared-runtime
owner's committed motion-flag, camera/inverse, reciprocal-square-root and
combo-push arithmetic fixes (`1ca4430`, `4fd8f9d`, `4f70b84`, `d5695c9`). It
passes 13 focused source/profile checks with the owned DOL, source-context and
collision traces, 23 ownership tests, and both real-asset Results cases
(`work/pr86-shared-math-*-r1.log`). After the startup/costume repairs below,
the fresh A comparison agrees exactly for 7,144 ticks (0–7143), including the
formerly failing tick 5606 (`work/pr86-candidate-a-state-compare-r1.json`).
This establishes that the imported arithmetic fixes clear that observed
boundary; the remaining session, port draw records and CPU blocks are still
uncompared. The prefix remains incomplete, with per-tick recorded PAD and
source CPU decisions recomputed.

The rendered A run on this checkpoint completes natural matches at 11,273 and
14,587 source frames, renders Results for 182/180 frames, and returns to CSS
after each (`work/pr86-shared-math-a-two-match-r1/report.json`). Page and native
command errors are empty; no timing interruptions occurred in this run. The
report retains served hashes and dirty test-source provenance. This supersedes
the A carry failures below for that runtime, but is not original comparison or
performance evidence. Fresh final-candidate A/B runs and the full suite remain
pending after the additional startup/costume fixes.

Expanded all-costume validation found two preparation defects missed by the
neutral-costume action cases. `245c4d4` restores the input snapshot before the
original startup player-selection routine, preserving the new deferred stage
ordering. Both held-A form directions now pass all five costumes. The reduced
ordering and ownership tests retain one-shot, world, RNG, camera, queue and
pre-Fighter guards. Separately, the Kirby copy adapter selected a body-costume
row for a joint-backed hat whose original loader always selects row zero:
Fox costume 1 followed unrelated data from `PlKbCpFx.dat` slot `0x240`, then
rejected unrelocated word `0x20080008` at `0xc06c` (data-relative offsets).
The source-family repair also follows the original per-category visibility and
texture-index fallback for parts-only hats. Its four reduced tests fail before
and pass after, including malformed consumed-pointer rejection and six real
donor archives (`work/pr86-kirby-copy-tests-{before,after}.log`). On `02b63aa`,
all seven forms pass all-costume entry and teardown in both orientations.
Fox's actual copied-move acquisition/use/loss/reacquisition passes all six
Kirby colors. The equivalent G&W test exposed a color-1 acquisition
crash in `HSD_DObjSetFlags` through `ftParts_8007487C` and
`ftKb_SpecialN_800F14B4` (`work/pr86-kirby-gw-colors-failure-r1.log`). This is
not waived by the passing construction or neutral donor checks. The reduced
pre-operation probe found authored DObj index 0 accessing an empty copied list:
the selected color's model/material cache was null. `2943d8f` imports the
selected source costume roots and calls the original selected-color loader,
then verifies all required cache pointers. Shared-filename rows remain distinct
cache slots; neutral publication order and malformed-data rejection are retained.
The rebuilt main trace now passes all six colors for both Fox and G&W copy
acquisition/use/loss/reacquisition/teardown
(`work/pr86-selected-copy-all-color-lifecycle-r1.log`). Five copy-owner tests and
ten browser manifest tests pass; the latter is checked against every authored
copy-model filename in the pinned source. All other focused character gates pass in
`work/pr86-costume-copy-character-gates-r1.log`.

The latest B rendered run on `02b63aa` completes natural matches at
12,744/13,363 source frames, Results at 185/180 frames and both CSS returns.
Both matches observe Zelda→Sheik→Zelda→Sheik, with zero page/native-command
errors and no timing interruptions (`work/pr86-candidate-b-two-match-r1/`).
Its separate reference comparison agrees exactly through tick 8780 (8,781
ticks; `work/pr86-candidate-b-state-compare-r1.json`), still an incomplete
per-tick state prefix. Final-candidate `2943d8f` is frozen in
`work/pr86-final-candidate-frozen`; longer replay, A's final functional loops
and the owned-input full suite are now running. These pending checks are not
credited as passes.

The following paragraphs retain the reconciliation chronology; a historical
failure or passing prefix is not a claim about a later build.

Main including PR #89 is merged at `3b548b1`. The merge preserves deferred
match fighter construction, live Fighter leases, logical source-stack and
executed register tracking, common-data loading order, inactive TEV descriptors,
Results leases, effect-bank reload and archive/collision cleanup. Two concrete
integration failures were fixed: Kirby's checked descriptor builder now calls
the original HSD parser directly rather than the source-file-only archive
entry; the native action fixture explicitly selects its supplied US locale.
Neither fix relaxes the source RuntimeFiles ownership checks.

Reconciliation then exposed three concrete Nana defects, each with a reduced
failing-before/passing-after regression: Player mapping consumers read beyond
the `PdPm.dat` string instead of their table owner (`2c2c16b`); delayed negative
stick values converted directly from float to unsigned byte (`0994ae9`); and
partner correction omitted the original double fused multiply-add (`762a21b`).
The owned-DOL tests bind both arithmetic fixes to the executed instructions.
The real-asset CPU9 lifecycle now observes Nana death → Sleep while Popo keeps
his stock → later Popo stock loss → paired Rebirth → Nana damage reset and
resumed gameplay, followed by teardown (`work/pr86-ice-cpu-lifecycle-r3.log`).
The prior acceptance of a permanently sleeping partner is withdrawn.

Fresh affected builds and source-context/collision/native/common/archive checks
pass, including six owned-profile checks and 24 ownership tests. Both real-asset
four-participant Results construction/teardown cases pass in
`work/pr86-final-results-{a,b}-r1.log`; the explicit ItCo Random Article check
passes in `work/pr86-final-random-article-r1.log`. These are component/native
checks, not original comparisons. The final full suite is still pending.

The version-2 observer retains primary and secondary entities. The exact
comparator (`e91cc7e`) first found Nana's motion/input mismatch at tick 105;
the signed-byte repair cleared it. The next one-bit Nana position mismatch at
tick 609 cleared after the double-FMA repair. The longer updated A comparison
now agrees for ticks 0–5605, then differs only in Popo's screen-KO X position:
source `42810a45`, port `42810a46`, source sequence 33740/tick 5606, port index
6980 (`work/pr86-nana-correction-a-state-compare-r2.json`). The original
`ftDrawCommon_80080E18` path writes that position through the inverse camera
matrix. The broader equivalence task is actively repairing that shared
camera-arithmetic boundary; the exact receipt was sent to its owner instead of
starting a duplicate investigation. B's bounded prefix agrees for all 3,786
available ticks (`work/pr86-rejoined-b-state-compare-r1.json`).

These compare exact RNG, match clock, PAD bytes and the declared primary and
secondary Fighter fields. Source CPU decisions are recomputed, not supplied
as inputs; raw CPU blocks and port draw state are not compared. Scheduling is
per-tick recorded PAD, not recorded-queue or live-cadence equivalence. Prefix
exhaustion remains incomplete/unverified, never a full-session pass.

Initial reconciled A/B two-match loops passed before these later gameplay
repairs (`work/pr86-reconciled-{a,b}-two-match-r1/`). The latest B run, built
from `a162781`, passes both natural matches (14,446 and 13,845 source frames),
both Results→CSS returns and active Zelda/Sheik switching. Its full-canvas
Results screenshots are retained at source frames 180/183; page/native-command
errors are zero and ten timing pause/resume entries are recorded
(`work/pr86-final-b-two-match-r1/report.json`). The latest A run on that same
runtime fails the explicit skipped-conversion carry guard at source frame
6,952 (`work/pr86-final-a-two-match-r1/report.json`). This remains unfinished
work, separate from the screen-KO float mismatch. Diagnostic checkpoint
`ea8ab4d` retains the last executed carry-clobber call site and complete DOM
logs without weakening rejection. Browser reports retain actual served hashes,
GPU checks and repository state; dirty test/document changes are not hidden.

The diagnostic rerun (`work/pr86-carry-diagnostic-a-r1/report.json`) completed
one natural A match, then identified the rejected carry specifically as Nana
(kind 11, slot 2, secondary entity 1) at match-two source frame 1865. It is not
the earlier item-draw or Samus CatchWait crash. `559d0c2` adds the owned-DOL
audited follower motion/null-item definitions with live leases and checked
return scope; unsupported item-address and bypass routes still reject. The
three actual-source/profile checks and source-context/collision traces pass.
The first rerun in `work/pr86-nana-carry-a-two-match-r1/` instead exposed a
new patch-composition regression: preceding hunks moved the ordinary B24B8
frame hook onto B1EF0's similar locals. Fox then rejected a carry at frame 695.
The isolated pre-change patch confirms the old placement was correct.
`baa72ee` anchors the hunk to its exact function name; the added prepared-source
placement check fails before and passes after. This failure remains retained,
not credited to the shared runtime task.

## Character action checklist

Construction results refer to real-asset source traces that execute source ticks
and teardown. Targeted action runs use costume zero unless noted. A passing
browser match supplements these checks; it does not prove each listed action.

The additional `--remaining-up-special` controller cases pass ground and air
entry, source continuation, return to grounded Wait without stock loss, and
teardown for G&W Fire, Kirby Final Cutter, Samus Screw Attack, Yoshi Egg Throw,
Zelda Farore's Wind and Sheik Vanish. The first three rising/teleport families
and Zelda/Sheik must advance upward; Yoshi is explicitly an Egg Throw check,
not a claim that it provides the same recovery behavior. G&W Rescue, Kirby
Cutter Beam and Yoshi thrown-Egg Articles must actually appear and retire.
The focused test and individual source-transition logs are retained in
`work/pr86-up-special-actions-r1.log` and `work/pr86-up-special-{3,4,16,17,18,19}-r1.log`.
These on-stage native cases supplement the checklist below. Off-stage/ledge
recovery, move-specific original comparison and broader action inventories
remain unverified; Ice Climbers' paired Belay case remains separate.

The added `--kirby-copy-ko` cases acquire and use Fox, G&W and Samus copies,
lose/reacquire them by up-appeal, then walk off Final Destination using ordinary
PAD. Each consumes exactly one stock, observes Rebirth and the original copy
reset, resumes grounded Wait, and passes repeated match teardown
(`work/pr86-kirby-ko-r1.log`, three donor subcases). These cover joint-hat,
copied-part and charge-Article families. KO-copy-loss for other donors and
damage-triggered random copy loss remain unverified.

| Character | Content/construction | Targeted action coverage | Still unverified / failed |
| --- | --- | --- | --- |
| Game & Watch | **Native traced** in mixed-content lifecycle and rendered in both A matches. Ten authored Articles pass the real visibility/ownership regression. | Chef source motion, sausage Article creation/lifetime/teardown. | Other specials, defense/recovery, damage/KO and broader action inventory. |
| Kirby | **Native traced** against Fox across all six costumes with repeated construction/teardown; rendered in historical A matches. Copied-part visibility counts/IDs and Game & Watch's secondary texture-animation pair are source-adapted and checked against live DObj visibility. | Six donors pass acquisition, copied use, up-appeal loss, same-donor reacquisition and teardown: Mario, Popo/Nana, G&W, Fox, Samus and Captain. The first five create their copied Articles; Captain's Falcon Punch enters the copied motion and damages its victim. A separate three-player case verifies true Mario→Fox replacement and use. The Ice observer identifies actual victim ownership across both entities; Nana maps to the Popo copy family. G&W additionally checks Chef/Pan ownership and visibility. | Nineteen donor families listed below remain unverified. Other normals/defense/recovery/damage/KO and copied-particle pixel equivalence remain open. |
| Ice Climbers (Popo/Nana) | **Native traced** as two distinct source entities with repeated lifecycle; rendered in historical A matches. | Belay separates the pair. The strengthened CPU9 case observes Nana death→Sleep while Popo retains his stock, later Popo stock loss, both entities in Rebirth, Nana damage reset and resumed gameplay, then teardown. The original A capture also demonstrates paired rejoin after Sleep. | Other specials/recovery and broader damage/KO cases remain unverified. The earlier acceptance of Nana remaining permanently asleep after Popo's stock loss is withdrawn. |
| Samus | **Native traced** in mixed-content lifecycle and rendered in both B matches. The authored fifth `x48` grapple joint owns its 34-joint graph and 25 sibling-instance references. | Down-B creates and clears Bomb. Raw-Z captures Mario into CatchWait; a fresh throw input consumes the grapple joint and enters the source throw path; mixed teardown passes. | Other specials, recovery, broader damage/KO coverage. The Kirby-copy palette owner is now resolved; effect-specific particle pixels remain unverified (Kirby row). |
| Yoshi | **Native traced** in mixed-content lifecycle and rendered in both B matches. | Neutral-B captures Mario into `ftCo_MS_CaptureYoshi`, transitions to `ftCo_MS_YoshiEgg`, and naturally releases to `ftCo_MS_Fall` at action frame 243, with no injected fighter state. | Other specials, recovery, damage/KO; the tested path is fighter-victim capture, not the separate item-target Egg Lay branch. |
| Zelda | **Native traced** in mixed-content lifecycle and selected in both B browser matches. | Four in-match down-B transformations (two each direction) preserve grounded/four-stock lifecycle. | Broader moves/recovery/damage/KO. Held-A startup form selection is not counted as down-B transformation coverage. |
| Sheik | **Native traced** as an in-match Zelda form and as a starting form; no assertion during the rebuilt B browser run. | Repeated down-B both directions; ground and air side-B each consume their distinct authored pose roots, create the Chain Article, and pass source Article teardown. | Broader moves/recovery/damage/KO. B's CSS lineup selects Zelda at the shared icon, so the browser matches do not claim an independently selected Sheik CSS slot. |

The source `ftKb_Init_803CA9D0` copy-root table includes Mario, Fox, Captain
Falcon, Donkey Kong, Koopa, Link, Sheik, Ness, Peach, Popo, Pikachu, Samus,
Yoshi, Jigglypuff, Mewtwo, Luigi, Marth, Zelda, Young Link, Dr. Mario, Falco,
Pichu, Game & Watch, Ganon and Roy. Nana uses the Popo source copy family; the
Zelda/Sheik pair shares a source copy/effect identity. Targeted donor action
coverage passes for Mario, Fox, Popo/Nana, Game & Watch, Samus and Captain Falcon.
The following source donor families remain unverified for acquisition/use/loss/replacement: Donkey
Kong, Koopa, Link, Sheik, Ness, Peach, Pikachu, Yoshi, Jigglypuff, Mewtwo,
Luigi, Marth, Zelda, Young Link, Dr. Mario, Falco, Pichu, Ganon, and Roy. The
source donor inventory is from
`.deps/melee/src/melee/ft/kinds/ftKirby/ftkirbydata.c`.

The strengthened action fixtures are checkpoint `cbee4ed`; all four focused
tests (including donor subcases) pass in
`work/pr86-distinctive-kirby-focused-r4.log`. This distinguishes same-donor
reacquisition from the separate Mario→Fox replacement test.

Kirby's Samus copy resolves `EfKbSs.dat` group 0's raw palette address
`0x80A8812A` to 512 bytes in owned `ItCo.usd` (archive SHA-256
`5d62efd6437149335b9eef87624ae1253ba7bb260fb08cbae04a3d9215eacd11`,
file offset `0x264A2A`, data offset `0x264A0A`; palette SHA-256
`99036809572a8b9d2d5918b85600320f18ab1440d165ca8e9855a53726af0ec9`).
These imply source archive/data bases `0x80823700`/`0x80823720`. The checked
source-address-region resolver copies external bytes and rejects unmapped or
out-of-range pointers. The retained headless Metal Kirby/Samus replay consumed
41,036 input events, but bounded ticks 310–491 saw no bank-34 particle generation
or TLUT call. Shutdown exited 133 with `Failed to allocate MTLBuffer`.
Effect-specific particle pixels remain unverified; this is not a pixel pass.

Retained targeted logs include `work/pr86-kirby-mario-donor-r1.log`,
`work/pr86-kirby-ice-donor-r1.log`,
`work/fighter-action-checks-v1/kirby-copy-gw-v4.log`,
`work/pr86-kirby-donor-matrix-test-r1.log` and the per-donor
`work/pr86-kirby-*-article-r1.log` records,
`work/pr86-ice-climbers-lifecycle-r2.log`,
`work/pr86-samus-catchwait-reproducer-r1.log`,
`work/pr86-sheik-ground-air-side-special-r1.log`, and the Yoshi victim-capture
trace under `work/`. These are local diagnostic receipts, not retail comparisons.
The tracked `test_remaining_fighter_distinctive_actions_and_lifecycle` case in
`tests/test_gameplay_content_match.py` reruns the Game & Watch, Ice Climbers,
Samus, Yoshi, Zelda and Sheik branches. The separate tracked Kirby donor matrix
now runs five real donor cases, including Samus's external ItCo palette owner;
both focused tests passed in `work/pr86-kirby-external-owner-r1.log`.

## Original-menu CPU9 captures and repeatability

Both lineups were set up through original CSS/SSS controller input on the owned
GALE01 revision-2 image. Reports verify actual fighter identities, CPU9 levels,
four stocks and Final Destination, rather than intended cursor movements. The
original capture used Null video: these are source state/draw/lifecycle
references, not pixel or PCM references.

| Lineup | Repeatable source evidence | Scope |
| --- | --- | --- |
| A: Game & Watch, Kirby, Ice Climbers, Fox | `work/fighter-reference-a-valid-pair.mwrc` / `.json`, independent source sessions #04/#06. | Pass: exact source-consumed PAD/workload repeatability over 42,492 frames, three natural matches and Results/CSS returns. Decoded MWRO records are exact after excluding only host timestamp and per-run capture/sequence IDs. Independent #04/#05 differed at a source-consumed CSS PAD sample; replay #04/#06 resolves repeatability without calling it CPU nondeterminism. |
| B: Samus, Yoshi, Zelda, Falco | `work/fighter-reference-b-valid-pair.mwrc` / `.json`, independent source sessions #08/#09. | Pass: exact source-consumed PAD/workload repeatability over 45,226 frames, three natural matches and Results/CSS returns. The observer now preserves both Zelda/Sheik Fighter entities sharing one source slot; the earlier observer rejection was an observer limitation, not invalid source setup. |

These valid references are evidence for the stated source setup/workload only;
they are not evidence that the browser agrees with retail.

## Rendered browser matches and original comparison

Installed headless Chrome 153 rendered the original CSS/SSS route and gameplay
over a real loopback HTTP server. Both scenarios used four source-confirmed CPU9
fighters, four stocks and Final Destination; each ran two natural matches, each
reached Results, returned through ordinary Enter input to CSS, and started the
second match. End diagnostics show CSS live with prior match/Results owners
absent. No page errors or native-command errors were recorded.

| Scenario | Receipt | Outcome |
| --- | --- | --- |
| A: Game & Watch, Kirby, Ice Climbers, Fox | `work/pr86-browser-a-two-match-r1/report.json` plus retained `initial-css.png`, SSS/stage, source-frame, natural-Results and returned-CSS screenshots. | Pass: two natural rendered matches (port source frames 10,482 and 15,596), both Results→CSS returns, second match started; 27 timing pause/resume interruptions were observed and recorded. |
| B: Samus, Yoshi, Zelda, Falco | `work/pr86-browser-b-two-match-r4/report.json` plus the corresponding CSS/SSS/stage, source-frame, natural-Results and returned-CSS screenshots. | Pass: two natural rendered matches (port source frames 14,616 and 16,939), both Results→CSS returns, second match started; 147 timing pause/resume interruptions were observed and recorded. Sheik's ground/air/down-B paths are instead established by the targeted source-action trace; B's shared CSS icon selects Zelda. |

These are rendered functional match/return results, not uninterrupted timing,
pixel/PCM equivalence, foreground timing, physical-controller acceptance or
performance evidence. Timing interruptions were explicitly resumed at the same
source frame; they are not hidden or converted into a performance pass.

Once the valid repeatable references were available, both whole-session recipes
were replayed in the browser. This harness checks the scene expected for each
consumed input, completion, and source tick/draw totals. Its
`melee-web-port-session-diagnostic` header explicitly declares
`cpu_observations: not_captured` and `draw_state: not_captured`. Match diagnostic
rows contain RNG, match clock, PAD history and primary Fighter fields, but these
rows are not compared to the original state snapshots. Secondary entities are
also absent from this port diagnostic. The immutable original MWRO streams do
retain secondary entities and before/after draw slices. The earlier statement
that these runs compared or preserved all port state/draw fields was incorrect.

| Lineup | Receipt and first observed divergence | Scheduling/result scope |
| --- | --- | --- |
| A | `work/pr86-a-original-port-comparison-r2/retail-browser-report.json`: recipe input 13,414, port match frame 11,916; expected source scene Results (4), observed port Match (3). | `input_scheduling: per_tick`; CPU decisions are excluded as inputs and recomputed. `scheduling_equivalence: not_evaluated`. The scene mismatch stops the run before the full input timeline and complete tick/draw counts; this is not a scalar-field first mismatch or a fighter attribution. |
| B | `work/pr86-b-original-port-comparison-r1/report.json` and `progress.json`: recipe input 13,329, port match frame 11,971; expected source scene Results (4), observed port Match (3). | Same per-tick recorded PAD scope; CPU decisions recomputed; scheduling equivalence not evaluated. The harness stops before completing the timeline. The Sheik pose-root crash is absent in this rebuilt run; a specific cause of the scene progression difference is not established. |

These are **failing, partial scene-progression comparisons**. They identify the
first checked scene mismatch, not the earliest scalar/state or draw divergence.
Those comparisons were never performed by this harness. The reported source
tick/draw-count failures are downstream of the incomplete replay. Headless replay does not establish foreground
timing or physical input acceptance.

An existing-roster four-Mario boundary control also diverges at the same scene
gate in the opposite direction: `work/fighter-browser-mario-boundary-control-01/retail-browser-report.json`
stops at input 14,592 with the source still in Match while the port has entered
Results. This makes match-duration/scene divergence non-exclusive to the new
roster, but does not identify one shared cause or establish a downstream state
or draw match. Its scheduling scope is also per-tick with equivalence not
evaluated; this is a bounded control receipt, not evidence that the new-fighter
comparisons pass.

## Remaining gates

1. Integrate current main including PR #89, validate character/runtime ownership
   on the reconciled branch, and establish a fresh browser baseline. Then find
   the earliest observable state mismatch with explicit capture/comparison scope
   before reducing a causal boundary.
   The four-Mario control shows the scene mismatch also occurs with the existing
   roster, in the opposite direction; no single cause is established. Keep this
   separate from the tick-1776 investigation.
2. Broaden focused moves/recovery/damage/KO coverage where still listed above,
   especially Kirby donor families still named unverified. Do not infer those
   actions from CPU9 matches. The captured Samus copied-special interval did not
   call the bank-34 particle generator or wrapped-address TLUT; this scoped
   negative does not establish all particle visuals.
3. Keep pixels, PCM, foreground scheduling, physical controllers, performance
   and admission separate and unclaimed.

PR #86 remains the single open PR. No merge or deployment has been made.
Evidence does not support “fully playable,” equivalence, or task completion.

## Integration checks

An earlier full-suite run recorded 1,442 tests (1,354 pass, 88 optional skips).
The first 1,465-test run found six failures: four ad-hoc compile fixtures
omitted `gameplay_result_motion_table.cpp`, a direct material-animation graph
path lacked the 256-joint bound, and four new CPU probes were absent from the
pinned profile. Those were fixed and focused regressions passed. The next clean
run recorded 1,465 tests, 78 skips and one fixture-only compile failure:
Fountain and Old-Yoshi synthetic readers used positional `MeleeWebNativeDat`
initializers that omitted the new `source_region` callback. Both now use named
initializers; the focused Fountain test and strict Old-Yoshi syntax check pass.
The final clean run passed: 1,465 tests in 445.966 seconds, 78 optional skips,
zero failures (`work/pr86-final-suite-r3.log`). The affected Release runtime and
content-trace builds also pass. The focused Wasm batch passes five Kirby donors
plus all six distinctive-action branches.
