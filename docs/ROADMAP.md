# Acceptance roadmap

The goal is full vanilla Melee compiled to WebAssembly, preserving original
behavior without a shipped PowerPC interpreter or JIT. Complete and verify the
supported playing experience before expanding scope.

This page owns priority and execution order. GitHub issues own bounded tasks and
completion criteria; [STATUS](../STATUS.md) owns observed results and scoped
receipts. Closed implementation issues do not establish every acceptance gate.

## Now — Reliable local versus v1

The active [milestone](https://github.com/ericvanlare/melee-web/milestone/1)
targets one frozen desktop configuration and this ordinary player route:

**Original CSS → original SSS → four-stock Mario versus Mario on Final
Destination → original Results → original CSS, for three consecutive matches
without reloading.**

Use two human players for physical-input and uninterrupted-play checks. The
retained four-Mario CPU9 three-match reference is a separate original-versus-browser
comparison. Neither substitutes for the other. Preserve source heap/RNG history;
do not splice fresh-process captures or reset the page to make a sequence pass.

| Work | Owner | Execution boundary |
| --- | --- | --- |
| First session divergence and complete reference sequence | [#82](https://github.com/ericvanlare/melee-web/issues/82), child of [#6](https://github.com/ericvanlare/melee-web/issues/6) | Owner-paused until explicitly resumed; planning does not restart it |
| One two-controller profile and measured latency | [#83](https://github.com/ericvanlare/melee-web/issues/83), child of [#35](https://github.com/ericvanlare/melee-web/issues/35) | Prepare the existing tools/profile, then arrange the physical session |
| Uninterrupted three-match audio and performance | [#84](https://github.com/ericvanlare/melee-web/issues/84), child of #35 | Share the controller candidate/profile; use the existing cold/warm sequence protocol |
| Fixed comparison of other browser ports | [#81](https://github.com/ericvanlare/melee-web/issues/81) | Parallel research outside the milestone; never blocks a known fix |

Start with retained evidence, profile/scenario preparation and known player
failures. Reduce a failure before expanding instrumentation. Study relevant
existing implementations with source/attribution links as part of each fix.
Do not create another replay framework, general benchmark platform or open-ended
competitor audit. After two experiments at one boundary, reduce the reproducer
or request a bounded review before another long run.

### Finish line

Freeze Release artifacts, machine/OS/browser/display/power profile, controllers,
input/scenario inventory and criteria before final runs. Existing protocols and
thresholds apply; the issue bodies define finite attempt plans. A new failure
needs a reduced diagnosis and an explicit new campaign, not silent retries.

Close the milestone only when #82, #83 and #84 have their declared evidence and
the final candidate retains all three gates. Link exact receipts from STATUS.
Component fixes do not close acceptance issues; relevant changes after a receipt
require scoped revalidation. Report source-state agreement, physical input/latency,
audible continuity and performance separately.

Verify KO/respawn, outcome/Results, pause/resume, No Contest recovery, loading,
audio ownership and teardown within the named profile. Report preparation,
memory and failures separately from active play. Original framebuffer/hardware
PCM equivalence, all-device support and tournament acceptance remain separate
under #35 and the [accuracy contract](ACCURACY_CONTRACT.md).

Routine browser work stays headless through shared tools. Foreground timing,
physical controllers and audible checks require an arranged foreground session
or a separate machine; otherwise the gate remains unrun. See
[browser validation](HEADLESS_BROWSER_VALIDATION.md) and
[consecutive-match timing](HITCH_CAPTURE.md#consecutive-match-track-after-holdouts).
Do not relax accuracy rules to meet the milestone.

## Next — Broaden the verified experience

Choose a named rotation from already integrated fighters/stages before adding
content. Extend original comparisons, repeated-match ownership and physical
cold/warm play using the [fighter](ADDING_CHARACTERS.md) and
[stage](ADDING_STAGES.md) checkpoints.

Broaden live sampling/source-draw validation under
[#28](https://github.com/ericvanlare/melee-web/issues/28), controller routing
under [#5](https://github.com/ericvanlare/melee-web/issues/5), and independent
visual/PCM/device acceptance under #35. Defects blocking v1 are fixed during Now;
completing every broad umbrella is not a v1 prerequisite.

## Later — Complete vanilla, then optional features

Use the [full-game inventory](FULL_GAME_PORT.md) for remaining content, rules,
menus, saves, single-player modes, movies and collections. Broader browser/device
support follows a stable reference profile. Netplay and custom content follow
the vanilla boundary in [#9](https://github.com/ericvanlare/melee-web/issues/9).
A coverage dashboard supports this work; it is not a playable-slice prerequisite.

## Historical scope

[#34](https://github.com/ericvanlare/melee-web/issues/34) is closed Results/return
implementation history; #82–#84 own remaining v1 acceptance.
[#33](https://github.com/ericvanlare/melee-web/issues/33) has a scoped
[holdout result](CURRENT_RUNTIME_HOLDOUTS_20260919.md); preserve earlier failed
campaigns and the unresolved historical cause. It does not validate new sequences.

The earlier Mario/FD route skipped Results and the initial alpha was silent.
Original Results is now required and [production audio](AUDIO_PRODUCTION.md)
has an audited release path; these facts do not establish full-game fidelity.
[Early menu notes](NEXT_PHASE.md) remain history, not an alternative work queue.
