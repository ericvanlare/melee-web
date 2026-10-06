# Roadmap and priorities

The goal is full vanilla Melee compiled to WebAssembly, preserving original
behavior without a shipped PowerPC interpreter or JIT. On the way there, the
project delivers two things first: **online multiplayer** and **everything a
competitive-rules set needs**. Whole-game breadth follows them.

This page orders work. It is a living document: the architects update it when
priorities change, and record why in the [architects' log](ARCHITECTS.md). The
pinned [priorities tracker](https://github.com/ericvanlare/melee-web/issues/158)
shows each item's live state and links its issues and pull requests. GitHub
issues own bounded tasks and completion criteria. [STATUS](../STATUS.md) owns
observed results and scoped receipts. Do not copy changing measurements here, and
do not treat a closed implementation issue, successful build or short replay as
a complete acceptance result.

## Current priorities

Set by the owner on 2026-10-05. Two tracks run in parallel. When they compete
for the same scarce resource, such as the single browser-timing lane on the test
machine or a reviewer's attention, take the lowest-numbered unblocked item and
prefer Track A.

### Shared prerequisite: gameplay headroom

**H1. No forced timing pauses in desktop gameplay.** On the desktop reference
profile, a natural four-stock match through the original menus completes without
a timing-guard pause. Both tracks depend on this. Lockstep stalls both players
on any host stall, rollback must re-simulate several ticks inside one tick's
budget, and competitive play cannot tolerate pauses. H1 gates A3 and B5. The
functional steps A1 and A2 can proceed without it. Work starts from the open
pause classification in [#116](https://github.com/ericvanlare/melee-web/issues/116).
The fix must come from the runtime, never from a threshold, frame-rate,
skipped-tick or auto-Resume change.

### Track A: online multiplayer, beelined

**Target.** Two people on different machines play vanilla Melee against each
other in the browser under competitive rules, through the original menus.

The path reuses what the project already proved. The browser runtime replays
recorded per-tick controller input to exact recorded-session agreement with the
original game. Two runtimes that start from the same CSS context and receive the
same inputs should therefore stay in step without any snapshot or restore. That
makes delay-based lockstep the shortest route to real play. Rollback, and then
Slippi desktop cross-play, build on it later. The design, including the host
channels that A1 must rule out, is [architecture decision 016](ARCHITECTURE.md#016--online-play-starts-as-lockstep-from-a-shared-css-context).

| Step | Outcome |
| --- | --- |
| **A1. Determinism** | Two fresh browser instances given the same build, disc, CSS start context and per-tick input produce identical per-tick state checksums from CSS through Results and back to CSS. Their pacing, caches and memory initialization deliberately differ. First run in Chrome, then across Chrome and WebKit, and across x86 and arm64. |
| **A2. Local lockstep** | Two tabs on one machine play through original CSS → SSS → match → Results → CSS with a fixed input delay over loopback, with matching checksums on every tick. |
| **A3. Internet lockstep** | Two machines connect through a small signaling service and a WebRTC data channel (with relay fallback). They play a complete set with desync detection and a clean disconnect. |
| **A4. Rollback** | Snapshot, restore and re-simulate replace the fixed delay with prediction. This continues the state-ownership work in [#115](https://github.com/ericvanlare/melee-web/issues/115). |
| **A5. Slippi desktop cross-play** | A pinned Slippi compatibility profile and a transport bridge let a browser player face a desktop Slippi player ([project direction](PROJECT_DIRECTION.md), [#9](https://github.com/ericvanlare/melee-web/issues/9)). |

Every online mode carries an explicit mode identity under #9, so a networked
session is never reported as retail-equivalent evidence.

### Track B: competitive-rules Melee before the whole game

**Target.** A player can run a competitive singles or doubles set through the
original menus:

- All 26 selectable fighters on the original CSS, from an unlocked save.
- The legal stages: Battlefield, Final Destination, Dream Land, Yoshi's Story,
  Fountain of Dreams and Pokémon Stadium, each with its stage services and hazards.
- Tournament rules set through the original VS Rules and Rules Plus menus (stocks,
  timer, items off, pause off, friendly fire), with time-out and sudden death.
- Team Battle with four controller ports.
- Results, rematch and return to CSS.

| Step | Outcome |
| --- | --- |
| **B1. Gap inventory** | One tracked checklist of what each competitive item lacks, with its evidence label. It is kept in the tracker. |
| **B2. Legal stages complete** | Every legal stage passes the [stage checkpoints](ADDING_STAGES.md), including Pokémon Stadium's transformations and the other stages' services and hazards. |
| **B3. Rules and modes** | Competitive VS Rules and Rules Plus settings, Team Battle with four ports, and time-out and sudden death work through the original menus. |
| **B4. Roster accuracy** | Natural CPU sessions across the full roster compare to the original, covering transformations, partners, copy abilities, grabs, projectiles and recovery. |
| **B5. Competitive-set acceptance** | A frozen Release candidate plays a full singles set and a doubles set under competitive rules with separate state, input, audio and timing receipts. |

Whole-game items that no competitive set needs wait until Tracks A and B finish,
unless they block one of those tracks. These include single-player modes, Event
Matches, trophies, movies, non-legal stages, minigames and unlock challenges.
Existing work on them may land, but no new campaign starts.

### Standing rules and baselines

- The accuracy rules in [AGENTS.md](../AGENTS.md), the
  [accuracy contract](ACCURACY_CONTRACT.md) and the
  [performance and accuracy playbook](PERFORMANCE_AND_ACCURACY.md) are unchanged.
  Priorities change the order of work, not what counts as correct.
- The original CSS → SSS → four-stock Mario/Final Destination → Results → CSS
  route remains the regression baseline. Every change must keep it working.
- Human-only gates are open but not scheduled. These are physical controllers
  ([#83](https://github.com/ericvanlare/melee-web/issues/83)), foreground timing,
  audible output and latency against photons. Do not block other work on them.
- Desktop browsers come first. Mobile is best-effort: keep field diagnostics and
  memory reductions that also help desktop, but start no mobile-only campaign
  until H1 passes on desktop ([#134](https://github.com/ericvanlare/melee-web/issues/134)).
- Run at most one timing-sensitive browser workstream at a time on the shared
  test machine. Functional browser checks, such as A1 determinism, do not take
  that lane.

## Acceptance boundaries

The regression route's evidence is recorded in the
[route ledger](ORIGINAL_MENU_ROUTE_CAPTURE.md), the
[recorded-session receipt](evidence/recorded-session-state-v1.json) and
[STATUS](../STATUS.md). Each establishes only its declared builds, inputs, state
fields and observations. None establishes general game equivalence, exact pixels
or PCM, live input, foreground timing, physical-controller acceptance or
tournament readiness.

Each boundary below stays separate:

| Boundary | Bounded goal | Evidence scope |
| --- | --- | --- |
| Original comparison | Extend source-phase state and menu/transition comparisons from retained recorded sessions when a new divergence is observed. | Use the existing [comparison](ORIGINAL_COMPARISON.md) and [recorded-session](RECORDED_SESSION_STATE.md) tools; preserve first divergence, input order, source heap/RNG context and the complete failing sequence. |
| Live and physical input | Establish one supported controller profile, including assignment, simultaneous input, analog thresholds, digital clicks, focus and reconnect behavior. | Replay input is conditional evidence. Physical input and end-to-end latency need their own arranged session and receipt under [#35](https://github.com/ericvanlare/melee-web/issues/35), coordinated with [#5](https://github.com/ericvanlare/melee-web/issues/5) and [#28](https://github.com/ericvanlare/melee-web/issues/28). |
| Visual output | Compare named menu, stage, combat, HUD, KO/respawn, Results and return checkpoints on a frozen baseline. | Declare dimensions, camera, output assumptions, source phase, masks and tolerances; keep visual differences separate from state and timing claims. |
| Audio | Validate sustained menu, gameplay, transition, Results and teardown playback against an independent reference. | Keep source requests, bank ownership, emitted PCM, audible observations and replacement limitations separate; production packaging is governed by [AUDIO_PRODUCTION](AUDIO_PRODUCTION.md). |
| Sustained performance | Run the named Release route across cold and warm application/driver-cache conditions, including preparation, active play, repeated matches and teardown. | Use [hitch capture](HITCH_CAPTURE.md) and the [performance playbook](PERFORMANCE_AND_ACCURACY.md); report source deadlines, browser callback gaps, audio underruns and memory separately. |
| Networked play | Keep lockstep, rollback and cross-play claims separate from single-machine accuracy. | Per-tick checksum agreement between peers, input delivery and delay, desync and disconnect handling, and measured restore/re-simulation cost. A networked session is never retail-equivalent evidence by itself. |

## How to run an execution issue

1. Start from the current candidate and retained receipts. Preserve current
   local-resource, audio and headless-browser policies.
2. When a comparison or player run fails, reduce the first failing boundary.
   Record the observable outcome, smallest experiment, pass criteria, exclusions,
   existing solutions checked and stopping rule before another long run. Reuse
   existing observers and transports; do not create a parallel replay or
   benchmark framework.
3. Revalidate relevant receipts after source, runtime, build, browser, controller
   or package changes. Keep preparation, active play, teardown and failures
   distinguishable in every report.

## Admission criteria for a named profile

Before final runs, freeze:

- the Release artifact and source revision
- the machine, OS, browser, display and power settings
- the controller and its mapping
- the disc identity, the input and scenario inventory, and the comparison schema

Report each of these as a separate claim: source-state agreement, menu and
transition order, live and physical input, visuals, PCM and audio continuity,
latency, and performance. A failure consumes its declared attempt. A repair
requires a new scoped campaign and cannot erase the earlier result.

The profile is admitted only when its declared route and gates have receipts in
STATUS, with preparation and active-play limits stated. Equivalence with original
hardware PCM or framebuffer, all-device support, complete roster and mode
coverage, and tournament acceptance remain broader work.

## Later scope and historical evidence

Broader browser and device support follows a stable reference profile. Custom
content and non-vanilla modes follow the boundary in
[#9](https://github.com/ericvanlare/melee-web/issues/9). The bounded comparison
with other browser ports in [#81](https://github.com/ericvanlare/melee-web/issues/81)
can inform implementation choices. It is not an accuracy oracle or an acceptance
dependency.

The earlier Results-skipping Mario/Final Destination route and the silent-alpha
checks remain historical evidence for their original scopes. Merged Results and
production-audio work is implementation and package evidence. On its own, it
does not establish the full route, general equivalence or full-game fidelity.
[NEXT_PHASE](history/NEXT_PHASE.md) preserves the early menu integration notes
and is not a second work queue.
