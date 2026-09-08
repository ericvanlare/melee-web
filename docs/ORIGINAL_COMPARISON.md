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
