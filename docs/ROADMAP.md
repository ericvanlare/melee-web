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

In progress: this same source runtime is connected to browser presentation and
fixed-tick controller input. Authored Final Destination stage objects, Mario
items/effects, stock outcomes and the renderer cache are integrated. Cold
first-use preparation, audible and physical controller checks, broader rendered
combat and original-game state comparisons remain. This checkpoint does not
satisfy milestone 1.

## 1 — Playable local stock match

Launch Mario versus Mario on Final Destination from user-supplied local data.
Both local players must use the original action/state, input, physics, collision,
attack and damage paths. Validate movement, jump/landing, shields/grabs/attacks,
KO, stock loss, respawn, match outcome and restart, with required audio behavior.
Direct launch is sufficient; full menus are a later gate.

The normal browser launch now uses the authored Final Destination player markers
`(-60, 10, 0)` and `(60, 10, 0)` through the original Ground API. Close-range
`+/-20` coordinates remain diagnostic fixtures. The latest local suite reports
224 tests passed without skips; the historical 192-test checkpoint is retained
only as an earlier validation point.

Compare recorded original-game and port state at fixed simulation boundaries:
positions, actions, damage, stocks, RNG and outcome. Preserve original 60 Hz
simulation independent of presentation. Missing services fail explicitly; do not
substitute handwritten movement or successful runtime stubs.

Reference hardware selected from the development machine: Apple M4, Mac16,12,
32 GiB RAM, macOS 26.6.2 (25G83). The latest saved-cache full-stage run was
captured in Chrome 152's IAB at DPR1 at
1280×960 records 14,452 ticks, 17.63 ms recent p95, 30.50 ms worst interval,
zero intervals above 33.3 ms and zero audio underruns. The saved-cache stock
run records 1,985 ticks, three respawns, four losses, P2 victory, 22.23 ms worst
interval, zero intervals above 33.3 ms and zero audio underruns.
Measure cold and warm release-build frame intervals, CPU/GPU work where
available, input latency, audio underruns, loading stalls and memory high-water.
The frame budget is approximately 16.7 ms with headroom; warm timing does not
establish cold first-use acceptance.

The optional renderer-only `/melee-render-cache` uses IDBFS, saves after native
unload when the pipeline queue is idle, and restores 81 cached pipelines before
assets on the next page startup. Game assets are not persisted. The first cold
visit still has unresolved rendering stalls. Browser audio uses a 1536-sample
prefill and per-frame transport gating; the saved-cache runs had no underruns.
The selectable 2× SDL render scale remains available for scale-specific checks.

## 2 — Complete versus loop

Local disc import → menus → character/stage selection → controlled matches →
results → menus. Expand fighters, stages, items and four-player scenarios through
the same source runtime, preserving accuracy and performance regressions.

## 3 — Full vanilla game

All original modes, movies, saves, controller routing, failure recovery and
browser lifecycle behavior. Broaden browser support after the reference
implementation is stable. Custom fighters, netplay and product extras are later
projects. See [NEXT_PHASE.md](NEXT_PHASE.md) for the immediate work boundaries.
