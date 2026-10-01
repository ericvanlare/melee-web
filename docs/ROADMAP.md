# Acceptance roadmap

The goal is full vanilla Melee compiled to WebAssembly, preserving original
behavior without a shipped PowerPC interpreter or JIT. Complete and verify the
supported playing experience before expanding scope.

This page orders work. GitHub issues own bounded tasks and completion criteria;
[STATUS](../STATUS.md) owns observed results and scoped receipts. Do not copy
changing measurements here, and do not treat a closed implementation issue,
successful build or short replay as a complete acceptance result.

## Current supported route

The ordinary player route is:

**Original CSS → original SSS → four-stock Mario versus Mario on Final
Destination → original Results → original CSS.**

The current route ledger and recorded-session receipt in [STATUS](../STATUS.md)
establish only their declared builds, inputs, state fields and observations.
They do not establish general game equivalence, exact pixels or PCM, live input,
foreground timing, physical-controller acceptance or tournament readiness.

The acceptance work stays separate across these boundaries:

| Boundary | Bounded goal | Evidence scope |
| --- | --- | --- |
| Original comparison | Extend source-phase state and menu/transition comparisons from the retained recorded session when a new divergence is observed. | Use the existing [comparison](ORIGINAL_COMPARISON.md) and [recorded-session](RECORDED_SESSION_STATE.md) tools; preserve first divergence, input order, source heap/RNG context and the complete failing sequence. |
| Live and physical input | Establish one supported two-controller profile, including assignment, simultaneous input, analog thresholds, digital clicks, focus and reconnect behavior. | Replay input is conditional evidence. Physical input and end-to-end latency need their own arranged session and receipt under [#35](https://github.com/ericvanlare/melee-web/issues/35), coordinated with [#5](https://github.com/ericvanlare/melee-web/issues/5) and [#28](https://github.com/ericvanlare/melee-web/issues/28). |
| Visual output | Compare named menu, stage, combat, HUD, KO/respawn, Results and return checkpoints on a frozen baseline. | Declare dimensions, camera, output assumptions, source phase, masks and tolerances; keep visual differences separate from state and timing claims. |
| Audio | Validate sustained menu, gameplay, transition, Results and teardown playback against an independent reference. | Keep source requests, bank ownership, emitted PCM, audible observations and replacement limitations separate; production packaging is governed by [AUDIO_PRODUCTION](AUDIO_PRODUCTION.md). |
| Sustained performance | Run the named Release route across cold and warm application/driver-cache conditions, including preparation, active play, repeated matches and teardown. | Use [hitch capture](HITCH_CAPTURE.md) and the [performance playbook](PERFORMANCE_AND_ACCURACY.md); report source deadlines, browser callback gaps, audio underruns and memory separately. |

## Immediate work order

1. Start from the current candidate and retained receipts. Keep the player route
   on the original CSS/SSS path and preserve current local-resource, audio and
   headless-browser policies.
2. When a comparison or player run fails, reduce the first failing boundary and
   record the observable outcome, smallest experiment, pass criteria, exclusions,
   existing solutions checked and stopping rule before another long run. Reuse
   existing observers and transports; do not create a parallel replay or benchmark
   framework.
3. Complete the named physical-controller profile and the sustained audio/
   performance campaign as independent gates. A headless functional result does
   not satisfy foreground timing, physical input or audible checks.
4. Revalidate relevant receipts after source, runtime, build, browser, controller
   or package changes. Keep preparation, active play, teardown and failures
   distinguishable in every report.

## Admission criteria for a named profile

Freeze the Release artifact, source revision, machine, OS, browser, display and
power settings, controller and mapping, disc identity, input/scenario inventory,
and comparison schema before final runs. Report source-state agreement, menu and
transition order, live/physical input, visuals, PCM/audio continuity, latency and
performance as separate claims. A failure consumes its declared attempt; a repair
requires a new scoped campaign and cannot erase the earlier result.

The profile is admitted only when its declared route and gates have receipts in
STATUS, with preparation and active-play limits stated. Original hardware PCM or
framebuffer equivalence, all-device support, complete roster/mode coverage and
tournament acceptance remain broader work.

## Broaden the verified experience

Choose a named rotation from integrated fighters and stages before adding content.
Use the [fighter](ADDING_CHARACTERS.md) and [stage](ADDING_STAGES.md) checkpoints,
preserve source identities and authored bounds, and extend the same lifecycle,
comparison, visual, audio, input and performance boundaries. Use the
[full-game inventory](FULL_GAME_PORT.md) for remaining rules, menus, saves,
single-player modes, movies, collections and service dependencies.

## Later scope and historical evidence

Broader browser/device support follows a stable reference profile. Netplay,
online services and custom content follow the vanilla boundary in
[#9](https://github.com/ericvanlare/melee-web/issues/9). The bounded competitor
comparison in [#81](https://github.com/ericvanlare/melee-web/issues/81) can inform
implementation choices but is not an accuracy oracle or acceptance dependency.

The earlier Results-skipping Mario/Final Destination route and silent-alpha checks
remain historical evidence for their original scopes. Merged Results and
production-audio work provide implementation and package evidence; they do not
by themselves establish the full route, general equivalence or full-game
fidelity. [NEXT_PHASE](NEXT_PHASE.md) preserves the early menu integration notes
and is not a second work queue.
