# Remaining fighter port notes (PR #86)

## Disposition

These six selectable characters (seven source forms including Zelda/Sheik) are
an unfinished development candidate, not admitted or fully playable. Real-asset
construction, targeted distinctive actions, two rendered four-CPU9 match loops
per requested lineup, and repeatable original references are now available.
The browser matches are functional rendered evidence, not original equivalence.
Both supported original-versus-port comparisons stop at a natural match-duration
scene divergence. The Samus-donor palette now resolves through a checked
cross-archive source-address map; particle-pixel use remains unverified.

Evidence labels follow the
[performance/accuracy playbook](PERFORMANCE_AND_ACCURACY.md). Raw disc data,
extracts, captures, screenshots and run logs remain local and ignored under
`work/` / `assets-local/`; this note contains no personal input paths.

## Character action checklist

Construction results refer to real-asset source traces that execute source ticks
and teardown. Targeted action runs use costume zero unless noted. A passing
browser match supplements these checks; it does not prove each listed action.

| Character | Content/construction | Targeted action coverage | Still unverified / failed |
| --- | --- | --- | --- |
| Game & Watch | **Native traced** in mixed-content lifecycle and rendered in both A matches. Ten authored Articles pass the real visibility/ownership regression. | Chef source motion, sausage Article creation/lifetime/teardown. | Other specials, defense/recovery, damage/KO and broader action inventory. |
| Kirby | **Native traced** against Fox across all six costumes with repeated construction/teardown; rendered in both A matches. Copied-part visibility counts/IDs and Game & Watch's secondary texture-animation pair are source-adapted and checked against live DObj visibility. | **Passed for Mario, Popo/Ice Climbers, Game & Watch, Fox, and Samus donors:** inhale acquisition, a source-created copied-neutral-special Article, up-appeal copy loss, reacquisition/replacement and match teardown. G&W additionally passes copied Chef/Pan Article ownership and visibility. The Samus row resolves `EfKbSs.dat` group 0's raw palette address `0x80A8812A` to the exact 512 bytes in owned `ItCo.usd` (SHA-256 `5d62efd6437149335b9eef87624ae1253ba7bb260fb08cbae04a3d9215eacd11`, file offset `0x264A2A`, DAT data offset `0x264A0A`; palette SHA-256 `99036809572a8b9d2d5918b85600320f18ab1440d165ca8e9855a53726af0ec9`). Subtracting the raw file offset from the guest address implies an archive base of `0x80823700` (`0x80823720` for the DAT data section). A generic checked source-address-region resolver now owns/copies external bytes and rejects unmapped or out-of-range pointers; no row is omitted or coerced. The five-donor tracked regression passes acquisition, Article use, loss, replacement and teardown. A headless Metal replay of the exact Kirby/Samus PAD workload completed all 41,036 input events; bounded match-0 probes over ticks 310–491 saw no bank-34 particle generation or TLUT call during that captured interval, so effect-specific particle pixels remain unverified. Its process exited 133 on shutdown with `Failed to allocate MTLBuffer` after observer/input completion; no screenshot/pixel claim is made. Other normals/defense/recovery/damage/KO remain open. |
| Ice Climbers (Popo/Nana) | **Native traced** as two distinct source entities with repeated lifecycle; rendered in both A matches. | Belay separates the pair (10 to 91.2236 source units). A CPU9 opponent kills Nana; she finishes her death motion in source `ftCo_MS_Sleep`, while Popo survives. Ordinary source input then loses Popo's stock: Popo rebirths, Nana remains asleep. Decomp `Player_80032070` only re-rebirths Nana when her source dead flag is set; this observed non-rejoin behavior is consistent with that lifecycle, not evidence of an entity-persistence defect. Teardown passes. | Other specials, recovery, partner rejoin after other causes, damage/KO cases beyond this Nana-death/Popo-stock sequence. |
| Samus | **Native traced** in mixed-content lifecycle and rendered in both B matches. The authored fifth `x48` grapple joint owns its 34-joint graph and 25 sibling-instance references. | Down-B creates and clears Bomb. Raw-Z captures Mario into CatchWait; a fresh throw input consumes the grapple joint and enters the source throw path; mixed teardown passes. | Other specials, recovery, broader damage/KO coverage. The Kirby-copy palette owner is now resolved; effect-specific particle pixels remain unverified (Kirby row). |
| Yoshi | **Native traced** in mixed-content lifecycle and rendered in both B matches. | Neutral-B captures Mario into `ftCo_MS_CaptureYoshi`, transitions to `ftCo_MS_YoshiEgg`, and naturally releases to `ftCo_MS_Fall` at action frame 243, with no injected fighter state. | Other specials, recovery, damage/KO; the tested path is fighter-victim capture, not the separate item-target Egg Lay branch. |
| Zelda | **Native traced** in mixed-content lifecycle and selected in both B browser matches. | Four in-match down-B transformations (two each direction) preserve grounded/four-stock lifecycle. | Broader moves/recovery/damage/KO. Held-A startup form selection is not counted as down-B transformation coverage. |
| Sheik | **Native traced** as an in-match Zelda form and as a starting form; no assertion during the rebuilt B browser run. | Repeated down-B both directions; ground and air side-B each consume their distinct authored pose roots, create the Chain Article, and pass source Article teardown. | Broader moves/recovery/damage/KO. B's CSS lineup selects Zelda at the shared icon, so the browser matches do not claim an independently selected Sheik CSS slot. |

The source `ftKb_Init_803CA9D0` copy-root table includes Mario, Fox, Captain
Falcon, Donkey Kong, Koopa, Link, Sheik, Ness, Peach, Popo, Pikachu, Samus,
Yoshi, Jigglypuff, Mewtwo, Luigi, Marth, Zelda, Young Link, Dr. Mario, Falco,
Pichu, Game & Watch, Ganon and Roy. Nana uses the Popo source copy family; the
Zelda/Sheik pair shares a source copy/effect identity. Targeted donor action
coverage passes for Mario, Fox, Popo, Game & Watch, and Samus. The following
source donor families remain unverified for acquisition/use/loss/replacement: Captain Falcon, Donkey
Kong, Koopa, Link, Sheik, Ness, Peach, Pikachu, Yoshi, Jigglypuff, Mewtwo,
Luigi, Marth, Zelda, Young Link, Dr. Mario, Falco, Pichu, Ganon, and Roy. The
source donor inventory is from
`.deps/melee/src/melee/ft/kinds/ftKirby/ftkirbydata.c`.

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

Once the valid repeatable references were available, both original-versus-port
whole-session replays were run with the retained state/draw fields and without
weakening comparison criteria:

| Lineup | Receipt and first observed divergence | Scheduling/result scope |
| --- | --- | --- |
| A | `work/pr86-a-original-port-comparison-r2/retail-browser-report.json`: recipe input 13,414, original match frame 11,916; expected source scene Results (4), observed port Match (3). | `input_scheduling: per_tick`; CPU decisions are excluded as inputs and recomputed. `scheduling_equivalence: not_evaluated`. The scene mismatch stops the run before the full input timeline and complete tick/draw counts; this is not a scalar-field first mismatch or a fighter attribution. |
| B | `work/pr86-b-original-port-comparison-r1/report.json` and `progress.json`: recipe input 13,329, original match frame 11,971; expected source scene Results (4), observed port Match (3). | Same per-tick recorded PAD scope; CPU decisions recomputed; scheduling equivalence not evaluated. The harness stops before full state/draw counts. The Sheik pose-root crash is absent in this rebuilt run; this remaining difference is match-duration/scene progression, not evidence of that crash or a specific fighter cause. |

These are supported but **failing, partial** original comparisons. They identify
the exact earliest gate divergence; they do not establish a later scalar/state
or draw-field divergence because scene validation stops first. The reported
source tick/draw-count failures are downstream of the incomplete replay, not a
reason to omit or relax fields. Headless replay does not establish foreground
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

1. Reduce the Results-versus-Match duration divergence to a smaller causal
   case and compare unchanged source state/draw fields up to that boundary.
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
