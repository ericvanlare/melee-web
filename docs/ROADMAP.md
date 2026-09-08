# Acceptance milestones

The goal is full vanilla Melee compiled to WebAssembly, without an emulator.
The first playable milestone is a local **Mario-versus-Mario stock match on
Final Destination at 60 fps**. Progress is measured by original runtime behavior,
not the number of supported inspection assets.

## 0 — Rendering and runtime foundation

Established: pinned tools/source, original HSD model/material/skin/animation
rendering, local asset import and browser PAD sampling. The separate Wasm runtime
now passes original `Fighter_Create` → source Fall/Wait settling → 120 neutral
Wait ticks → unload → restart, four times across two complete SDK worlds.
See [the runtime gate](FIGHTER_RUNTIME.md) and [STATUS.md](../STATUS.md).

Next: connect this same source runtime to browser presentation and fixed-tick
controller input, and establish original-game state comparisons before expanding
movement and combat. Rendered inspection animation is still separate from the
validated fighter process loop.

## 1 — Playable local stock match

Launch Mario versus Mario on Final Destination from user-supplied local data.
Both local players must use the original action/state, input, physics, collision,
attack and damage paths. Validate movement, jump/landing, shields/grabs/attacks,
KO, stock loss, respawn, match outcome and restart, with required audio behavior.
Direct launch is sufficient; full menus are a later gate.

Compare recorded original-game and port state at fixed simulation boundaries:
positions, actions, damage, stocks, RNG and outcome. Preserve original 60 Hz
simulation independent of presentation. Missing services fail explicitly; do not
substitute handwritten movement or successful runtime stubs.

Select desktop reference hardware/browser versions before claiming 60 fps.
Measure release-build frame intervals, CPU/GPU work where available, input
latency, audio underruns, loading stalls and memory high-water. The frame budget
is approximately 16.7 ms with headroom; average viewer FPS is not acceptance.

## 2 — Complete versus loop

Local disc import → menus → character/stage selection → controlled matches →
results → menus. Expand fighters, stages, items and four-player scenarios through
the same source runtime, preserving accuracy and performance regressions.

## 3 — Full vanilla game

All original modes, movies, saves, controller routing, failure recovery and
browser lifecycle behavior. Broaden browser support after the reference
implementation is stable. Custom fighters, netplay and product extras are later
projects. See [NEXT_PHASE.md](NEXT_PHASE.md) for the immediate work boundaries.
