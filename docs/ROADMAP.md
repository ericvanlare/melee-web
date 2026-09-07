# Acceptance milestones

## 0 — Source and browser feasibility (current)

- Pin source/toolchain revisions and reproduce a build from a clean checkout.
- Render synthetic GX geometry through actual Aurora, including real HSD state code.
- Inspect the visible result and collect browser logs. Validation errors are failures.
- Inventory SDK, audio, scheduler, byte-order, pointer and numerical dependencies.
- Then load and render a real Melee asset/scene. A synthetic triangle does not meet this.

Decision: proceed to game integration only with a demonstrated rendering path and
an explicit design for the missing services. No full-game performance claim at this gate.

## 1 — Complete versus loop

Local disc import → menus → character/stage selection → controlled match with audio
and input → results → menu. No emulator fallback. Missing services must fail visibly.

## 2 — Accuracy and performance

Use identical recorded input streams and compare original-game and port state at
defined frame boundaries (positions, actions, damage, RNG, match outcome). Bound
and explain numerical differences; image comparison alone is insufficient.

Select explicit desktop reference devices/browser versions before performance
acceptance. Measure visible-tab frame intervals, CPU submission, GPU time where
available, audio underruns, memory high-water, loading stalls and end-to-end input
latency. Budget approximately 16.7 ms for a 60 Hz presentation with headroom; preserve
the original simulation cadence. Test 1v1 and demanding four-player scenarios.

## 3 — Full vanilla game

All fighters/stages/items, original single-player modes and movies, persistent
saves, controller routing, failure recovery and lifecycle behavior. Broaden browser
support after the reference implementation is stable. Custom fighters, netplay and
product extras are future projects, not requirements for these milestones.
