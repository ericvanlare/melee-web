# Remaining fighter port notes (PR #86)

## Disposition

These six selectable characters (seven source forms including Zelda/Sheik) are
an unfinished development candidate, not admitted or fully playable. Real-asset
construction, targeted distinctive actions, two rendered four-CPU9 match loops
per requested lineup, and repeatable original references are now available.
The browser matches are functional rendered evidence, not original equivalence.
The historical whole-session replays stopped at scene mismatches; their
harness did not establish the earliest gameplay-state divergence. The recorded-
session comparator identifies the first mismatch only among its declared
captured gameplay fields and input scope; it does not compare raw CPU blocks,
port draw state, pixels, or live scheduling. The Samus-donor palette now
resolves through a checked cross-archive source-address map; particle-pixel use
remains unverified.

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

### Earlier reconciliation checkpoint (historical)

At this earlier checkpoint, PR #86 was semantically merged with `origin/main`
`8671362` at commit `4cc63d7` (including #89 commit `aecc3e64` and PR #93
merge `2e083bf`). Its test/build results below retain that checkpoint's
provenance and are not retroactively relabeled as validation of the newer
merged tree.

On the merged tree, the Results source-tick/entry Node contracts and focused
Python entry suite pass (8/8). The `gameplay_results_scene_trace.js` and
`native_menu_host_trace.js` C++ targets build after
`python3 scripts/gameplay_sources.py` refreshes generated sources.
The initial focused invocation stopped before simulation with
`Missing required native menu runtime file: MnMaAll.usd`: the ignored local
fixture then had 38 of the 73 files required by current `menu_asset_names()`.
All 35 missing exact disc files are now extracted into ignored
`assets-local/native-menus`, preserving existing files. With the complete
fixture, the P1-only native reducer runs its synthetic Zelda-origin Sheik
Results control, observes both disconnected CPU page transitions at source
tick 551, P1 confirmation at 600, and Results exit at 622. It then fails
`melee_web_menu_host_destroy()`'s
precondition with `Close native menu scene and restore RNG before destroying
host` (receipt `work/results-match-history-tests/p1-statistics-j4oi2h8y/`).
The control flow isolated a retained nonzero CSS/SSS transition latch: normal
leave marked the scene closed but did not clear that consumed latch. The tracked
fix in `src/gameplay_menu_host.c` clears the latch and consumed parent-route
flag after successful leave; `tests/native_menu_host_trace.cpp` adds a normal
CSS→SSS leave-and-destroy regression. The rebuilt regression passes, as does
the same P1-only synthetic reducer. Its receipt is
`work/results-match-history-tests/p1-statistics-f6r54eej/stdout.log`: both
disconnected CPU pages advance at source frame 551 before P1 confirms at 600;
Results exits and host OnExit/commit complete at tick 622; the retained camera
pool stays `0x81e5f00`, and all four demo owners are constructed and closed.
These checks and the Release runtime/native builds ran with `HEAD=4cc63d7` plus
the tracked source/test changes committed unchanged as `5e06c51`; the runtime
artifacts then hashed to Wasm
`86affc285908924423aa4d26aa550b072d51e56c0aa736241a6da4bd8a231dba` and JS
`62bf2d142fa0aaf8d7642cc2fbba39bf80c43355461d182de0eb9b2853f7f89a`. A fresh
build/test invocation from clean `5e06c51` remains pending before any
commit-bound browser claim.
This excludes a generic failure of that synthetic P1-only/auto-page/host-close
route and fixes its later host-destruction precondition. It does not test draw,
natural CPU9 outcomes, or the historical Sheik-winner camera-pool failure.

Critically, do not treat the synthetic reducer's former destroy assertion as
the cause of the historical camera failure. The historical report
`work/pr86-final-frozen-b-two-match-r1/report.json` records a natural slot-2
Sheik win (match frame 11,725, RNG 2,897,689,676) and reports camera-pool
ownership error surfaced at Results source cursor 560. The report records
checkout `607ff56`, but its served frozen artifact is hash-bound rather than
commit-bound. In that checkout, the equality check existed only in
`release_source_camera_and_ground()` during Results teardown; tick and draw
paths had no camera-pool checks. Cursor 560 therefore identifies where the
late cleanup check detected the mismatch, not when the pointer first changed.
The report retains
three P1 Enter intentions (160 ms held/120 ms released), but no Results-frame
event brackets, consumed PAD rows, connected-port state, pointer values or
ownership phase. Thus the first invalid transition cannot be recovered from
that report. A source-entry packet from another run is not a substitute. A
fresh rendered-browser control now exists on clean source head `a116abc`, but
it ends with Samus, not the historical slot-2 Sheik winner. Its three Enter
pulses were not consumed as P1 Start; the historical consumed PAD state is
absent, so this control is not an exact replay. Historical rendered-browser
reports remain bound to their original revisions.

The current merged-runtime target build and focused gates pass: source-context
and collision traces; owned-DOL `test_source_stack_profile.py`; common-context
restore/restart; Final Destination numeric context; authored/local-common/
Mario effect-bank cases; Nana's real nonzero Results root; and 44 texture,
material, native-joint/animation, archive-cache and archive-section tests. The
optional Link/Pikachu effect-archive checks were skipped because those local
archives are absent. This is not the required full suite or rendered gameplay.

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
Frozen `4fe075e` causal browser validation now agrees exactly through all 8,411
observed second-match rows (ticks 0–8410), clearing the former tick-8353
difference (`work/pr86-shield-sdi-b-boundary-state-r3.json`). The rendered run
advances to source input 22,566, retains its gameplay screenshot and unloads
cleanly without a page/native error. This is an incomplete declared-field
prefix, not whole-session equivalence. The separate functional Results failure
below remains open.

The full frozen `4fe075e` extension now completes all 45,226 inputs and three
natural match/Results/CSS returns with zero reported browser errors
(`work/pr86-shield-sdi-b-full-r2/report.json`). Its exact state comparisons agree
on all 12,095 / 14,132 / 15,680 captured gameplay rows, including both former
shield-SDI boundaries (`work/pr86-shield-sdi-b-full-state-match{0,1,2}-r2.json`).
The served Wasm SHA-256 is
`596704be242c6e01fd034990443bda9ebc37deb64efabab2ec12f48e12230b1d`.
This clears the observed B gameplay mismatches for the declared fields and
recorded-input scope. It does not compare raw CPU blocks, port draw state,
pixels/PCM or live scheduling; the exact comparator retains `complete=false`.

An additional B functional run reaches natural Results after 11,725 gameplay
frames with Sheik winning, then fails the camera-pool ownership guard at
Results frame 560 on return to CSS
(`work/pr86-final-frozen-b-two-match-r1/report.json`). This is an unresolved
runtime failure, not a comparison-only limitation. The new four-CPU,
Sheik-winner native Results confirmation/close control passes
(`work/pr86-sheik-results-repro-r1.log`), so the investigation continues at the
rendered/mode-exit ownership boundary. Neither a Sheik-specific cause nor PR
readiness is claimed.

### Reconciled rendered Results page-gate observations (2026-09-27)

On the post-#101 branch at `390f27d`, the headless Chrome 153 functional route
was rebuilt with development Wasm SHA-256
`0e288debdc4fb4c3a75d64c0bf0bb7420ff726d8ebbdac6b04b9a9f537310529` and a
capacity-8192 Results PAD/state trace. These runs are not a reproduction of the
historical Sheik-winner failure and did not reach its camera guard.

`work/pr86-results-padtick-b-natural-r2/report.json` retained a Samus-winning
natural B match where the first P1 Start was sent at Results source frames
182–191 while the original Results phase was still 1; it was ignored. The
trace shows phase 2 only from source frame 201, so this run does not test the
statistics transition. The phase-gated `r3` and `r4` runs queue the first
P1-only ten-tick edge in phase 2 and record disconnected CPU pages 2/3 advancing
to page 1 in active statistics at source frames 396 and 399 respectively,
before the planned confirmation boundary at frame 600. The trace preserves
P1/P2 connected and neutral, ports 2/3 disconnected, every consumed source
tick and no overflow. `r3` ends with Falco winning; `r4` ends with Samus winning
and retains Zelda-origin Sheik identity (`ckind=18`, `ftkind=7`).

Neither rendered run consumed the subsequent P1 confirmation: the existing
raw-PAD guard rejected queueing while the scene was stopped (`running=0`), so
neither proves a Results→CSS return or reaches the camera-ownership boundary.
The first atomic resume/queue helper inspected native diagnostics instead of
the page's `#status` timing-pause banner; its focused regression now checks the
DOM status before resuming. The native Results fixture separately consumes the
P1 confirmation at source tick 600 after CPU-page transitions at 551, but its
default-CSS synthetic standings are not a natural CPU9 Sheik win. The historical
camera-pool failure remains open.

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

The enclosing Results mode handoff now checks live profile aliases and the
camera-pool triple before/after OnExit and after route commit. Fault tests prove
rejection before callback writes; a failed postcondition does not replay a
consumed callback. The real-asset entry fault case and reduced dispatch tests
pass (`work/pr86-results-handoff-guard-sL8pY9/`); the initial missing-music
fixture failure is retained and resolved using the existing owned music file.
All three Zelda-origin Sheik winner animations are selected by the original
CopyPAD B/Y/X consumer and verified as demo motions 0/2/5. Native state-only
cases preserve ownership through 744 frames, mode exit and close
(`work/pr86-results-demo-variants-r2.log`, six focused tests). Their first build
failed on C-only Fighter headers; the fixture now uses existing read-only
`ftLib` accessors. An attempted Node draw traversal failed in the uninitialized
Aurora FIFO; that failed run is retained in `r1`. Native checks now explicitly
report drawing unrun, and actual drawing remains required in the separate
initialized GPU fixture. No historical gameplay fix is inferred from these
negative controls.

All three variants also pass the initialized GPU fixture frozen at `1103bd6`
(`work/pr86-results-demo-rendered-cow-qbtCof/{b,y,x}/report.json`). Installed
headless Chrome renders the distinct Sheik poses, advances 744 Results frames
and draw API calls, then completes host exit/commit and all four demo-owner
closures with the same camera pool. Source transition-2 draw suppression is
unchanged; API-call totals do not assert every callback ran. Each case retains
frame-30/180/600 screenshots, zero page/network/HTTP errors, hardware WebGPU
identity and exact served artifact hashes. The 74,213,141-byte preload is hashed
from the actual received ArrayBuffer before construction and that same buffer
is handed to Emscripten. A game-free reduction established CDP body eviction;
the changed observation method preserves exact byte/hash checks and does not
turn the older failed reports into passes. These are synthetic all-CPU Stock
Results controls, not a reproduction or fix of the historical natural failure.

The frozen `1103bd6` runtime also passes a fresh natural B two-match loop
(`work/pr86-handoff-b-two-match-r1/report.json`): 13,393/14,989 gameplay frames,
rendered Results at 181/184, both original CSS returns, and zero reported
page/native errors or timing interruptions. Actual winners are Samus and Falco;
the old Sheik-winner failure remains open. Ready-match observations contain
both active P3 FighterKind 19 (Zelda) and 7 (Sheik), distinct from merely
retaining a dormant secondary entity. Both entry packets are hash-bound,
gameplay/Results/CSS screenshots are retained, and final CSS has 33 objects /
17 processes with no Match/Results/Prize owner. Served Wasm SHA-256:
`90366cbde83f6ca929b6a0f8d77caabe77c5cceb4975230d52a3562513f7aa79`.
The harness records a changed working-tree hash because documentation and
unbuilt test fixtures were edited during the run; the frozen served bytes did
not change. Absence of recorded interruptions is not live performance evidence.

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
The next causal attempt, without concurrent builds or other browser cases,
passes the bounded comparison above; simulation timing, input recipe and queue
bounds remain unchanged. That does not establish the cause of the earlier
audio overflow. A subsequent full-recipe attempt loses its Chrome connection
during CSS (`work/pr86-shield-sdi-b-full-r1/report.json`), before gameplay.
The retained operator-recovery log records stopping only the disconnected test
loop so its in-memory failure report could be saved. No game state was changed.
The tracked `tests/character_reference_browser_test.mjs` now rejects that
closed/disconnected-page condition immediately, propagates snapshot errors,
and hashes the actual served JS/Wasm. Its focused failure-propagation test
passes (`work/pr86-reference-health-r1.log`); the fresh full recipe completes as
recorded above.

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

The borrowed G&W category-4 visibility view now has an additional original-code
proof, rather than relying only on the copy descriptor's shorter count. Owned
`PlKb.dat` supplies two body rows and `PlKbCpGw.dat` supplies one copy row. The
next apparent count is an adjacent relocated pointer. Owned-DOL profiles verify
signed `cmpw` in `ftParts_80074B6C`, `80074CA0`, and `80074D7C`: the original MEM1
pointer makes that inner loop empty, while a native Wasm pointer is positive.
The adapter derives both counts from the owned descriptors and rejects a tail
with any nonempty original signed loop before permitting the scoped borrowed
view. It does not rewrite pointer bits or infer a bound from an asset filename.
Seven focused checks pass, including positive-tail rejection, scalar-zero and
negative tails, all six owned donor archives/all colors, and the three owned
executable profiles (`work/pr86-kirby-signed-visibility-r1.log`, exit0). Affected
runtime and rendered validation of this additional rejection remain pending.

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

The six single-fighter forms also pass `--remaining-damage-ko`
(`work/pr86-damage-ko-r1.log`, six subcases). Ordinary Mario A input must
actually increase the victim's damage; the victim then walks off using PAD,
loses exactly one stock, enters Rebirth, restores zero damage and its original
active identity, resumes grounded Wait, and completes repeated teardown.
No position, damage, motion, stock or RNG field is injected. This supplements
the rows below with targeted damage/KO lifecycle coverage at costume zero;
other attacks, launch directions, costumes and off-stage recovery remain open.

| Character | Content/construction | Targeted action coverage | Still unverified / failed |
| --- | --- | --- | --- |
| Game & Watch | **Native traced** in mixed-content lifecycle and rendered in both A matches. Ten authored Articles pass the real visibility/ownership regression. | Chef source motion, sausage Article creation/lifetime/teardown; ground/air Fire, Rescue Article lifetime and landing; ordinary damage/KO/Rebirth as scoped above. | Other specials, defense, off-stage recovery and broader action inventory. |
| Kirby | **Native traced** against Fox across all six costumes with repeated construction/teardown; rendered in historical A matches. Copied-part visibility counts/IDs and Game & Watch's secondary texture-animation pair are source-adapted and checked against live DObj visibility. | Six donors pass acquisition, copied use, up-appeal loss, same-donor reacquisition and teardown: Mario, Popo/Nana, G&W, Fox, Samus and Captain. The first five create their copied Articles; Captain's Falcon Punch enters the copied motion and damages its victim. A separate three-player case verifies true Mario→Fox replacement and use. The Ice observer identifies actual victim ownership across both entities; Nana maps to the Popo copy family. G&W additionally checks Chef/Pan ownership and visibility. Ground/air Final Cutter, beam Article lifetime and landing pass; Fox/G&W/Samus copy KO loss, Rebirth and resumed gameplay pass. | Nineteen donor families listed below, other-donor KO loss and random damage-triggered copy loss remain unverified. Other normals/defense/off-stage recovery and copied-particle pixel equivalence remain open. |
| Ice Climbers (Popo/Nana) | **Native traced** as two distinct source entities with repeated lifecycle; rendered in historical A matches. | Belay separates the pair. The strengthened CPU9 case observes Nana death→Sleep while Popo retains his stock, later Popo stock loss, both entities in Rebirth, Nana damage reset and resumed gameplay, then teardown. The original A capture also demonstrates paired rejoin after Sleep. | Other specials/recovery and broader damage/KO cases remain unverified. The earlier acceptance of Nana remaining permanently asleep after Popo's stock loss is withdrawn. |
| Samus | **Native traced** in mixed-content lifecycle and rendered in both B matches. The authored fifth `x48` grapple joint owns its 34-joint graph and 25 sibling-instance references. | Down-B creates and clears Bomb. Raw-Z captures Mario into CatchWait; a fresh throw input consumes the grapple joint and enters the source throw path; mixed teardown passes. Ground/air Screw Attack continuation and landing pass. | Other specials, off-stage recovery, broader damage/KO coverage. The Kirby-copy palette owner is now resolved; effect-specific particle pixels remain unverified (Kirby row). |
| Yoshi | **Native traced** in mixed-content lifecycle and rendered in both B matches. | Neutral-B captures Mario into `ftCo_MS_CaptureYoshi`, transitions to `ftCo_MS_YoshiEgg`, and naturally releases to `ftCo_MS_Fall` at action frame 243, with no injected fighter state. Ground/air Egg Throw, thrown-Egg Article lifetime and landing pass; ordinary damage/KO/Rebirth as scoped above. | Other specials, off-stage recovery, broader damage/KO; the tested Egg Lay path is fighter-victim capture, not its separate item-target branch. |
| Zelda | **Native traced** in mixed-content lifecycle and selected in both B browser matches. | Four in-match down-B transformations (two each direction) preserve grounded/four-stock lifecycle. Ground/air Farore's Wind continuation and landing pass. | Broader moves/off-stage recovery/damage/KO. Held-A startup form selection is not counted as down-B transformation coverage. |
| Sheik | **Native traced** as an in-match Zelda form and as a starting form; no assertion during the rebuilt B browser run. | Repeated down-B both directions; ground and air side-B each consume their distinct authored pose roots, create the Chain Article, and pass source Article teardown. Ground/air Vanish continuation and landing pass. | Broader moves/off-stage recovery/damage/KO. B's CSS lineup selects Zelda at the shared icon, so the browser matches do not claim an independently selected Sheik CSS slot. |

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

## Historical browser matches and scene-only comparison

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

1. Resolve the historical natural Results camera-pool failure. The current
   source-tick path now has both a rendered natural slot-2 Zelda-origin Sheik
   winner (`work/pr86-0c9b21a-b-four-match-source-tick-r1/report.json`) and a
   fresh native CPU9 MatchExitInfo→Results reducer at PR head; page transitions
   and confirmation pass under that route. Current keyboard-gated browser
   matches also pass, but their fresh winners were Samus. The old Sheik packet
   lacks trusted key brackets, consumed PAD/page state, controller status and
   camera-entry pointers. Its first invalid ownership transition remains
   unknown. The remaining discriminator is the natural Sheik winner with the
   trusted ordinary-Enter path and first-change camera snapshots; do not claim
   resolution from either separately passing route.
2. The shield-SDI candidate's full B reference replay and exact declared-field
   comparisons now pass for every captured gameplay row. The fresh A two-match
   and B four-match browser loops are retained; complete final affected
   builds/full tests and the supported original comparison. Main including #89 is already integrated;
   source-context, owned stack-profile and ownership checks pass on that base.
   Reduce any new first mismatch before extending another run. Keep the separate
   broader-equivalence/tick-1776 investigation with its existing owner.
3. Broaden focused moves/recovery/damage/KO coverage where still listed above,
   especially Kirby donor families still named unverified. Do not infer those
   actions from CPU9 matches. The captured Samus copied-special interval did not
   call the bank-34 particle generator or wrapped-address TLUT; this scoped
   negative does not establish all particle visuals.
4. Keep pixels, PCM, foreground scheduling, physical controllers, performance
   and admission separate and unclaimed.

PR #86 remains the single open PR. No merge or deployment has been made.
Evidence does not support “fully playable,” equivalence, or task completion.

## Reconciled-head natural Sheik-winner Results discriminator (2026-09-27)

The current-head rendered receipt is
`work/pr86-0c9b21a-b-four-match-source-tick-r1/report.json`. It binds to clean
source HEAD `0c9b21afb59d50157da21f518290d20d0c82d430`, tree
`eae066b2bd3e98e28a0c8179921fdc07cb7b6cbc`, with no tracked diff at start or
end. The served Release artifacts are headless Chrome 153 / WebGPU Metal JS
`62bf2d142fa0aaf8d7642cc2fbba39bf80c43355461d182de0eb9b2853f7f89a` and Wasm
`8cf791ba7f8e558ab03f215b3f1be2565545eee97e899265ca4dd84d9e829406`.

Four natural lineup-B matches (Samus, Yoshi, Zelda, Falco; all CPU9; four
stocks; Final Destination) each reached Results, returned to original CSS,
and entered the next match. Match 1 ended at gameplay frame 14,885 with slot 2
as the sole terminal winner; the retained typed entry packet identifies
`ckind=18, ftkind=7` (Zelda-origin Sheik), and
`match-1-natural-results-after-first-start.png` visibly shows Sheik first.
No winner, seed, form, or gameplay state was forced.

The Results input was a development source-boundary PAD schedule, not ordinary
keyboard event replay: P1-only ten-tick Start holds at Results ticks 180, 360,
and 600; ports 0/1 were connected (P2 neutral), and CPU ports 2/3 disconnected.
Both CPU pages advanced 0→1 at completed source frame 552 (the tick-551
sample), before the consumed P1 confirmation at tick 600. All 622 Results
source ticks returned. The source camera pool was null before Results OnEnter,
became `0xb21b940` during OnEnter, and matched the context/owner after collision
adoption. The phase-labelled ownership guards did not fire through Results
draw/tick, close, host handoff, teardown, or CSS return. The report retains the
consumed PAD rows, page checks, entry packets, screenshots, source progress,
and zero page/native/timing-disruption errors. This is rendered functional
evidence, not retail state/draw equivalence, pixel equivalence, or foreground
timing/performance evidence.

Comparison with the historical report
`work/pr86-final-frozen-b-two-match-r1/report.json` leaves a real gap. That
older run has three P1 Enter intentions (160 ms held / 120 ms released), a
natural Sheik win, and a camera-pool error detected at Results cursor 560, but
no build-manifest link from its served Wasm hash to a source commit, trusted
keyboard event brackets, consumed PAD edge/release rows, CPU page state, or
camera-entry pointer snapshot. In the fresh run above the final confirmation is at 600, after the
CPU page transition at 552; the historical failure is detected 40 ticks
earlier. The old cursor therefore cannot be mapped to an equivalent input or
first-corruption boundary. A source diff review from old checkout `607ff56` to
this head finds the added Results pool observations/guards and per-destructor
checks, but no change to the original Results OnEnter/OnExit routines that
would explain a behavioral repair. The current run excludes a generic
Sheik-winner teardown failure under its controlled source-tick route; it does
not establish that the historical keyboard failure was fixed.

Next discriminator: keep the same first two source-tick pulses but queue the
final P1 edge at tick 553, immediately after both CPU page transitions at 552.
If that current-head natural Sheik case also returns cleanly, proceed to a
current-head ordinary-keyboard run that records event brackets and consumed
PAD rows; do not infer its outcome from the controlled PAD case. The historical
first invalid ownership transition remains unlocated until those differences
are tested or a reviewable first-change packet establishes the boundary.

## Current natural Results checkpoint (2026-09-27)

The retained natural Sheik-winner failure
(`work/pr86-final-frozen-b-two-match-r1/report.json`) records harness/source
checkout commit `607ff56428cc7c110c1ce2e552110351f2e8bcbb` and a local
documentation-only edit. It served the frozen artifact
`work/pr86-final-candidate-frozen/gameplay_menu_browser.wasm`, SHA-256
`96592b3fdab21695b5209d41715e65b50f156b025b671860c776a9adec525faf`, also
recorded in `prototype-build.json`. That manifest binds artifact bytes but not
their source commit; the binary predates the harness commit timestamp. The
terminal standings identify slot 2 as the winner; the retained screenshot and
form evidence show Zelda-origin Sheik. The report fails at Results source
cursor 560 with the generic camera-pool message, but has no raw Results PAD
trace or pointer triple.

In the 607ff source checkout (whose Results source files are unchanged from its
parent), the only `cm_804D645C` equality failure was in the final
`release_source_camera_and_ground` step, after scene OnExit,
flash/object/collision teardown. The enclosing Results host called mode
OnExit and committed the route without a camera-pool check. This identifies the
old assertion as a late release-boundary observation, not the first invalid
write; the frozen executable lacks commit-level build provenance. Current HEAD
adds phase-labelled pool checks through tick/draw, scene OnExit, mode OnExit
and route commit, but the natural Sheik-winner path has not yet been rerun on
that instrumented code.

Fresh current-HEAD natural control
(`work/pr86-results-boundary-b-natural-r7/report.json`) used the B four-CPU9,
four-stock Final Destination route in headless Chrome 153 with WebGPU. The
served development Wasm was HTTP 200, 119,763,091 bytes, SHA-256
`0e288debdc4fb4c3a75d64c0bf0bb7420ff726d8ebbdac6b04b9a9f537310529`; the
report binds source HEAD `d1d5c26024efc0f5d2e6044b244d602800b2f9e3`, clean at
capture. This match naturally reached Results at source frame 14,003; slot 0
won (RNG `3303176909`), so it is not the Sheik-winner discriminator. P3
transformed Zelda↔Sheik repeatedly.

The passive trace retained 4,710/4,710 Results ticks without overflow or tick
failure and preserved port errors `[0,0,-1,-1]`. At Results tick 560, P1 was
neutral, stats phase was 2, and all result pages remained at 0. The only
consumed P1 Start was ticks 509–515; 48 Enter pulses caused 96 trusted key
events and 251 timing pause/resumes. Disconnected CPU pages advanced at ticks
701 and 1703. No camera-pool guard fired; the harness failed its Results→CSS
return check at source cursor 4708. This is a keyboard/timing control, not a
camera-ownership resolution. The rendered synthetic control at
`work/pr86-results-rendered-post93-r1/statistics/report.json` remains scoped to
its default-CSS synthetic Results profile: pages advanced at tick 551 and the
pool remained unchanged through 621 rendered ticks and route commit.

### Natural P1-only auto-page control (2026-09-27)

Two fresh B CPU9 captures exercised the source-tick P1-only Results control
through the actual four-player menu/match path. The retained reports are
`work/pr86-results-padtick-b-natural-r5/report.json` and
`work/pr86-results-padtick-b-natural-r6/report.json`. Both served the current
development Wasm SHA above in headless Chrome 153/WebGPU, preserved the natural
port profile (`err=[0,0,-1,-1]`), retained all Results PAD samples, and reached
the original CSS return screenshot. In r6 the natural winner was slot 2 at
match frame 14,204 (RNG `1088324269`), but its typed Results identity was
`ckind=18, ftkind=19` (Zelda), not Sheik. Slot 2 transformed to Sheik during
gameplay, then back to Zelda at match frame 13,364. This remains a Zelda-win
control and does not resolve the historical Sheik-winner failure.

The r6 Results trace retained 631/631 ticks with no overflow or failed tick.
Disconnected CPU pages 2 and 3 changed 0→1 at Results source frame 397 while
`phase=3, stats_phase=2`; both transitions occurred before the consumed P1
confirmation at source frame 609. P1 Start was held on ticks 205–214 and
609–618, with consumed neutral releases and no unexpected P1 buttons. P1/P2
were connected, neutral P2 stayed unchanged, and P3/P4 remained disconnected.
The page-transition assertion passed and the browser returned to CSS without
page/native errors or a camera-pool guard firing. This excludes a generic
failure of CPU auto-page alone for this current Zelda-winner source-tick profile.
It is not the old ordinary-keyboard route: the confirmation landed at tick 609
(the historical close failure was observed at tick 560), and the old report
does not retain consumed input ticks to establish exact timing parity.

Both browser reports are correctly retained as runner failures caused after
the successful CSS return by a harness trace-record shape mismatch, not as
runtime/camera failures. The first expected a summary on a raw trace; the
second expected samples on the new `{summary, trace}` record. The corrected
call site and pure trace-record regression now pass the focused Node and
four-test Results-entry Python suite. The r6 capture was independently
revalidated offline against its retained raw trace, typed entry packet and
returned-CSS screenshot; no fresh clean browser receipt has yet been produced
after the final harness fix.

### Passive Results entry pointer snapshots (2026-09-27)

The Results context now copies read-only pointer values immediately before and
after source `gm_Scene_Results_OnEnter`, after native collision adoption, and
after the context/owner pointers are adopted. The development PAD-trace observer
serializes those values with the retained raw four-port source-tick trace. This
does not establish ownership, change teardown order, or weaken any existing
guard. Focused static/source-order and Results-entry packet tests pass; the
existing native Results handoff, pool-guard and winner-demo controls also pass
(six focused checks). Those native binaries predate the observer. The affected
development runtime was rebuilt successfully; its served Wasm SHA-256 is
`ac42965282993856a312b2fc273c7c98c0be732a39dbdea63a9d0253369ebcd0`.
Two fresh captures now contain the entry snapshots, but neither reached a
camera guard or reproduced the historical Sheik-winner failure.

The historical natural-keyboard control at
`work/pr86-results-boundary-b-natural-r7/report.json` provides a source-tick
prefix for the separate `keyboard-gated` diagnostic: trusted Enter keydowns at
Results frames 198, 296, 394, and 509; only the frame-509 pulse was consumed as
P1 Start (frames 509–515). Active statistics began at frame 520. Disconnected
CPU slots 2 and 3 both auto-advanced from page 0 to page 1 at frame 701, 181
source-frame ticks later, with port errors `[0,0,-1,-1]`. This matches the
source counter `x2++ > 180` in `gmresultplayer.c`. Their next automatic page
advance occurred at frame 1703. The 509 pulse and exact 160 ms hold / 120 ms
release edge are retained; the new harness gates the *next* Enter until the
first automatic page transition is observed. It is a bounded diagnostic, not
full historical scheduling parity: the older Sheik-failure report contains no
consumed PAD trace.

Two rebuilt headless Chrome B captures reached natural Results but did not
reproduce the camera failure. Falco won each match. The first no-input gate
stopped at Results frame 516 in phase 2 / `stats_phase=0`; the second withheld
Enter for 60 seconds and remained in that same phase through frame 1789. Both
showed no page/button changes and correct port errors; all seven entry camera
pointers were null. These results establish that a P1 Enter is required to
enter active statistics; they do not implicate camera ownership. Reports and
screenshots are preserved under
`work/pr86-results-keyboard-gated-b-natural-r{2,3}/`.

An earlier fixed-prefix helper required the consumed P1 Start after frame 509.
That assumption was disproved by r4, where the first consumed Start was at 303.
The current helper accepts the consumed initial Start at the actual source
position, then waits for both slot-2/slot-3 page changes before allowing the
next trusted Enter. Offline application to the r7 trace prefix through state
frame 720 passes: initial Start 509–515, stats phase begins at 520, and both
pages change 0→1 at 701 (181 ticks later), with ports `[0,0,-1,-1]`. The full
r7 trace is intentionally not passed to this first-page gate: it also contains
the later 1→2 transition at 1703, which occurs after the point where the
controlled test must send confirmation. A fresh rendered capture of the
adaptive gated path is still required; the historical Sheik-winner failure
remains unresolved.

## Historical integration checks

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

### Keyboard auto-page ordering correction (2026-09-27)

The natural B capture `work/pr86-results-keyboard-gated-b-natural-r4/report.json`
reached a Samus-win Results scene and retained 537/537 source PAD samples. Its
old browser assertion failed because it rejected a second consumed P1 Start
without checking when that Start occurred. Replaying the complete raw trace
through the corrected helper now passes: the initial Start was consumed at
Results source frames 303–315; active statistics began at frame 314; both
disconnected CPU pages changed 0→1 at frame 495 (181 source-frame ticks after
statistics began); and the next Start was consumed at frames 523–528, after
both transitions. The corresponding trusted, non-repeat keyboard keydowns
were observed at frames 303 and 523. All ordinary inputs retained the harness's
160 ms hold / 120 ms release, and raw PAD errors remained `[0,0,-1,-1]`.

The corrected source gate recognizes a consumed Start strictly after both
page transitions as confirmation, while rejecting an extra Start before either
page or between staggered page changes. The active-statistics phase may begin
while the initial 160 ms key hold is still present; that overlap is retained
instead of being misclassified as another confirmation. Focused Node and
Python tests pass (5 Python cases), and offline reprocessing of r4 passes the
source ordering checks. This is not a clean browser-runner pass: r4's report
still correctly records the old harness assertion failure, so a fresh capture
with the corrected harness must complete Results→CSS. Its winner was Samus and
all seven camera-entry pointers were null; it neither reaches a camera guard
failure nor resolves the historical natural Sheik-winner case.

The remaining discriminator is unchanged: obtain a fresh natural slot-2
Sheik-winner Results run with the corrected keyboard/PAD ordering checks and
the camera ownership snapshots. Historical failure at cursor 560 remains
separate evidence; the r4 helper correction is a test fix, not a runtime or
camera-pool fix.

The first fresh run using that helper revision,
`work/pr86-results-keyboard-gated-b-natural-r5/report.json`, exposed a
separate diagnostic false-pass: natural CSS return after three ordinary Enter
pulses was treated as success even though neither disconnected CPU page had
advanced. This Samus-win match did complete the rendered Results→CSS return,
but its 417 retained Results ticks show no page transition; P1 Start was
consumed at 213–218 and again at 395–401. The report's `result=pass` is not
gate evidence and is superseded by this audit. No camera guard fired, and the
entry pointer snapshots remained zero. The browser reported no page or native
command errors and retained 12 timing interruptions; it was a rendered
headless run, not a foreground timing test.

The harness now stops the ordinary keyboard trigger attempts as soon as the
first source-consumed Start appears, sends no more input while waiting for both
CPU pages, and fails if CSS returns before the page gate. It then requires the
next trusted Enter's source cursor to be later than both transitions, retains
keydown/keyup source-frame brackets around the consumed PAD Start, and checks
the 160/120 ms hold/release records. New captures also hash-bind the imported
PAD-trace, keyboard-driver, and headless-browser helper sources. The corrected
offline/Node/Python checks pass. Offline application to the r5 trace through
frame 394 rejects its second Start before either CPU page changed. At the time
this historical checkpoint was written, the adaptive gate had not yet been
exercised in a fresh browser capture; the subsequent clean r7 result is
recorded below.

## Corrected natural keyboard Results gate (2026-09-27)

The corrected browser harness was exercised in
`work/pr86-results-keyboard-gated-b-natural-r7/report.json`. This is a clean
headless Chrome 153 / WebGPU functional pass for one natural four-CPU9 B match
on Final Destination through rendered Results and original CSS return. Samus
(slot 0) won at match frame 14,163 with RNG 1,041,870,253. The renderer reported
an Apple Metal adapter, no page/native-command errors, and 18 timing
interruptions/resumes; these are not foreground timing or performance results.
Its hash-bound Results entry packet,
`work/pr86-results-keyboard-gated-b-natural-r7/results-entry-1-e6e6ca133f579ff6094c1b5e2701495a05314b804155fe2bbd9ad99bfbd7eda9.json`,
records Samus as `ckind=16, ftkind=13` and slot-2 Zelda as
`ckind=18, ftkind=19` at Results entry. Rendered gameplay, Results, auto-page,
and CSS screenshots are retained in the same directory.

The source-observed P1-only ordinary-keyboard route is now covered end to end:
trusted Enter keydown/up brackets were at Results source frames 201/208 and
429/435; P1 Start was consumed at 202–209 and 429–434. Active statistics began
at frame 213, and disconnected slots 2/3 auto-advanced page 0→1 at frame 394,
181 source ticks later and before the second confirmation. Port `err` values
remained `[0,0,-1,-1]`, all other buttons and analog fields stayed neutral, and
all 451 retained Results ticks returned without failure or overflow. Results
returned to original CSS with the prior match/Results owners unloaded. The
entry camera pointer snapshot fields serialized as zero/null and no ownership
guard fired. A later scoped-assets audit found that these serialized values were
the observer's default: Results is constructed in `finish_asset_handoff()`,
which did not copy the camera snapshot. Do not treat r7's null/zero fields as
entry-pointer evidence. The ownership guards were active and did not fire. This
supersedes the earlier claim that only a two-port synthetic test had exercised
the P1-only auto-page route.

This natural control still does not reproduce the historical camera failure.
The old report has a slot-2 Sheik winner at match frame 11,725/RNG
2,897,689,676 and a failure at Results source cursor 560; it records three
160ms/120ms Enter intentions but no trusted event brackets, consumed PAD
samples, port status, Results phase/page state at tick 560, or pointer values.
The new control has a slot-0 Samus winner at frame 14,163/RNG 1,041,870,253,
two trusted Enter pulses, and page transitions at frame 394 followed by
confirmation at 429. Therefore it demonstrates the natural keyboard/page
mechanism and a non-Sheik CSS return, but cannot establish whether the old
Sheik run had reached its CPU page transition by cursor 560 or locate its first
invalid pool ownership boundary.

The preceding corrected-harness retry,
`work/pr86-results-keyboard-gated-b-natural-r6/report.json`, completed a
similar Samus-win Results→CSS flow but is retained as a harness failure because
the final validator referenced the PAD summary before loading it. The complete
raw trace was independently checked offline. The source bookkeeping was fixed
to defer its final pass until the post-return raw trace and trusted input
brackets are both available; the new r7 report passes that final assertion.
The earlier `padtick-r6` folder is a separate, older trace-record-shape failure
and is not promoted to a pass.

Next is a changed unforced natural B sample, preferably two matches in one
fresh session, using the passing `keyboard-gated` harness. Continue until slot 2
finishes as `ckind=18, ftkind=7`; then retain the live phase-labelled guard
message (phase, tick, source/context/owner pool and actor pointers) and compare
entry, auto-page, host handoff, exit and destruction. Until that exact winner
reproduces or otherwise receives a defensible resolution, the historical
camera-pool failure remains open and PR #86 is not ready to merge.

## Scoped Results observer repair and fresh two-match control (2026-09-27)

The failed two-match capture `work/pr86-results-keyboard-gated-b-natural-r8/`
contains the complete first natural Samus-win Results→CSS loop, then a
match-2 observer failure. Its raw Results trace had 683 attempts/rows: rows
0–447 belonged to match 1 (source frames 1–448), while row 448 restarted at
source frame 1 for match 2. The Results trace reset was below
`advance()`'s `scoped_assets` early return, so the disc-backed handoff skipped
it. The source reset and camera-snapshot reset now run before that return; the
trace summarizer rejects any non-increasing source-frame boundary rather than
folding separate Results sessions together. A static regression protects the
reset ordering.

The same audit found that scoped Results construction occurs in
`finish_asset_handoff()`. That path did not copy
`GameplayResultsSession::camera_entry_snapshot()`, so the earlier r7 zero/null
pointer fields (and r8's) were defaults, not captured object ownership. The
scoped constructor now records the snapshot after construction and before the
retained PAD input is released. A source regression covers that ordering.

Fresh headless capture
`work/pr86-results-keyboard-gated-b-natural-r9/report.json` verified the trace
reset: its single match had 1,803 monotonic rows and a nonzero constructed
camera-pool snapshot. It then exposed a separate input-gate flaw. The first
ordinary Enter produced P1 Start at Results frame 198 while the preceding
source state was fade phase 1; the held input lasted through phase 2, but that
phase remained at `stats_phase=0` for the retained 1,803 ticks. The CPU auto
pages therefore never began, and the run correctly failed the gate rather
than claiming a Results pass. Offline evaluation of that trace now rejects the
phase-1 Start explicitly.

The keyboard-gated harness now waits for original Results phase 2 before
dispatching any Enter. On the fresh rebuilt runtime
(development Wasm SHA-256
`41bc87d7a1e09b1a4b54750fed606f4457b2f0d88c71252e83752d2f299fe0f4`),
`work/pr86-results-keyboard-gated-b-natural-r10/report.json` passes two natural
B CPU9, four-stock Final Destination matches through rendered Results→CSS
returns. Both source traces are separate, monotonic 458-row sessions. Phase 2
was observed at source frames 203 and 206; the initial P1 Start runs began at
210 and 213, and disconnected slots 2/3 each changed page 0→1 at frames 402
and 405 (181 ticks after active statistics began). Trusted post-page Enter
confirmation, four-port errors `[0,0,-1,-1]`, all source tick returns, typed
entry packets, original CSS returns, GPU availability, screenshots, diagnostics
and 34 timing interruptions are retained. There were no page/native errors;
headless functional success is not foreground timing or performance evidence.

The first winner was Samus, slot 0, at match frame 14,058. The second was
Yoshi, slot 1, at frame 14,669; slot 2 had naturally transformed to Sheik
(`ckind=18, ftkind=7`) at Results entry but did not win. Both camera snapshots
now capture the constructed pool (`source_pool_after_onenter`, adopted context
pool and owner pool `0xa59e6e0` in this run); no ownership guard fired. This is
a natural Sheik-loser control, not a reproduction or resolution of the
historical slot-2 Sheik-winner failure at cursor 560.

Focused coverage after these changes: runtime target build passed; Node
observer/harness checks passed; `test_results_entry_packet.py` passed 6/6;
`git diff --check` passed. The first Enter is now checked at the phase-2
boundary before waiting for CPU auto-pages. The next browser sample remains an
unforced B run until a typed slot-2 Sheik winner reaches the guarded Results
path. The historical camera failure and first invalid ownership transition
remain unresolved; PR #86 is not ready to merge.

## Natural B controls r11/r12 and input-bracket correction (2026-09-27)

Both captures served the Wasm built from source commit `a3d584f` (SHA-256
`41bc87d7a1e09b1a4b54750fed606f4457b2f0d88c71252e83752d2f299fe0f4`) through
installed headless Chrome 153/WebGPU. Neither reproduces the historical
slot-2 Sheik-winner failure.

- `work/pr86-results-sheik-winner-b-natural-r11/report.json`: one natural
  B-lineup four-CPU9, four-stock Final Destination match. Falco slot 3 won at
  source frame 13,597; slot 2 remained Zelda at Results entry. Results phase 2
  was observed at frame 208, CPU pages changed 0→1 at 407, a trusted post-page
  P1 Start was consumed at 449, and the 471-row trace returned to CSS. Entry
  snapshot retained all seven ownership points; constructed source/context/
  owner pool values matched at `0xa59e6e0`. Sixteen timing interruptions; no
  page/native errors. This is a CSS-return control, not a Sheik-winner test.
- `work/pr86-results-sheik-winner-b-natural-r12/report.json`: the match itself
  and Results→CSS teardown completed, with Samus slot 0 winning at source frame
  15,222. Slot 2 transformed to Sheik during gameplay, then back to Zelda at
  frame 14,746 before Results. The 954-row Results trace records CPU pages
  changing at frame 493; trusted Enter attempts at frames 520, 615, 720, and
  827 had no consumed P1 Start, while the attempt bracketed by frames 931–938
  contains the actual source Start at frame 932. At cursor 560 P1 was neutral,
  Results was in phase 3 / statistics phase 2, disconnected pages were `[1,1]`,
  and no player was confirmed; the old failing report lacks these fields. All
  source ticks returned; port errors remained `[0,0,-1,-1]`; the camera
  source/context/owner snapshots
  matched at `0xa59e6e0`; 44 timing interruptions; no page/native errors. The
  report is marked failed because the old final assertion required the *first*
  dispatched post-page pulse to be consumed, though the trace and screenshots
  show a later trusted pulse was consumed and CSS teardown completed. Do not
  call the pre-fix report a harness pass.

The keyboard-gated validator now correlates every retained post-page keydown/
keyup bracket to raw source PAD and accepts only a bracket that actually
contains a consumed P1 Start strictly after both page transitions. It retains
all no-op attempts and the accepted attempt index; it does not turn a dispatched
key into a source input. A focused Node regression models r12's four unconsumed
attempts followed by the consumed fifth, and rejects missing/pre-boundary
starts. Running the helper against the archived r12 trace finds accepted
attempt 4 at frame 932. Node syntax and trace tests, `test_results_entry_packet.py`
(6/6), and `git diff --check` pass. No browser rerun or runtime build has yet
validated the updated harness; its acceptance logic is checked offline against
the retained trace.

After two further non-target natural samples, the next experiment changed to
the tracked Match-history reducer rather than another long replay. The current
branch's `gameplay_results_scene_trace` target rebuilt successfully from
`a3d584f` (JS SHA-256
`31875fe8cba542c56269494e583075c0bf44a532e103c9dccebced3bcbf8af83`, Wasm
`6f3cd77ca0332c360ea5f6eb4e0b7e138b8d0f03c0e0497b0a54220ed928f6e4`). The
focused `test_source_pad_b_match_to_sheik_results` passed in 2.172s; retained
command/stdout/stderr are under
`work/results-match-history-tests/native-gj5wrf8z/`. This current-branch case
uses four controlled human PAD streams but advances an actual source Match:
slot 2 performs down-B at frame 44, the source reports a Zelda-origin Sheik
winner at frame 2,834 with losses `4,4,0,4`, and Results constructs the real
winner demo. Camera pool stayed `0x7090540` through 732 Results frames, scene
exit, mode OnExit, host unload and session teardown. The profile selected
Prize 192, not CSS; no GPU draw, CPU9, natural-match, or reference claim.

With the same rebuilt target, focused
`test_results_camera_ownership.py` passed 4/4 in 10.326s, including the three
source CopyPAD B/Y/X winner-demo variants, mode-exit/profile/camera guard
faults, and non-repeatable callback checks. Those variants use controlled
Results standings and are native-state-only; they are not natural-match
reproductions. The earlier frozen
`work/pr86-results-match-history-rendered-d6MV2r/HANDOFF.md` (commit `4ea6d46`)
remains historical and its unbuilt four-slot-HUD correction is not evidence;
the run above is the current-branch refresh of the bounded source Match path.

The reducer shows that a source-generated Zelda-origin Sheik winner can pass
current Match→Results ownership/close, but it does not reproduce the natural
four-CPU9 cursor-560 failure. That historical report still lacks raw Results
PAD, phase/page state, and all camera pointer snapshots. The first invalid
ownership transition remains unknown; do not claim this issue resolved or PR
#86 ready to merge. The next useful discriminator must retain the exact
natural-winner route or identify a source difference between it and the
controlled Match-history case before another long capture.

## Results cursor-560 input-path reduction (2026-09-27)

The fresh current-build source control
`work/results-match-history-tests/p1-statistics-vxac9bsr/` passes the specific
automatic-page-before-confirmation assertion: P1-only 10-source-tick Start
holds at ticks 180, 360 and 600; connected ports 0/1 remain neutral while
disconnected CPU ports 2/3 auto-advance 0→1 at tick 551. The final confirmation
edge is consumed at 600, the host exits at 621, and the original pool remains
`0x70903c0`. This is synthetic Results standings with chosen source ticks and
native state only. It proves that controlled P1 input plus CPU auto-page and
host close works; it is not the natural CPU9 input schedule.

Added `keyboard-three-prefix` to the tracked browser harness. This one-match
mode sends exactly three trusted ordinary Enter down/up pairs, preserving the
historical 160ms/120ms requests, waits for Results source cursor 560, and
retains the event source-frame brackets plus the raw PAD/state sample before
resuming ordinary input for the natural CSS return. It does not infer consumed
ticks from wall time and explicitly does not claim to replay the historical
PAD stream.

Fresh headless Chrome 153/WebGPU run
`work/pr86-results-sheik-winner-b-three-pulse-prefix-r1/report.json` passes a
natural B CPU9, four-stock Final Destination match and Results→CSS teardown.
Samus wins at gameplay frame 13,677 (RNG 3,068,933,094), so this is not the
Sheik-winner target. Through Results cursor 560, the three trusted Enter pairs
were bracketed at source frames 192/192, 292/294 and 388/388; none contained a
source-consumed P1 Start. The 561 retained source samples all returned without
overflow. At cursor 560, Results was still in internal phase 2 / statistics
phase 0, all four pages were 0, and P1/P2 were neutral; port errors were
`[0,0,-1,-1]`. The first later consumed P1 Start was at source frame 765;
disconnected CPU pages advanced at 957, after statistics began. The full trace
retains 2,932 Results source ticks and the run returned to CSS with no
page/native errors. The cursor-560 canvas screenshot was visually inspected.
There were 143 Results timing interruptions; this headless functional run is
not foreground timing or performance evidence.

This differs from r13's standard 48-pulse loop, which had a P1 Start run at
305–311 and both CPU pages advance at 497 before cursor 560. Together, the two
current natural controls show that ordinary 160/120ms Enter requests do not
map to a fixed source PAD schedule: r13's first three events include a
consumed Start before the page transition, while the exact-three prefix above
has none through cursor 560. The historical failed packet still records only
three input intentions and lacks their event brackets, consumed PAD/trigger/
release bytes, port status, Results page state and camera pointers. Therefore
neither current Samus control establishes which input path the old Sheik
failure actually consumed or identifies its first invalid ownership
transition. The camera-pool failure remains unresolved; PR #86 is not ready to
merge.

Harness syntax, the focused prefix-contract test, and `git diff --check` pass.
The captured browser report binds the run to clean runtime Wasm SHA-256
`41bc87d7a1e09b1a4b54750fed606f4457b2f0d88c71252e83752d2f299fe0f4` and the
new harness SHA recorded in the report. No runtime source changed for this
reduction. The next natural attempt must use this three-pulse prefix and retain
the typed winner/form; do not treat another non-Sheik winner as a resolution.

## Results natural-CPU9 auto-page reducer and continuation failure (2026-09-27)

The new source-side reducer
`work/results-match-history-tests/native-ytbc6aex/stdout.log` runs the actual
four-CPU9 Stock match to its natural terminal and passes its real CPU-typed
`MatchExitInfo` through Results. It makes no forced seed, form, terminal or
winner assignment. Its P1-only PAD schedule uses separate 10-tick Start holds
at source ticks 180, 360 and 600, with explicit release edges at 190, 370 and
610. Ports 0/1 remain connected and neutral between P1 edges; CPU ports 2/3
remain disconnected. Their page 0→1 auto-transitions occur at tick 551, before
the confirmation trigger at 600. Results host close at tick 622 preserves
camera pool `0x7090720`. This is native state-only Results coverage with the
default CSS/save host subset, not browser/GPU or retail equivalence; the natural
winner was Samus, not the slot-2 Zelda-origin Sheik target.

Timing scope matters: the reducer's confirmation edge is at Results source
tick 600, while the historical camera-pool report stops at tick 560. It proves
both disconnected CPU pages advance before the chosen confirmation, but it
does not reproduce the historical failure boundary or establish the old
browser's consumed Enter/PAD schedule. The frozen failure still lacks that raw
PAD trace and its page/pointer snapshots.

Browser run
`work/pr86-results-sheik-winner-b-three-pulse-two-matches-r2/report.json`
reached the cursor-560 checkpoint in a natural B CPU9/4-stock/FD match. Samus
won at gameplay frame 17,740 (RNG 143,578,268). The three trusted ordinary
Enter pairs were bracketed at Results frames 186/186, 287/296 and 394/394;
source PAD shows the second Start at frames 288–295, while the previous source
state is Results phase 2. CPU slots 2/3 auto-advanced their first page at
frame 480. At cursor 560, Results is in phase 3/statistics phase 2, pages are
`[0,0,1,1]`, no player is confirmed, PAD is neutral, and port errors are
`[0,0,-1,-1]`. All camera-entry pool observations are `0xa59e6e0`; no ownership
guard fired. This excludes only a generic failure for this non-target
winner/input route and does not resolve the historical Sheik-winner failure.

That browser attempt failed later, before CSS return: after cursor 560 it
retained only the original Start PAD run despite dozens of later trusted Enter
events; the final sample was still Results frame 4,786. The report records 251
timing interruptions and no source tick failures. Investigation found the
continuation helper could pass a stale pre-pause state into its resume check
and dispatch keyboard input without confirming source advancement. The tracked
harness now reads fresh diagnostics before continuation and gates post-prefix
keyboard input on an advancing source frame; the first three historical-prefix
attempts remain unchanged. Syntax and focused harness-contract tests pass, but
this correction still needs a fresh rendered-browser run after the shared lane
is released. The failed report is preserved and is not counted as a completed
Results→CSS loop.

## Corrected continuation rerun and retained natural Sheik control (2026-09-27)

The corrected one-match headless Chrome run is retained at
`work/pr86-results-sheik-winner-b-three-pulse-single-match-r3/report.json`.
It serves Wasm SHA-256
`41bc87d7a1e09b1a4b54750fed606f4457b2f0d88c71252e83752d2f299fe0f4`
and completes a natural B CPU9/four-stock/Final Destination match and
Results→CSS return. Samus slot 0 wins at gameplay frame 12,552 (RNG
2,414,528,033); this is not the target winner. The first three trusted Enter
keydown/up brackets are source frames 221/230, 328/329 and 425/425. Only the
first interval contains a source-consumed P1 Start (frame 224). Disconnected
CPU slots 2/3 advance page 0→1 at Results frame 416 before confirmation.

After tick 560, source state is Results phase 3/statistics phase 2, pages
`[0,0,1,1]`, all unconfirmed, P1 neutral, and port errors `[0,0,-1,-1]`.
All 561 source tick attempts through this checkpoint return without overflow
or failure. The entry/adoption camera-pool pointers all equal `0xa59e6e0`;
no tick/draw/exit/close ownership guard, page error or native error fires. The
corrected continuation reads fresh diagnostics, waits for source advancement,
then the ordinary keyboard path reaches CSS. The run records 29 timing
interruptions; this is functional headless evidence only. It validates the
harness freshness fix and a non-target route, not the historical Sheik-winner
input schedule.

The retained source-tick B control
`work/pr86-results-padtick-b-natural-r6/report.json` is useful but older and
must stay separately scoped. It records a B CPU9/4-stock/Final Destination
slot-2 win at frame 14,204 (RNG 1,088,324,269). Slot 2 transformed between
form kinds 19 and 7, then returned to kind 19 at frame 13,364 and remained
there at the win; this is a Zelda-form win after in-match Sheik transformations,
not a Sheik-form winner. At Results cursor 560, P1 is neutral, pages are
`[0,0,1,1]`, and nobody is confirmed. Disconnected CPU pages advanced at frame
397. Its P1-only source-tick confirmation begins at frame 609; all 631 PAD
ticks return, and the browser reaches CSS with Results/world ownership
unloaded and no page or native error. Its served Wasm is the earlier
`d1d5c26` build, SHA-256
`0e288debdc4fb4c3a75d64c0bf0bb7420ff726d8ebbdac6b04b9a9f537310529`; that
source already had the phase-labelled camera-pool guards. Between `d1d5c26`
and current `a3d584f`, the Results source changes only add entry-pointer
snapshots. However, r6 used injected source-tick PAD, not the historical
ordinary keyboard path; its report is marked failed by a post-CSS serializer
error (`Cannot read properties of undefined (reading 'filter')`) and has no
camera pointer snapshot. Treat it as a non-target Zelda-form winner under a
different input path, not as an exact replay or clean harness pass.

The source-tick auto-page control and r6 do not reproduce the historical camera
failure, but r6 is not a Sheik-form win and neither report matches the
historical keyboard path. The frozen failure report still contains only
three Enter intentions and the generic late `camera release` failure: no
consumed PAD brackets, page state or pointer values. The first invalid
historical pointer transition remains unknown. Next discriminator is a fresh
current-Wasm keyboard-prefix run with a natural slot-2 Zelda-origin Sheik win;
do not seed or force the winner. PR #86 remains open and not ready to merge.

## Current merged-main B Results control (2026-09-27)

The fresh two-match headless Chrome control at
`work/pr86-current-d1c519d-b-keyboard-gated-two-match-r1/report.json` was
produced from clean source commit `d1c519d`. The served Wasm is SHA-256
`c6851edeb7a48355fc164d9ca86b7d663a0f00b5537deb2c1e3e653069a3ce04`.
Both natural lineup-B winners were Samus: match 1 frame 15,655/RNG
1,289,946,642; match 2 frame 11,864/RNG 2,921,327,661. Both rendered Results
loops returned to original CSS and entered the next match.

With ordinary trusted P1 Enter (160ms held/120ms released), the current source
trace records initial P1 Start at frames 208–217 and 207–216; disconnected CPU
pages 2/3 advance at frames 400 and 399, respectively, before the ordinary
confirmation at 418 and 423. Port errors remain `[0,0,-1,-1]`; all 440/445
Results ticks return; no camera-owner failure occurs. This is a current
non-target functional control. It does not reproduce the old three-pulse
cursor-560 schedule or the natural Sheik-winner condition.

The next current-build comparison uses the older r6 `source-tick` input mode,
which previously produced a natural Zelda-origin Sheik win and captured the
auto-page/confirmation path. It remains explicitly distinct from keyboard
input and the chosen 180/360/600 native Results reducer. The existing #86
instrumented context will retain camera entry/adoption pointers and the first
changed source boundary if that natural outcome recurs.

The first current-build source-tick attempt is retained at
`work/pr86-current-d1c519d-b-source-tick-single-match-r1/report.json`. It uses
the same served Wasm SHA as the keyboard-gated control and naturally ends with
Samus at frame 13,143/RNG 1,008,858,508. P1-only source Start runs are 208–217
and 610–619; CPU pages 2/3 auto-advance at Results frame 400 before the
confirmation sample at 610. All 632 Results ticks return, ports remain
`[0,0,-1,-1]`, CSS is live at the end, and the Results camera-entry/adoption
pointers are all `0xa48ef20`; no camera ownership guard fires. It is not the
Sheik-winner condition.

The browser harness nevertheless exits nonzero after the CSS return because
its source-tick summary dereferenced
`result.results_page_transition_checks` instead of the report-level array. The
captured report is retained as a serializer failure, not a clean scenario pass.
The tracked fix reads from the correct report owner, and the focused Results
packet/harness contract suite passes 8/8, including a regression against the
wrong lookup. This fixes reporting only; a fresh browser rerun is still needed
for a clean harness receipt, and the historical camera-pool cause remains
unknown.

## Current source-tick verifier boundary (2026-09-27)

The fresh current-branch two-match attempt is retained at
`work/pr86-current-8d61f5d-b-source-tick-two-match-r1/report.json`. Match 1
naturally ended with Falco at gameplay frame 13,284 (RNG 621,935,433), reached
Results and returned to original CSS. Its 632/632 Results source ticks returned;
disconnected CPU slots 2/3 changed page 0→1 at source frame 398 in phase 3,
statistics phase 2. The consumed P1-only Start confirmation run was 610–619;
port errors stayed `[0,0,-1,-1]`, and all Results camera-entry/adoption pointers
were `0xa48ef20`. No camera/page/native error occurred.

The report is still marked `fail` because final harness validation read
`confirmation_source_frame` from a source-tick gate record that only retained
the queue target. The raw source PAD trace proves the actual confirmation frame
is 610 and the page gate at 398 precedes it; this is a verifier false negative,
not a game failure. A new helper now binds the queued target to the consumed P1
Start run and records that observed frame. Regression checks, Node syntax and
the focused Results packet tests pass (8/8). The two-match browser run stopped
after match 1; a fresh rendered rerun is needed to validate the fixed verifier.
Falco is not the target slot-2 Zelda-origin Sheik winner, and the historical
camera-pool failure remains unexplained.

## Clean-current-head B browser control (2026-09-27)

One fresh, rendered, installed-headless-Chrome B run was produced from clean
source commit `a116abc7c5a38779e2e2cf8f672869f8bab000d8` (tree
`7c795b841d1c54e37a5b7a46d7d2377215c3fba5`). The Release runtime built from
that tree has Wasm SHA-256
`eb2d2808050184a4fcedc3cb65be72cdd2a5e64818f0bb4c7ec32350ca4e5a56a` and JS
SHA-256 `62bf2d142fa0aaf8d7642cc2fbba39bf80c43355461d182de0eb9b2853f7f89a`.
The run verified the live CSS lineup as Samus/Yoshi/Zelda/Falco, all CPU9,
selected Final Destination, naturally reached Results, and returned to the
original CSS. Samus won at match frame 17,344 (RNG 1,054,490,748). It is one
functional match, not the requested two-match loop or original comparison.

Receipt: `work/pr86-results-current-head-b-three-prefix-r1/report.json`;
the Results entry packet and all CSS/SSS/stage/gameplay/Results/return
screenshots are in the same directory. The report passed with no browser/page
or native command errors, no timing interruptions, and retained the CSS,
canvas, GPU and source diagnostics. Its scenario scope is rendered functional
behavior; it does not claim pixel equivalence, foreground timing, physical
controller acceptance, or performance.

The `keyboard-three-prefix` continuation sent three trusted ordinary Enter
keydown/up pairs, bracketed at Results source frames 193–203, 364–374 and
535–545. None was consumed as P1 Start in the first 561 Results source rows;
P1 Start runs were empty. Port errors remained `[0,0,-1,-1]`; disconnected CPU
slots 2/3 automatically advanced statistics page 0→1 at source frame 396,
before the observation target 560. The target snapshot is after source tick
560 (`source_frame=561`), and the camera pool stayed `0xb21b940` across source
OnEnter, collision adoption, and the context/owner snapshots. No ownership
guard fired through the observation and CSS return.

The historical report does retain three P1 Enter intentions at 160 ms held /
120 ms released, matching this control's intended pulse count and durations.
It does not retain their Results source-frame brackets, consumed PAD rows,
page state, or connected-port state. This control records event brackets
193–203, 364–374 and 535–545, and confirms none was consumed as P1 Start before
the observation. So it excludes a generic camera-pool failure for this
current-head Samus-winner route, but cannot establish that the historical
browser consumed equivalent button edges or had equivalent scheduling. The
historical runner checkout `607ff56` checked camera ownership only when
`release_source_camera_and_ground()` ran during Results teardown, not at each
tick/draw boundary. The report records that checkout and the served Wasm hash,
but the frozen build manifest contains no source commit. Cursor 560 is therefore
the late detection point, not evidence that the pointer changed at that frame.
The first invalid historical
ownership transition remains unknown. Next discriminator is another naturally
reached Sheik-winner B Results path with first-change camera snapshots; do not
call this control a resolution or equate the two incomplete input records.

## Natural CPU9 Results auto-page reducer on reconciled PR source (2026-09-27)

The focused `--lineup-b-cpu9-match-history-host-state` reducer was rebuilt and
rerun after correcting its observer to receive the completed source-frame
count. Passing receipt: `work/results-match-history-tests/native-dai7mh1u/`.
`command.json` binds the run to PR source `7d9170f` plus the exact one-line
tracked diff (SHA-256 `accdc785d9e3d0a062ac5d6167e9c9f282f8194b9cf404268e750a882807b2a9`),
trace JS `5d4a874466ddbbea1936fe250ac60c7a74d64c9f96df34deb657c16c80b751b4`,
and trace Wasm `03e9ea896eb8ee64f24fee4e722921057b4d6fd99ed9036aee76bb3cb9e4764e`.
The retained failed first attempt is separate at
`work/results-match-history-tests/native-4kqexyux/`; it reached a natural
terminal but the test driver passed a zero-based loop index to a completed-frame
observer, so it failed before consuming Results input. It is not a game or
camera-pool failure.

The passing run executed a natural four-CPU9, four-stock B match with no forced
seed, winner, form or terminal data. Samus (slot 0) won at source frame 13,236;
the match exited with seed 1,239,287,788. The state-only Results trace consumed
P1 Start press edges at source frames 180, 360 and 600, each held for ten source
ticks with neutral releases at 190, 370 and 610. Ports 0/1 remained connected;
CPU ports 2/3 remained disconnected. Their statistics pages each advanced
0→1 at source frame 551, before the frame-600 confirmation. Results closed at
frame 622, and camera-pool pointer `0x81e5fe0` remained unchanged through the
host OnExit/route-commit check. It made no source draw calls and did not continue
through rendered CSS.

This excludes a generic failure in the natural CPU9 `MatchExitInfo` → Results
entry → disconnected-CPU auto-page → later P1 confirmation → native host-close
path for this Samus-winner state-only case. It does not test the target natural
slot-2 Zelda-origin Sheik winner, draw/browser lifetime, or CSS continuation.
The historical failure packet records only three intended P1 Enter presses
(160 ms down / 120 ms up); it has no event-to-source-frame brackets, consumed
PAD edges/releases, port state, Results pages or camera pointers. This reducer
uses controlled source-tick PAD samples rather than literal keyboard events,
so it is a useful input/auto-page discriminator, not a reconstruction of the
historical consumed schedule. The first invalid ownership transition in the
historical cursor-560 camera failure remains unknown.

The next experiment proposed here was completed on `ab798ed`; its current-head
rendered result and continuing Sheik-winner limitation are recorded below.

## Reconciled-head rendered B source-tick loop (2026-09-27)

The current-head rendered functional receipt is
`work/pr86-ab798ed-b-source-tick-three-pulse-current-head-r1/report.json`;
screenshots, progress, consumed-PAD/page traces and hash-bound Results-entry
packets are retained beside it. It binds to clean source `ab798ed` and records
two natural four-CPU9/four-stock B matches, each completing Results→original
CSS→next match. Slot 2 repeatedly transformed Zelda↔Sheik, but Samus and Falco
won; neither match is the historical Sheik-winner target. The source-tick
P1-only Results continuation observed both disconnected CPU page transitions
before confirmation. The entry pointer snapshot shows the source OnEnter pool
creation and stable adoption; no camera guard fired.

This is rendered browser functional evidence, not retail simulation or pixel
equivalence, and its controlled development PAD schedule is not the historical
keyboard replay. The historical natural slot-2 Sheik-winner camera failure
remains unresolved; its first invalid transition is unknown. Do not claim this
non-target control resolves it.

## Current-head source-tick Results reducer (2026-09-28)

Built `gameplay_results_scene_trace` from clean PR HEAD
`978b9f43f4335046de44bb1689d09641e6f1dd38` and ran the single opt-in test
`test_natural_cpu9_terminal_flows_through_p1_cpu_auto_pages`. Passing receipt:
`work/results-match-history-tests/native-_pca52qm/` (`command.json`,
`stdout.log`, `stderr.log`). The trace JS/Wasm hashes are
`5d4a874466ddbbea1936fe250ac60c7a74d64c9f96df34deb657c16c80b751b4` /
`03e9ea896eb8ee64f24fee4e722921057b4d6fd99ed9036aee76bb3cb9e4764e`; the
tracked tree was clean.

The reducer ran a natural four-CPU9, four-stock Lineup B Stock match into its
actual typed `MatchExitInfo` and source Results session, with no forced seed,
winner, form or terminal state. Samus (slot 0, `ckind=16`, `ftkind=13`) won at
match frame 13,236. P1-only Start edges were held for ten source ticks at 180,
360 and 600 and released at 190, 370 and 610; ports 0/1 stayed connected and
neutral except P1's edges, while CPU ports 2/3 stayed disconnected. Both CPU
statistics pages transitioned 0→1 at Results source frame 551 (visible after
tick 552), before P1 confirmation at frame 600. Results completed at frame
622. Camera pool `0x81e5fe0` remained stable through each Results tick/audio
boundary and host OnExit/route commit; close, guarded owner destruction and
session teardown completed. This excludes a generic natural-CPU9
entry→auto-page→P1-confirm→native-host-close failure for the Samus standing.
It is native state-only: no source draw calls, rendered browser, CSS return or
retail comparison.

Comparison with the retained historical failure is limited by missing data.
`work/pr86-final-frozen-b-two-match-r1/report.json` records checkout
`607ff564`, served Wasm SHA-256 `96592b3f…`, a natural slot-2 Zelda-origin
Sheik win at frame 11,725, match-frame-0 RNG `2688366096`, and three intended
browser Enter pulses (160 ms down / 120 ms release). The frozen Wasm still
matches its recorded hash, but its build manifest contains no source commit.
It detects “Original Results camera pool ownership changed” at the late
Results cursor 560. It does not
retain key event/source-frame brackets, consumed PAD edges/releases, CPU page
state, connected-port bytes, or entry pointers; the cursor is not the first
invalid pointer transition. The current reducer has exact source-tick PAD
edges and topology but a Samus winner and no drawing. The current-head rendered
Sheik-winner source-tick receipt at `0c9b21a` complements it, but is not a
keyboard replay. The exact historical keyboard/Sheik combination therefore
remains open.

The r12 keyboard report is useful only as a separate observer correction.
`work/pr86-results-sheik-winner-b-natural-r12/report.json` is from older source
`a3d584f` and naturally ended with Samus. Its raw trace shows CPU pages 2/3
transitioning at frame 493, then four post-page trusted Enter attempts not
consumed by source; the fifth bracket (keydown 931, keyup 938) contains P1
Start at frame 932. Reapplying the current correlation helper to the archived
trace accepts that fifth attempt and preserves the four no-ops. The old report
remains marked failed because its then-current assertion required the first
post-page dispatch to be consumed; this derived check is not a fresh browser
pass and does not reproduce the historical Sheik-winner failure.

## Current-head ordinary-Enter Results prefix (2026-09-28)

The bounded headless Chrome run `work/pr86-ccca20d-b-keyboard-one-match-r1/report.json`
uses current PR HEAD `ccca20d`, the existing hash-identified Release runtime,
and the live CSS→SSS setup. It is one natural four-CPU9/four-stock Lineup B
match; no winner or seed was forced. Samus (slot 0) won at frame 14,490.
Zelda-origin Sheik in slot 2 repeatedly transformed, but was not the winner.
This is functional gameplay and Results-input evidence, not original-game
comparison or timing evidence.

The three ordinary focused Enter pulses requested 160 ms down/120 ms release.
Trusted non-repeat brackets were source frames 193–203, 365–375 and 536–546;
source consumed Start at 193–202, 365–374 and 536–545. Connectedness stayed
`[0,0,-1,-1]`. Results returned naturally to CSS at source frame 557 before
the harness's cursor-560 prefix checkpoint; internal Results phase was 4 and
all four pages remained at 0. Every retained source tick returned, the source
trace did not overflow, and no native/page/camera ownership error occurred.
The harness therefore reports its prefix-check assertion failure because CSS
already returned; this is not a gameplay crash. The camera-entry packet records
pool `0` before Results OnEnter and `0xb21b940` after OnEnter/adoption. The
runtime ownership guards passed through this route.

This fresh Samus-winner keyboard case still does not explain the historical
natural Sheik-winner error, whose first invalid pointer transition is unknown.
The current-head source-tick Sheik pass and native Samus auto-page pass remain
separate controls. The next discriminating run must obtain a natural slot-2
Sheik winner under ordinary keyboard input and retain its first camera-pointer
change, page state, consumed PAD edges and close boundary; no seed or winner is
to be forced.

## Current-head source-tick Results reducer rerun and guard audit (2026-09-28)

After the ordinary-Enter prefix returned to CSS too early, the opt-in native
reducer was rerun against PR source HEAD `ccca20d` without rebuilding its
unchanged trace artifacts. Passing receipt:
`work/results-match-history-tests/native-wkthgua3/`. `command.json` records
trace JavaScript/Wasm hashes `5d4a874466ddbbea1936fe250ac60c7a74d64c9f96df34deb657c16c80b751b4` /
`03e9ea896eb8ee64f24fee4e722921057b4d6fd99ed9036aee76bb3cb9e4764e` and the
documentation-only tracked diff present during the run.

This natural four-CPU9, four-stock Lineup B match produced a typed source
`MatchExitInfo`; Samus (slot 0, `ckind=16`, `ftkind=13`) won at source frame
13,236. It used P1-only ten-source-tick Start edges at 180, 360 and 600, with
releases at 190, 370 and 610. Ports 0/1 stayed connected, CPU ports 2/3
disconnected. The disconnected CPU pages advanced 0→1 at source frame 551
(visible after tick 552), before the frame-600 confirmation. Results closed at
frame 622 with camera pool `0x81e60a0` unchanged; owner destruction and guarded
teardown passed. This is a fresh native-state-only confirmation of the
auto-page gate, not a rendered keyboard replay and not the historical Sheik
winner.

The guard-source audit clarifies why the historical cursor cannot identify the
first bad transition. At runner source `607ff56`, the camera-pool equality
check was only performed by `release_source_camera_and_ground()` during
teardown. It did not sample Results entry, tick/input/audio, source step, draw,
scene OnExit or intermediate object destructors. Current code records the
entry pool before/after `gm_Scene_Results_OnEnter` and collision adoption, then
checks those source boundaries and each teardown destructor. Thus the old
`Original Results camera pool ownership changed` report is a late release
detection; it does not establish that the pointer first changed at cursor 560.

In the decomp source, the pool is allocated by `Camera_80028B9C`; Results calls
it from `fn_8017AA78()` during `gm_Scene_Results_OnEnter`. No separate
statistics-page camera initialization is visible in the source call graph.
This narrows the writer hypothesis but does not identify it: the native pass is
a Samus winner with no source draws, whereas the historical failure is a
browser-rendered Sheik winner with unbracketed keyboard input and no retained
entry/page/PAD/pointer snapshots. The current-head keyboard-gated control is
recorded below; the next discriminator is the historical three-pulse keyboard
prefix, with no forced seed or winner.

## Fresh merged-head rendered Results control (2026-09-28)

The current branch is `4924b28281ab7902ea9d6866ed3e596cf332b547`, merging
`origin/main` `d22b50f83c1e7acff9a23bd274b210515935bea4`; PR #89 commit
`aecc3e64def3090bdfc33d0eb66058df998ef14d` and PR #93 merge
`2e083bf1367ca8900874a11ac7566433cfdf37a1` are ancestors. The Release runtime
and both affected native trace targets were freshly built on this tree. Focused
post-merge gates pass: runtime owner 14/14, runtime-scope/Results-entry 17/17,
and natural Results plus native CSS teardown 2/2. These are not the required
full suite.

`work/pr86-4924b28-b-keyboard-gated-two-match-r2/report.json` binds the
headless Chrome 153 capture to clean source commit
`4924b28281ab7902ea9d6866ed3e596cf332b547`, tree
`c13818c52dc06963c5f373a4a36b0a4fa683b686`, runtime JS SHA-256
`62bf2d142fa0aaf8d7642cc2fbba39bf80c43355461d182de0eb9b2853f7f89a`, and
Wasm SHA-256
`66aed80ba10cb05a94c4d2d5d53529877b32b946684104cc52a6944a3f1cc3dd`. It
renders two natural B matches with Samus, Yoshi, Zelda/Sheik and Falco all
CPU9, four stocks, Final Destination. Samus won match 1 at frame 14,747;
Falco won match 2 at frame 13,895. Slot 2 transformed Zelda↔Sheik in both.
Match 1 Results returned to original CSS and launched match 2; match 2 Results
returned to CSS. Screenshots, source progress and full Results source PAD/page
traces are retained in that output directory/report.

This `keyboard-gated` case intentionally uses two trusted P1 Enter pulses per
Results visit, each requested as 160 ms down / 120 ms release. Ports 0/1 were
connected, ports 2/3 disconnected. The automatic CPU-page transitions were
observed before the separate confirmation: slots 2/3 advanced 0→1 after 181
source ticks at Results frame 401, then P1 Start was consumed at 421 for match
1; match 2 transitioned at 397 and consumed Start at 416. Entry snapshots
record pool `0` before Results OnEnter and `0xb21b940` after OnEnter, collision
adoption and context ownership. No retained camera guard failed through all
443/438 Results ticks, rendered draws, route returns and owner teardown; no
browser page error, native command error or source-timing interruption was
recorded. The screenshots' callback/audio diagnostics still do not establish
foreground timing, performance or PCM acceptance.

This excludes a missing disconnected-CPU auto-page and a non-Sheik rendered
Results-close failure on the merged baseline. It is not the historical input
path: the historical report retains three Enter intentions, and this gated
control retains two. Neither match ended with slot-2 Sheik, so the historical
camera-pool failure and its first invalid ownership transition remain
unresolved. The exact three-pulse merged-head rerun is recorded next.

## Merged-head historical three-pulse prefix control (2026-09-28)

`work/pr86-4924b28-b-keyboard-three-prefix-r1/report.json` uses the same
merged-head Release Wasm as the gated run above. Its source commit is
`4924b28`; provenance also records a docs-only tracked diff fingerprint and
confirms the tree did not change during capture. One natural B CPU9/four-stock
Final Destination match ended with Samus at frame 13,613. Zelda-origin slot 2
transformed repeatedly but did not win.

The three trusted, non-repeat Enter presses requested 160 ms down/120 ms up.
Source-consumed P1 Start runs were 192–201, 363–372 and 535–544; releases were
202, 373 and 545. Port errors remained `[0,0,-1,-1]`, and no CPU-page
transition occurred. Results naturally returned to CSS at source frame 557
(phase 4, all four pages still zero), after 557 returned pre-tick samples at
frames 0–556 and before the harness's requested cursor-560 checkpoint. It
therefore reports a failed prefix assertion, not a runtime failure. Every
retained tick has its consumed-PAD/page state; no camera guard, native-command,
page or source-timing error occurred. Camera-entry ownership was `0` before OnEnter
and `0xb21b940` after OnEnter/adoption/context ownership. The report retains
the failure screenshot, entry packet and complete partial PAD/page trace.

This current-baseline run now binds the three-pulse timing class and
connectedness that the older historical report omitted, but it remains a
Samus-winner control and cannot clear the natural Sheik-winner failure. The
ordinary-keyboard three-pulse control on `ccca20d` had the same natural CSS
return at frame 557. To avoid repeating this boundary unchanged, the next
discriminator is a current-head rendered B case using the distinct
`source-tick-three-pulse` Results schedule (P1-only source-tick edges at 180,
360 and 600; both disconnected CPU pages must advance before tick 600). If
slot-2 Sheik naturally wins, this tests whether the merged camera guards
tolerate that Results identity independently of keyboard scheduling; it is
not presented as a replay of the historical keyboard path. If it does not
naturally produce Sheik, retain that control and seek a bounded review with the
old failure packet and both current input traces instead of looping identical
matches.

## Strict merged-head P1-Enter Results discriminator (2026-09-28)

`work/pr86-4924b28-b-keyboard-gated-p1-enter-match-r1/report.json` is a fresh
headless Chrome 153 rendered Lineup B match on source HEAD `4924b28`, with the
same Release Wasm SHA-256
`66aed80ba10cb05a94c4d2d5d53529877b32b946684104cc52a6944a3f1cc3dd`. All four
fighters were verified CPU9, four stocks, Final Destination. Samus (slot 0)
won naturally at match frame 13,752; the target slot-2 Zelda-origin Sheik did
not win.

This stricter `keyboard-gated-p1-enter` mode preserves the split keyboard
ports used by original CSS setup (ports 0/1 connected, CPU ports 2/3
disconnected) and verifies that Enter only produces P1 Start; P2 remains
neutral. Trusted non-repeat Enter keydown/up retained 160 ms down / 120 ms
release. P1 Start was consumed at Results source frames 207–216. Statistics
phase began at 218; CPU pages 2/3 each advanced 0→1 at frame 399 (181 source
ticks) before the next Enter keydown at 419 and consumed P1 Start at 419–428.
All retained source ticks returned without overflow or failure, and Results
naturally returned to CSS.

The camera pool was `0` before Results OnEnter and `0xb21b940` after OnEnter,
collision adoption and context ownership. The entry/tick/draw/OnExit/close/
destructor guards remained green. There were no browser page/target crashes,
JS errors, native-command errors, or source-timing interruptions. This
excludes a missing CPU auto-page for the ordinary P1-Enter path and a generic
Samus Results-close failure on this merged build. It does not resolve the
historical Sheik-specific camera failure: the winner and match frame differ.
The old report is hash-bound to its served Wasm, but it retains no
event/PAD/page/camera snapshots, and its build manifest does not bind that
Wasm to a source commit.

The task description's “P1-only” is treated as P1-only Enter/Start, not a
P1-only controller-port profile. A port-disabled experiment broke original
CSS's P2 Yoshi-door input and is retained only as a rejected profile
interpretation. The bounded, read-only review request and comparison packet
are in the ignored work note `work/pr86-results-camera-review-v1.md`; the
review is complete. The next match will retain the new source allocator identity
observation described below, not repeat an unchanged capture.

## Bounded review and allocator-identity instrumentation (2026-09-28)

The read-only Results-camera review confirmed that the historical report's
served Wasm SHA-256
`96592b3fdab21695b5209d41715e65b50f156b025b671860c776a9adec525faf` matches
the retained `work/pr86-final-candidate-frozen/gameplay_menu_browser.wasm`.
The report separately records source checkout `607ff564`; its
`prototype-build.json` has artifact hashes but no source-commit field, so the
binary's source-build lineage remains unproven. The report also retains the
match-frame-0 RNG `2688366096`; the newer strict P1-Enter control began at
`1661808323`. No save/card identity was retained, so reproducing the same
natural RNG route is not established and the recorded value will not be
injected. Historical Results PAD/page/camera pointers and the first invalid
ownership transition remain absent.

The review found the current pointer guards can miss an allocator call whose
address is reused or restored before a boundary check. Source inspection found
the only source assignment to `cm_804D645C` is in `Camera_80028B9C`, and Results
OnEnter reaches `Camera_80028B9C(8)` through `fn_8017AA78`. The tracked camera
patch now increments a source allocator generation and records the last subject
count at every call. Results OnEnter requires exactly one original
eight-subject allocation; each later ownership check validates both the
pointer lease and generation. The entry packet records the generation bracket
and subject count. Patch application and focused synthetic Python/Node tests
pass. Affected native/browser builds and a fresh target scenario are pending;
this instrumentation does not itself resolve the historical Sheik-winner
failure.

## Draw-enabled source Results reducer (2026-09-28)

The preceding “builds and fresh target scenario pending” note is superseded for
the synthetic reducer only by
`work/pr86-results-draw-reducer-r4/report.json`. Built at source HEAD
`8dc4dae` with tracked-diff fingerprint
`2d0c877c40801a778b1eb4734c7eeede2c9d26f3597153d3cc7f1ec307fb08d1` and
Release Wasm SHA-256
`b01f0b765a40b9ef6571810188c06f699920f36cceba1b17b6cd4cc114883a5a`, installed
headless Chrome 153 rendered the original Results scene with synthetic
Zelda-origin Sheik standings and source-tick P1 Start at 180/360/600. It
returned all 622 Results source ticks and all 622 draws. Disconnected CPU pages
2/3 auto-advanced 0→1 at source frame 551, before P1's consumed post-page Start
at 600. The original eight-subject camera allocation pointer and generation
remained stable through every tick/draw, scene exit, host handoff and close.
The GPU was available and screenshots are retained in the same directory.
There were no page, native-command, source-tick, or camera-ownership failures;
the report classifies one `.data` request cancellation after all bytes were
consumed.

This establishes that the guarded *drawn synthetic Results* route, its CPU
auto-pages and later P1 confirmation do not alone cause the historical error.
It is not a natural Match→Sheik-win→Results reproduction, and does not clear
the historical slot-2 Sheik camera-pool failure. The historical report still
lacks Results PAD/page and camera-pointer brackets, so the first invalid
ownership transition remains unidentified. Do not treat this reducer as a
natural match or original-versus-port comparison.
