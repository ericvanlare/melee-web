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
   real HTTP `runtime.html` URL with `--headed`. Arrange this foreground session
   with the user or use a separate test machine. The script launches visible Chrome, imports
   the local disc, uses the existing development replay controls, and retains
   exports, errors and playing/ending screenshots. It hashes each frozen
   artifact through real browser HTTP fetches before and after the match.
6. Join the evidence with `scripts/check_cpu_match.py --manifest PATH
   --output PATH`. Run `scripts/analyze_cpu_coverage.py` for a standalone
   coverage report. Retain every comparison, including incomplete native or
   browser captures and their first divergences.

Manifest paths resolve relative to the manifest file. For example, a manifest
inside a prepared scenario directory has this shape; replace run directories,
build paths and the Node executable with those used by that capture:

```json
{
  "schema": "melee-web-cpu-match-run",
  "version": 1,
  "scenario_id": "mario-human-vs-fox-cpu1-final-destination",
  "scenario": "scenario.json",
  "input_plan": "input-plan.json",
  "recipe": "paired.mwrc",
  "reference_a": {
    "trace": "capture-a.jsonl",
    "run_directory": ".retail-replay-run-A"
  },
  "reference_b": {
    "trace": "capture-b.jsonl",
    "run_directory": ".retail-replay-run-B"
  },
  "native": {
    "trace": "native-v1/trace.jsonl",
    "observation": "native-v1/cpu-observation.jsonl",
    "build_directory": "path/to/frozen-native-build",
    "menu_assets": "path/to/owned/menu-assets",
    "game_assets": "path/to/owned/game-assets",
    "node_executable": "path/to/node"
  },
  "browser": "browser-v1",
  "build_directory": "path/to/frozen-browser-build"
}
```

The native capture directory must retain its config, receipt and every hashed
output beside `trace.jsonl`. Export `paired.mwrc` with
`scripts/export_retail_replay.py capture-a.jsonl capture-b.jsonl --output paired.mwrc`;
do not hand-author or substitute CPU-generated decisions into it.

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

The four-player trace demonstrates this dependency. Original
`ifMagnify_802FBBDC` sets the offscreen flag during scene-camera drawing.
`Fighter_8006A360` consumes that flag on subsequent ticks through
`ifMagnify_802FC998`, incrementing `dmg.x1910` while the original player gate
and damage limit permit it. The owned common data requires 60 qualifying ticks,
limits the effect below 150 damage, and applies one damage point. P2 Mario's
retail offscreen interval supplies ticks 1812–1871; the headless port leaves
the flag clear. At 1871 the retail damage increase triggers eight HUD-shake
`HSD_Randf` calls in `ifStatus_802F4B84`, explaining the subsequent RNG difference.
The counter itself is not among the recorded fields; this causal explanation
combines the observed flag interval and damage/RNG with the original source.

A future native match comparison needs the real shared draw boundary:
`GameplayMatchSession::draw()` through the original scene-camera traversal,
including HUD/magnifier state, and `melee_web_match_flow_present()`. Inserting
recorded offscreen flags or directly adding the missing damage would bypass
the behavior being validated. The current headless result remains failing.

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

## Complete-match results

All three retail pairs now repeat in fresh processes, including generated CPU
input, fighter/RNG/PAD state, camera/HUD observations, result publication and
original scene ownership reset. Every recipe has a natural original ending.
The current frozen port executions are `native-v9/` and `browser-v9/`, joined
by each scenario's `result-v9.json`.

| Scenario | Retail ticks / draws | Native core comparison | Visible browser core comparison |
| --- | --- | --- | --- |
| Mario / Fox CPU 1, FD | 1,199 / 1,199 | Exact through completion | Exact through completion |
| Marth / Falco CPU 5 / Mario CPU 9, Yoshi's Story | 4,346 / 4,343 | First RNG difference at 2241; no ending within the recipe | Exact through completion |
| Fox / Mario CPU 3 / Marth CPU 6 / Falco CPU 8, Dream Land | 3,838 / 3,836 | First damage/RNG difference at 1871 | First generated-input difference at 2495; reaches completion |

Here “core” means the existing declared entry, supplied PAD samples, generated
fighter input, fighter state, RNG, source match clock and all three controller
history banks. **No scenario passes every expanded field across retail, native
and browser.** Camera arithmetic and source-draw timing remain explicit failures.
The two- and three-player browser CPU decision, HUD, magnifier and match-result
domains agree independently; CPU decisions were never supplied to either port.

### Two players

The accepted pair in `work/cpu-corpus/case1-paired-v2/` begins at RNG
`1162236032` and ends with P2 Fox winning by elimination. Its 1,199 source ticks
contain nine attack-motion entries (Mario 7, Fox 2), three damage increases,
two stock losses, one respawn and eight CPU target changes. The observed CPU
states are 1, 2, 3, 5, 10 and 18; 12 queued command classes occur. Queue presence
is not proof that every queued command executed.

The original complete failures before the caller-carry repair remain retained:
CPU state first differed at 864, RNG at 865 and HUD damage at 960. The shared
carry fix restores all 1,199 core ticks. Explicit camera fused operations also
restore the initial camera X. The remaining transform differences are position
X at 240 (`4273582b` versus `4273582d`) and interest X at 247 (`42a51592` versus
`42a51593`). FOV agrees throughout this workload.

The entry-scale repair described below removes the Fox subject-Y mismatch at
18. The next subject difference is Z at 82 (`bf147ce9` versus `bf147cea`).
The browser has three preparation source draws before its 1,199 live draws,
changing clip planes before retail does; its strict camera comparison first
fails at live tick zero. Native has zero draws, a clip-plane difference at 1
and a magnifier difference at 322. These fields and phases remain compared.

### Three players

The accepted pair in
`work/cpu-corpus/paired-v8/marth-human-vs-falco5-mario9-yoshis-story-120s/`
ends with P3 Mario winning by elimination. All 4,346 browser core ticks and
CPU decision observations agree. Coverage includes 77 attack-motion entries,
58 damage increases, five stock losses, three respawns and 134 CPU target
changes. There are 449 offscreen player-draw observations.

The original omits camera traversal at ticks 1598, 2599 and 3600. The browser
performs 4,346 live draws plus three preparation draws, so strict traversal
alignment fails. Browser FOV already differs at tick zero, and its first
subject difference is Marth Z at 67 (`bd59cc74` versus `bd59cc73`).
The headless native run consumes all 4,346 samples but never reaches the
original ending; its retained prefix first differs in RNG at 2241. It is a
failed capture, with no synthesized end record or appended neutral input.

The historical one-minute workload remains intact under its separate original
catalog ID. It timed out after 3,838 ticks and 3,835 draws with Falco and Mario
both marked winners, requiring Sudden Death. The active workload was separately
declared with a two-minute stock timer and 7,923-sample authored bound. It
continues the same combat cycle, and its discovered natural 4,346-tick timeline
was then captured twice independently. This does not turn the older tie into
a completed whole match. Sudden Death coverage remains open.

### Four players

The accepted pair in
`work/cpu-corpus/paired-v7/fox-human-vs-mario3-marth6-falco8-dream-land/`
ends with P4 Falco as the sole timeout winner. Coverage includes 126
attack-motion entries, 116 damage increases, four stock losses, three respawns
and 255 CPU target changes. Original camera traversal is absent at 2268 and
3269; the browser's extra traversals remain visible in the strict comparison.

Earlier browser attempts stopped at 22 and 18 ticks on audio timing guards.
The shared host's timing-pause message now uses the existing development
page's recognized prefix. Instrumented accuracy runs can explicitly resume
and count those pauses; live play still pauses and performance capture fails.
`runtime.html` is unchanged. The next complete retained prefix reached 2,622
ticks before aborting on the opcode-63 unsupported-command sentinel during
an ordinary taunt. The common-appeal repair below removes that fault.

`browser-v9/` completes all 3,838 ticks and teardown, with exact RNG, PAD history,
HUD, magnifier and result state. Its first core difference is P2 Mario's
CPU-generated input at 2495: retail emits stick bytes `90,40`; the port emits
`00,00`. The source `ftCo_800AC5A0` uses uninitialized stick locals when the
knockback vector is zero during its periodic hitlag branch. The
[DOL register audit](CPU_ZERO_KNOCKBACK_ABI.md) shows that these bytes
come from volatile-register and fighter-pointer residue. Matching them requires
an explicit original ABI compatibility model; no substitute neutral rule or
recorded CPU decisions were added.

The native clank speed difference at 264 is repaired. Its next difference is
the draw-dependent one-point Mario damage and subsequent RNG change at 1871,
explained above. Complete native failures are retained. The entry-scale
repair removes the first subject difference at 12; the next is Fox Z at 74
(`3f123975` versus `3f123976`). Camera interest Y still differs at 184.

### Shared repairs and regression checks

Common appeal uses action rows 239/240, selected by motion states 264/265.
Those rows now retain checked command storage for Mario, Fox, Falco and Marth.
Fox's appeal also contains HSD node-visibility channel 11. Native fighter clips
reach the existing `JObjUpdateFunc` handler, which updates `JOBJ_HIDDEN`.
Generic inspection and the pose bridge still reject that channel; native and
generic animation cache policies are distinct. All four owned archives pass
command-ownership checks, and malformed streams and unsupported channels
remain rejected.

The clank repair preserves three original fused operations in `ftcoll.c`;
see [the instruction and operand audit](FTCOLL_X191C_ROUNDING_AUDIT.md).
The entry-scale repair separately preserves `ftCo_EntryStart_Phys`:
GALE01r2 `0x800C6788` rounds the subtraction, `0x800C67A8` rounds the entry
fraction, and `0x800C67AC` fuses the final multiply-add before `0x800C67B0` stores
it. Owned common data has `x6BC = 30` and `x6C4 = 3c23d70a`. A fraction of 9/30
and Fox's initial scale `3f75c28f` reconstruct the observed fused result
`3e970a3e`; splitting the multiply/add gives the prior port's `3e970a3d`.
The new native matrix diagnostic now reports the exact retail root scale at 18.
Global contraction and fast-math remain disabled.

The existing level-1 and level-9 recipes still match their unchanged retail
references for 480 ticks each in native and browser after these repairs.
The existing human PAD-history regression still matches 240 ticks in both.
Browser source-step/draw counts are exact for each checkpoint and recorded
page errors are empty. These are port reruns, not new checkpoint references.
The browser, retail-trace and timer Release targets build. All 604 unit tests
pass with 35 optional-fixture skips. Owned timer checks
pass original countdown, pause/resume, timeout, repeated teardown and
inconsistent-selection rejection.

## Coverage and integration limits

The accepted retail workloads total 9,383 source ticks and 9,378 traversals,
with 212 attack-motion entries, 177 damage increases, 11 stock losses, seven
respawns and 397 CPU target changes. They cover CPU levels 1, 3, 5, 6, 8 and 9,
all four fighters in CPU roles, three human-character roles and three stages.
Falco as the human and Battlefield remain outside these three workloads.

Coverage separates observed CPU decision-state samples, queued command
inventory and executed fighter motions. Queued bytes do not prove that each
command executed. Reports also count attacks, received damage, stock losses,
respawns, target changes, camera/magnifier events, stages, source ticks/draws,
roles and difficulty. Tick counts divided by 60 are nominal source duration,
not measured wall time or an original VI cadence claim.

Instrumented state runs retain native callbacks over 16.67 ms and browser
gaps over 33.3 ms as separate counters. Performance admission requires later
isolated cold/warm runs after the performance branch is integrated. These
captures cannot establish that admission. The accepted MWRC recipes use the
existing development replay boundary, so the stable integrated runtime can
reuse them in its performance mode. Fresh application state and explicitly
controlled cache conditions remain required; these runs are not cold/warm
profiles.

Future CPU holdouts must be independently authored after the shared fixes.
They should include unseen two-/three-/four-player fights, the currently
uncovered difficulties 2, 4 and 7, Falco as human, Battlefield, zero-knockback
hitlag, taunts, recovery, stock loss/respawn and consecutive-match teardown.
Teams, other CPU kinds and Sudden Death remain outside the current admitted
implementation boundary. All three current workloads remain development cases.

Integration may touch `src/gameplay_menu_browser.cpp`, shared match ownership,
`CMakeLists.txt`, `cmake/FighterRuntime.cmake`, and
`patches/melee-gameplay.patch`. Status/evidence integration may also overlap in
`STATUS.md` and `docs/REPLAY_CORPUS.md`. This work does not require changing the
inline host in `runtime.html`, hitch tools, or renderer/cache instrumentation.
