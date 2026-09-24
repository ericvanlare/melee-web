# Original-game movement comparison

The first measured comparison covers a stationary full Mario jump and landing.
Across 102 original game frames, motion IDs, non-idle animation frames and
vertical-velocity float bits match the source port. A second retail run matches
all 102 frames' motion, height bits and vertical-velocity bits. These are scoped
results; complete Mario gameplay equivalence is not established.

The original runs on Pokémon Stadium's stationary side platform (GrKind16,
y=25); the source port runs on Final Destination (GrKind37, y=0). Final
Destination is locked on the fresh original save. Subtracting the stage-height
translation leaves small floating-point position differences (up to roughly
0.000008 units in this capture). Costumes, RNG and match rules also differ.
The comparison excludes idle animation phase, camera, horizontal movement,
collision at edges, damage and audio. It must not stand in for an FD match oracle.

## Final Destination spawn evidence

The normal browser launch now reads the authored FD player markers through the
original Ground marker API. The source positions are `(-60, 10, 0)` for slot 0
and `(60, 10, 0)` for slot 1; the runtime derives the default facing from each
marker's x sign. Close-range `+/-20` positions remain explicit fixtures used by
combat and diagnostic traces. This verifies source stage-point selection for
launch, while the complete original `gm_16AE` match setup remains outside the
scope of the movement comparison.

## Provenance and capture

- Original: owned GALE01 revision2, main.dol SHA1
  `08e0bf20134dfcb260699671004527b2d6bb1a45`.
- Reference-only emulator: official Dolphin2606a, commit
  `c77bbaa0f372c3f72281602a8b087206706542cb`, Interpreter64, one CPU thread,
  cheats disabled, fixed RTC1704067200, isolated user directory and fresh save.
  Dolphin is not part of the browser runtime or a fallback for unported code.
- Source port: pinned Melee source and checked Emscripten build documented in
  `dependencies.lock.json` and `docs/DEPENDENCIES.md`.
- Input: P1 X held for12 original match-counter frames, then90 neutral frames;
  P2 neutral. Input uses Dolphin's documented Pipe controller backend.
- Observation: GDB hardware execution breakpoints and bounded memory reads only.
  No game-memory writes, code patches, injected functions or edited game state.

`tools/reference_capture.py` is sourced from an attached GDB session for this
exact original version. Its addresses are version-specific. Set
`MELEE_REFERENCE_WORK` to an ignored local output directory (default `work`).
The isolated Dolphin user directory's controller pipes must already exist as
`reference-oracle-user/Pipes/pad1` and `pad2` below that directory.

Start Dolphin with debugger mode `-d`; merely enabling the GDB socket does not
provide working execution breakpoints in this configuration. Configure GDB for
`powerpc:common`, big endian and no pagination. After attaching, source the
collector before original fighter creation, and create a user-visible hardware
breakpoint at `0x80390eb4` (the original GObj scheduler return) with silent commands.
The collector's internal breakpoints record creation and return without stopping.
Select the original match through ordinary controller input, then use:

```
ref-capture on
ref-game 12 1 PRESS X
ref-game 90 1 RELEASE X
ref-capture off
```

Keep the reference window focused for Pipe input. The script logs input changes
and the original match counter at `0x8046b6c4`. Scheduler returns can repeat
without advancing gameplay, so **do not equate breakpoint hits with game frames**.
Group by the original counter and reject groups with differing measured states
before comparison. Do not silently drop mismatching samples.

The port's `gameplay_trajectory_trace` target consumes the same local ten-file
simulation bundle as the other source traces. It settles normally, supplies the
same12-frame X input, and emits102 states. Run it with the project-local Node
runtime and the local asset directory as its argument. Raw original records and
comparison reports remain in ignored `work/`; they contain local provenance and
are not bundled with the app.

## FD terminal visibility compatibility

A separate isolated FD stock match uses the same owned original DOL and Dolphin
version. Its fresh local save enables only FD's progress bit before match setup;
the setup address is verified against the actual save getter instructions, not
source offset comments. Match setup then uses ordinary Pipe controller input.
This setup is separate from the earlier unmodified-save jump captures above.

With Dolphin's cached interpreter, a hardware breakpoint at `0x8036aeb0`
observed original `FObjUpdateAnim` taking its no-interpolation branch at match
frame7012. The delayed visibility track has start=-40, opcode CON, interpolation
NONE, state LOAD_DATA, no remaining packed values, zero duration, and its complete
three-byte stream consumed. Its authored constant is zero. The original passes
an uninitialized stack float (`0xc0b39bde`, about -5.612777) to `HSD_JObjUpdateFunc`;
the joint flags are `0x30000050` before and after the callback, so it stays hidden.
The original instruction sequence confirms that this path never writes the
callback's output stack slot.

The host patch defines this terminal single-CON case using the authored value,
with no change to parser state or flags. This preserves the observed FD visibility
result; it deliberately does **not** reproduce incidental original stack bits.
The same rule now covers every terminal single-datum stream: each interpolation
type's own zero-duration path emits `p1` (CON at `time>=fterm`, LIN with its
wait flag, SPL0/SPL/SLP at `fterm==0`), and KEY terminals are already defined by
`FObjLaunchKeyData`. Donkey's Pass and StopCeil branch terminals were the first
exercised SPL0 endpoints; their focused checks now pass and the platform-drop
reducer completes. States outside that class still fail explicitly. Generic pose
validation rejects unpaired LIN/SPL tracks; native fighter-action hydration
retains structurally bounded streams and fails at the guarded consumer if
undefined output is reached. See [Donkey's current
boundary](DONKEY_KONG_PORT_NOTES.md). Parser regressions exercise delayed
zero and one constants, early-callback rejection and invalid-state rejection.
The two-world 36,000-frame FD probe passes every state and cycle restart.

A subsequent cached-interpreter per-frame stepping session encountered an emulator
unknown-instruction error. Its partial movement trace is diagnostic only and is
not accepted as an edge-equivalence result. Further movement captures use the
plain interpreter; the earlier rare visibility breakpoint capture did not use
that repeated stepping workflow.

## Full-stage FD edge regression

A new plain-interpreter FD capture starts P2 grounded at x=59.153053283691406,
facing left, then holds right for75 frames. The original enters Fall at
x=86.153053283691406 and remains airborne on the next frame at
x=87.01305389404297. This is a two-minute Time match; the port regression uses
stock rules. The comparison is scoped to the run-off behavior before stock loss.

The old isolated port probe omitted full stage initialization. Consequently
`mpColl_804D64AC` never advanced, and the fighter selected `mpCheckFloor` instead
of the generation-dependent `mpCheckFloorRemap` path. Full stage updates already
ran in the browser. Adding those same stage and camera lifetimes to the probe
removes the artificial floor regrab without changing gameplay collision code.
The regression now checks continuous run-off and the first stock loss at this
initial position, plus a separate post-respawn running scenario. These checks
establish the observed behavior, not bitwise equality of every fighter field.

## Four-stock movement and first respawn

The local read-only capture in `work/reference-stock/stock-plain.jsonl` is from
GALE01 revision 2 on Final Destination with four stocks, items set to NONE,
Mario yellow on port 1 at -60 and Mario red on port 2 at +60. The original
collector starts at game frame 12, holds port 2 right for 100 frames, then sends
440 neutral frames. The capture contains one stock loss and one grounded
respawn; its provenance is recorded in the adjacent `provenance.json`. Original
assets and reference data remain outside the repository.

The port's `gameplay_trajectory_trace --stock` settles both source fighters for
120 neutral ticks, emits the initial sample, then uses the same 100 right and
440 neutral raw input frames. `tools/compare_stock_trace.py` aligns all 541
samples and reports exact equality for motion, ground/air state, stock count,
position bits, velocity bits and damage bits for both fighters. P1 receives neutral input throughout. The port uses the neutral red costume
for both fighters; the original uses yellow for P1 and red for P2. Startup
animation age differs from the original capture, which begins at game frame 12. Animation frame/idle phase and RNG are
explicitly excluded because their startup states are not synchronized. This is
a bounded movement, death and first-respawn comparison, not a full-match
equivalence claim.

The extended `--stock-jab` recipe adds 120 neutral ticks, three P1 A ticks and
90 neutral ticks. Against original game frames 12..765, the comparator with
`--recipe stock-jab` reports 754/754 matching samples for the same state fields.
P1 enters jab motion 44 at relative tick 661; P2 enters damage motion 79 with
3% damage at tick 662. Idle phase and RNG retain the exclusions above.

## Current browser runtime evidence

The latest saved-cache renderer run, captured in Chrome 152's IAB at DPR1,
completes a 14,452-tick full FD cycle at
1280×960 with a 30.50 ms worst interval, no interval above 33.3 ms and no audio
underruns. A separate saved-cache raw-input stock run completes 1,985 ticks,
three respawns, four losses and the normal P2 winner with a 22.23 ms worst
interval, no interval above 33.3 ms and no audio underruns. The renderer-only
`/melee-render-cache` restores 81 cached pipelines before assets on the next
page startup and persists no game files. Browser audio uses a 1536-sample
prefill and per-frame transport gating; these runs had no underruns. Cold
first-use stalls remain unresolved, and these warm browser runs do not establish
full original-game equivalence, audible output or physical two-controller
behavior. The selectable 2× SDL render scale remains available for
scale-specific checks.

## CSS/SSS and match-entry lifecycle gate

The versioned transition schema, read-only retail collector, port emitter and
strict first-divergence comparator are documented in
[`TRANSITION_EQUIVALENCE.md`](TRANSITION_EQUIVALENCE.md). A pinned four-stock
Mario/Mario Final Destination capture now completes all nine boundaries. Retail
keeps `menu01.hps` active under one owner with zero stream start/stop, AX driver
initialization or language-bank initialization calls through both CSS/SSS trips,
then performs one stop/start into `sp_end.hps` at match entry. The port matches
that lifecycle and audio result. The port now retains the same raw menu payload
through SSS exit, then applies persistent stock/item/rumble settings through the
source VS-entry adapter. A deterministic replay matches the retail rules, all
four player records and RNG at every captured boundary.
