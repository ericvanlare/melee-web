# Complete-match CPU development corpus

This extends the checkpoint evidence in [CPU_OPPONENTS.md](CPU_OPPONENTS.md).
It does not admit the supported content slice, physical input, audio, pixels,
or performance. The existing level-1 and level-9 retail checkpoints remain
unchanged. These three authored workloads are development cases, not holdouts.
The [portable evidence ledger](evidence/cpu-match-development-v1.json) records
reference/build hashes, exact first divergences, coverage and retained failures.

| Players | Human | Ordinary VS CPUs | Stage | Stock timer |
| --- | --- | --- | --- | --- |
| 2 | P1 Mario | P2 Fox, level 1 | Final Destination | 1:00 |
| 3 | P1 Marth | P2 Falco, level 5; P3 Mario, level 9 | Yoshi's Story | 2:00 |
| 4 | P1 Fox | P2 Mario, level 3; P3 Marth, level 6; P4 Falco, level 8 | Dream Land | 1:00 |

The declared rules are two stocks, the stock countdown shown above, items off,
individual competitors, normal damage and speed, and pause enabled. The
source menu must establish these values; a recipe records the entire original
`StartMeleeData`, including every active player's port, role, level, costume,
stocks, team and modifiers. A setup declaration is not evidence that the
original game actually entered it.

## Shared implementation

Both entry pages use `gameplay_menu_browser.js/.wasm/.data`. The prototype's
temporary same-origin iframe still mounts `runtime.html`; CPU simulation,
player construction, rules, result ranking and teardown live in the shared
compiled implementation. No CPU decisions are implemented in the shell.

The source match owner now accepts two through four contiguous players using
the supported fighters and stages, human or ordinary VS CPU kind 4. Its stock
range is 1–5, matching the original CSS control. Teams, extra active slots,
other CPU kinds, unsupported content and unsupported rule profiles remain
rejected. This admission check is an implementation limit, not accuracy
acceptance for all configurations within it.

At exit, shared code uses the original `gm_80166378` ranking operation on an
owned copy of the source `MatchEnd`, after the final source draw. Reading the
outcome during simulation does not publish the results. A cached terminal
snapshot survives teardown until the next match begins. A unique source
winner is reported by slot; a tied list is preserved, not reduced to a guessed
winner. This is the result-ranking boundary, not the complete results-screen,
bonus/statistics or memory-card session implementation.

## Evidence format and execution

Use the existing retail input-plan, candidate and MWRC machinery. Version 3
adds an explicit active-player count and supports 2–4 fighter records while
retaining the version-2 initial PAD history and four raw PAD samples per tick.
The human samples are consumed at the original polling boundary. CPU ports
are declared neutral or disconnected physical inputs; their observed AI
outputs never enter the accepted input recipe.

For each declared workload:

1. Use a verified original disc/DOL and pinned Dolphin in a fresh owned process.
   Establish rules, characters, CPU levels and stage through original menus.
2. Discover the natural ending within the declared input bound. A cap hit is
   incomplete. Retain it; do not extend it with neutral samples or trim a
   failed reference into a success.
3. Capture the exact natural timeline twice in independent fresh processes.
   Bind the setup, input plan, initial RNG, disc/DOL, Dolphin binary, collector
   and application context in the existing provenance. Require repeatability
   before exporting the paired MWRC recipe.
4. Run the existing native trace through the bounded capture wrapper:

   ```sh
   python3 scripts/capture_cpu_native.py \
     --build-directory build/browser \
     --menu-assets path/to/owned/menu-assets \
     --game-assets path/to/owned/game-assets \
     --recipe work/cpu-corpus/case/native.mwrc \
     --output work/cpu-corpus/case/native-v1 \
     --cpu-hitlag-diagnostic
   ```

   The wrapper runs `gameplay_retail_trace.js` through Node and always supplies
   `--require-match-complete`; `--timeout` defaults to 900 seconds. It retains
   complete or partial `trace.jsonl` (native stdout) and `stderr.log`, extracts
   `CPU_AUDIT ` records to `cpu-observation.jsonl` and
   `HITLAG_AUDIT ` records to `hitlag-audit.log`, and records the return code,
   completion status, input recipe hash, JS/Wasm hashes and output hashes in
   `capture-config.json` and `capture-result.json`. The receipt also hashes the
   Node executable and every file in both owned asset trees before and after
   execution. A changed executable, recipe or asset fails the run. A completed wrapper run is capture-boundary evidence only; it
   makes no accuracy claim.
5. Freeze the browser build with `scripts/prepare_prototype.py`, serve it with
   `scripts/serve.py`, and run `scripts/capture_cpu_browser.mjs` against the
   real HTTP `runtime.html` URL. The script launches visible Chrome, imports
   the local disc, uses the existing development replay controls, and retains
   exports, errors and playing/ending screenshots. It hashes each frozen
   artifact through real browser HTTP fetches before and after the match.
6. Join the evidence with `scripts/check_cpu_match.py --manifest PATH
   --output PATH`. Run `scripts/analyze_cpu_coverage.py` for a standalone
   coverage report. Retain every comparison, including incomplete native or
   browser captures and their first divergences.

The browser path has an explicit preparation interval before live source ticks
and draws. Count and retain preparation source draws separately from live
source draws, and record the boundary where live tick zero begins. Align each
comparison failure by source tick and phase (`initial`, live frame, source draw or
ending), carrying the corresponding draw ordinal when one exists. An extra
browser preparation draw must not be mistaken for a native live draw, silently
shift the timeline, or cause rows to be dropped. Phase alignment only makes
the comparison address the same source boundary; it does not relax any core
state, CPU, RNG, PAD, match-clock or teardown field.

The CPU observation sidecar records semantic decision state, generated PAD,
targets, active queues, written command bytes, camera transforms and subjects,
HUD state, magnifier flags, source clocks and ending state. Pointer values are
normalized to player slots or offsets inside the command buffer. The sidecar
observes every source tick and source draw separately, followed by the
published result and fighter teardown. Unknown pointers, missing rows or
missing teardown cannot become a passing comparison.

Retail releases scene ownership through the next scene's
`HSD_GObj_80391304` object-library reset. The collector observes its empty
entity lists after `gm_Scene_Vs_OnExit` publishes the result and retains that
boundary in `scene-teardown.json`. It does not expect individual fighter
destructors during this original transition. The port explicitly destroys
its owned fighters; both sidecars require that no match fighters remain owned.

The native trace excludes source drawing. Its result is retained separately:
original drawing can affect later gameplay, including magnifier-related
state. The visible browser must independently execute the compiled CPU logic
and original draw traversal. A headless pass alone is insufficient, and a
headless divergence is not hidden by a browser pass.

### CPU hitlag causal proof

The retained GALE01r2 `ftCo_800ADE48` reaching-definition audit found that the
retail hitlag path reads the incoming callee-saved `r31` at `0x800AE270` even
though the decompiled local `switch_cmd` had no definition. Retail writes
`r31 = 0` only on the rejection branches at `0x800AE1D4` (GKoops),
`0x800AE1F0` (CPU kind `0xF` or `0`) and `0x800AE204` (no `x221A_b3`); the
true `x221A_b3` path has no `r31 = 1` write.

The ordinary CPU kind-4 dispatch reaches `0x800B2F68`, then
`ftCo_800B24B8`, whose prologue carries `r31 = &fp->cpu` into its direct
`ftCo_800ADE48` calls. The command path through `ftCo_800AE7AC` carries its
nonzero command argument, while `ftCo_800B21C8` explicitly clears `r31` on
its no-target path at `0x800B22F8`. A global forced-true value would therefore
change retail behavior.

The downstream source patch makes `ftCo_800ADE48` take `switch_cmd` explicitly:
all callers pass `1`, except `ftCo_800AE7AC`, which forwards `arg2`, and the
`ftCo_800B21C8` no-target path, which passes `0`. This preserves the observed retail caller carry; it does not inject recorded
CPU outputs or depend on a difficulty, character or tick number.

## Two-player complete-match evidence

The accepted retail pair in `work/cpu-corpus/case1-paired-v2/` repeats for exactly
**1,199 source ticks and 1,199 source draws**, including result publication and
scene ownership reset. P2 Fox wins by elimination. Both source processes start
from the same declared original menu context and RNG `1162236032`.

Before the carry fix, CPU decision state first diverged at tick 864: retail
entered state 18 during hitlag, while the port remained in state 1. RNG first
diverged at tick 865, and the downstream HUD damage first differed at tick 960.
Read-only retail and native diagnostics agreed on the hitlag flag and remaining
hitlag frames, isolating the undefined caller carry rather than the collision.
Those complete failing runs remain in `native-v1` and `browser-v1` evidence.

After the fix, `native-v6/` and visible Chrome `browser-v6/` agree with both
retail captures for all 1,199 ticks of the existing core state, including
CPU-generated fighter input, RNG, fighter state and controller history. The
expanded CPU decision, match-clock, HUD and published-result domains also agree.
The browser magnifier domain agrees through every source tick and draw.
The native trace deliberately excludes source drawing and retains its first
magnifier difference at tick 322.

The camera patch also preserves the original fused multiply-add boundaries
in subject extent calculation, camera bounds, target calculation and
interpolation. The GALE01r2 instruction addresses are recorded beside each
changed expression. It uses explicit `fmaf` operations with the original
separate rounding steps; compiler contraction and fast-math remain disabled.
The initial camera X now agrees at `3507b9e0`. The first remaining transform
differences are position X at tick 240 (`4273582b` versus `4273582d`) and
interest X at tick 247 (`42a51592` versus `42a51593`).

**The full expanded comparison remains failing.** The first camera
subject bone difference is Fox Y at tick 18 (`414f25a0` versus `414f259e`). The
browser also performs three source preparation draws before live tick zero:
1,199 replay draws plus 3 preparation draws equals **1,202 observed source draws**.
Those warm draws change the source camera clip planes before retail changes
them. The separate preparation stream and strict report retain this phase
mismatch; no field, epsilon or row was dropped to obtain a pass.

Observed coverage includes nine attack-motion entries (Mario 7, Fox 2), three
damage increases, two stock losses, one respawn, eight CPU target changes,
four magnifier transitions and 58 offscreen draws. CPU decision states
1, 2, 3, 5, 10 and 18 were sampled; 12 distinct queued command classes were
observed. Queue presence is not proof that every queued command executed.
The duration is 19.983 nominal seconds at 60 source ticks/second, including
intro and ending ticks; this is not a wall-time performance measurement.

The existing level-1 and level-9 checkpoint recipes were rerun after the CPU
and camera fixes without recapturing their retail references: native and visible browser
still match 480 ticks each. The existing PAD-history human regression also
matches 240 ticks in native and browser. Each browser checkpoint has the exact
corresponding source step/draw count and zero recorded page errors.
The Release browser, retail-trace and timer targets build; all 598 unit tests
pass with 35 optional-fixture skips. The timer target also passes original
countdown, pause/resume, timeout, repeated teardown and inconsistent-selection
rejection checks against owned local assets.

The instrumented complete browser run recorded one callback gap over 33.3 ms
(38.57 ms worst), while native callbacks had zero 16.67 ms misses
(10.06 ms worst). Other captures were running on the same machine. These
separate observations identify a timing failure without establishing an
isolated cold/warm performance result.

The complete two-player join is `result-v7.json`; its manifest binds the frozen
native and browser builds, exact Node/assets, both independent reference runs,
recipe, source teardown and before/after HTTP artifact inventories. An earlier
browser rerun (`browser-v2/`) reached the ending and matched core state but
failed the capture harness's response-body retrieval; it remains a failed run.

Three- and four-player discovery retries reached the original timeout after
3,838 ticks. The three-player bout published two winners, Falco and Mario,
and therefore requires Sudden Death before whole-match completion. The
four-player bout published Falco as its sole winner; its independent fixed
captures remain in progress. Discovery alone is not an accepted reference.

The original camera traversal counts were 3,835 and 3,836 respectively.
Three-player source ticks 1610, 2611 and 3612 had no camera traversal;
four-player ticks 2268 and 3269 had none. The observation format retains those
exact draw ordinals and source indices instead of equating simulation ticks
with camera traversal counts.

The preceding correctly prepared attempts reached 3,725 and 3,447 ticks before
the 1,800-second debugger wall-time limit. Those partial attempts remain
failures. The retries used the same rules and authored input with a larger
wall-time budget and grouped read-only memory reads.

The historical three-player 1:00 scenario remains addressable by its original
catalog ID and hash. The active three-player workload has a separate `-120s`
ID, an original 2:00 stock timer and a predeclared 7,923-sample input bound.
It continues the same authored combat cycle. It must independently reach a
natural ending and repeat in two fresh processes before becoming a reference;
the longer timer itself proves neither completion nor agreement. This change
does not modify the retained one-minute tie or the two-/four-player recipes.

A timed bout with multiple original winners may require Sudden Death. The
report flags this as incomplete whole-match coverage even if the ordinary
bout and its teardown agree. Do not claim a complete match from this boundary
without its natural ending.

## Coverage and integration limits

Coverage separates observed CPU decision-state samples, queued command
inventory and executed fighter motions. Queued bytes do not prove that each
command executed. Reports also count attacks, received damage, stock losses,
respawns, target changes, camera/magnifier events, stages, source ticks/draws,
roles and difficulty. Tick counts divided by 60 are nominal source duration,
not measured wall time or an original VI cadence claim.

Instrumented state runs retain native callbacks over 16.67 ms and browser
gaps over 33.3 ms as separate counters. Performance admission requires later
isolated cold/warm runs after the performance branch is integrated. These
captures cannot establish that admission.

Integration may touch `src/gameplay_menu_browser.cpp`, shared match ownership,
`CMakeLists.txt`, `cmake/FighterRuntime.cmake`, and
`patches/melee-gameplay.patch`. This work does not require changing the inline
host in `runtime.html`, hitch tools, or renderer/cache instrumentation.
