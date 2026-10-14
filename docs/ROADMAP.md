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

Set by the owner on 2026-10-05, with the owner-adopted
[2026-10-08 architect check-in](https://github.com/ericvanlare/melee-web/issues/158#issuecomment-6069469010)
and [execution response](https://github.com/ericvanlare/melee-web/issues/158#issuecomment-6069737799)
setting the milestone process. The owner’s October 12 match-only rollback
decision supersedes the earlier online sequence (see the [architects’ log](ARCHITECTS.md)).
Two tracks run in parallel. When they compete
for the same scarce resource, such as the single browser-timing lane on the test
machine or a reviewer's attention, take the lowest-numbered unblocked item and
prefer Track A.

### Shared prerequisite: gameplay headroom

**H1. No forced timing pauses in desktop gameplay.** On the desktop reference
profile, a natural four-stock match through the original menus completes without
a timing-guard pause. Both tracks depend on this. Lockstep stalls both players
on any host stall, rollback must re-simulate several ticks inside one tick's
budget, and competitive play cannot tolerate pauses. H1 gates live-play admission in A4 and B5. The
bounded functional and feasibility steps A1–A3 can proceed without it. Work starts from the open
pause classification in [#116](https://github.com/ericvanlare/melee-web/issues/116).
The fix must come from the runtime, never from a threshold, frame-rate,
skipped-tick or auto-Resume change. The current first-use respawn boundary and
retained failures are recorded in the [scoped H1 status entry](status/2026-10-13-h1-first-use-respawn.md).

### Track A: match-only rollback online

**Target.** Two people on different machines play the **Slippi rules** profile
in the browser with rollback confined to the match. **Vanilla is offline-only**
and remains the accuracy reference. Both profiles use the player's unmodified
USA revision 1.02 disc. A settings toggle selects Vanilla or Slippi rules; that
profile implementation is planned, not already available.

Original CSS and SSS run locally. Peers exchange selections and a versioned
match-start context: seed, characters/costumes, stage, rules, ports, content and
profile identities. Both must agree on the context and the actual constructed
match state before play. Rollback runs only inside the match. Scene transitions,
Results and rematch remain local, driven by a confirmed outcome and a new
agreement for the next match. Never roll back or lockstep those scenes.

Reuse the existing determinism work, input protocol, checksums, relay, room UI
and runtime-owned session. Keep the current full-route lockstep path as an
interim implementation and regression tool; stop extending it. [Decision 017](ARCHITECTURE.md#017--online-rollback-is-match-scoped-with-explicit-rules-profiles)
supersedes decision 016 for future online work.

| Step | Outcome |
| --- | --- |
| **A1. Determinism** | Preserve scoped shared-input/checksum evidence; extend browser/architecture coverage only through bounded experiments. It does not establish complete state ownership. |
| **A2. Interim local lockstep** | Retain the original CSS → SSS → match → Results → CSS functional baseline. No new menu/Results/rematch lockstep campaign. |
| **A3. Rollback feasibility and rules inventory** | [#318](https://github.com/ericvanlare/melee-web/issues/318), continuing [#115](https://github.com/ericvanlare/melee-web/issues/115): measure match-state size and snapshot/restore cost, full source tick/draw with GPU submission suppressed, and host-side effects. Add a match-only restored-snapshot repeat-tick checksum test after safe host ownership; report its exercised coverage alongside whether up to seven corrective ticks fit the declared budget. In parallel inventory Slippi gameplay changes and patch licenses. |
| **A4. Match-scoped online play** | After the feasibility decision, implement restore/prediction and an agreed match-start barrier. Deliver the Vanilla/Slippi rules settings toggle and versioned Slippi rules profile under [#9](https://github.com/ericvanlare/melee-web/issues/9), checked against a pinned Slippi Dolphin release bundle. Complete matches with controlled corrections, confirmed outcomes, local Results/rematch, exact state checks and clean disconnect. |
| **A5. Slippi desktop cross-play** | Extend the pinned profile and transport only after browser rollback correctness; demonstrate actual desktop interoperability. Browser networking or a profile name alone cannot establish cross-play. |

Every online session carries an explicit mode identity under #9. Slippi rules
may also be selected offline, with its own evidence. Source gameplay changes
must be enumerated, licensed and compared against the selected Slippi build;
never infer the complete profile from UCF alone or leak modifications into
vanilla. The chosen release bundle and per-mode stage rules need explicit pins.

**Completed prerequisites:** [#316](https://github.com/ericvanlare/melee-web/pull/316)
records the bounded keyboard-combat milestone, including a credited stock loss
and complete route, in the [combat receipt](status/2026-10-12-simulated-network-combat.md).
The complete two-browser route through the deployed staging Worker is recorded
in [#317](https://github.com/ericvanlare/melee-web/pull/317) and the
[readiness receipt](status/2026-10-11-local-room-ui-readiness.md).
Treat the public route as functional **transport validation**, not acceptance
or a reason to extend menu lockstep. RTT, fixed input delay, network-wait
counters, exact producer identities, failures and unmeasured wait durations
belong in that receipt. The isolated workers.dev Worker remains deployed idle
under the owner's Workers Free/$0 limit; no paid, billing, account/security,
DNS or production changes are authorized. Do not repeat successful captures
without a new hypothesis.

**Owner-directed manual alpha (pending):** On October 14 the owner deferred the
human PvP trial and directed the next autonomous sprint. Continue the drawn
rollback and Stadium milestones below without waiting for human testing; keep
physical-input, audible-output, foreground-timing and two-machine gates unrun.
The owner selected a live hosted
room page on the existing isolated staging Worker and declined local-package
setup as the user-facing path. The proposed two-Mac trial uses the existing
interim lockstep route: keyboard P1 on the host and controller P2 / Port 2 on
the joiner. The final candidate passed build/regression validation, a second
deployment's static HTTP checks and a bounded CSS/Host/Join/Leave check. The
first candidate's full-route attempt failed while progressing through Results;
the short check did not complete a full route. The two-Mac trial remains
pending in the [scoped boundary](status/2026-10-13-hosted-staging-room.md).
This experiment does not change or satisfy the match-only rollback target in
A4, and does not establish broader #288 acceptance.

**Feasibility checkpoint:** #318 retains source-only snapshot/replay measurements
and one terminal browser tick with full source draw and no GPU submission in its
[single receipt](status/2026-10-12-match-only-rollback-feasibility.md). The current
recommendation is no-go for naive browser-heap rewind or a combined seven-tick
budget claim: renderer content/cache ownership, audio clocks and asynchronous
file destinations are not closed. This is not a rejection of rollback or proof
that it is too slow. The next ownership design belongs under #115; A4 needs an
explicit go decision after those gates. A later multi-frame diagnostic must first
prove useful safe renderer reuse, preserving original draw callbacks. The
Slippi semantic/license/reference inventory continues under #9; no profile is
ready to port. Keep component measurements separate from browser acceptance.

After this bounded measurement checkpoint, investigate first-use respawn-platform
and common-effect pipeline preparation under H1/#116. Preparation must not consume
hidden gameplay ticks/RNG, change draw order or relax timing guards. Keep H1
incident recording on for combat and simulated-network runs; a forced pause is
retained H1 evidence. The owner identifies the #316 respawn preparation stall as
the second mid-play first-use incident; its failure stays retained.

The simulated-network milestones #307–#309 and combat #313 retain their scoped
results and failures. The unstarted #314 full-route/rematch expansion is
superseded by the match-only direction. The strict jump/landing observation gap
under #308 remains open, without an unchanged retry or weaker assertion.

The owner's October 13 fused-operation hypothesis is checkpointed under
[#274](https://github.com/ericvanlare/melee-web/issues/274). The default-off
original-site collision diagnostic leaves the retained first Bowser divergence
unchanged; the [existing B4 boundary](status/2026-10-08-b4-inner-geometry-boundary.md)
owns the comparison and producer identities. The stopping rule is applied:
do not repeat this replay or adopt the arithmetic. Further diagnosis needs a
smaller original/browser operand comparison. The Falcon velocity-Y candidate's
activation remains unobserved. Preserve exact bits and original arithmetic:
no blanket contraction, fast-math, widened comparisons or deadline changes.
Any later adopted arithmetic change reopens exact-receipt validation. The
[architect response](https://github.com/ericvanlare/melee-web/issues/158#issuecomment-6147291073)
records the parallel Slippi behavior mapping, pending owner licensing decision,
and deferred A5 gateway/cadence research. Copy no related-project code, and send
no automated traffic to Slippi production services or real accounts.

The owner-approved October 13 sprint targets one same-instance Mario/FD
checkpoint → original tick/full draw → restore → repeat comparison under
[#318](https://github.com/ericvanlare/melee-web/issues/318). First close the
reachable renderer content/handle, native audio/output and file-completion
owners. Freeze actual clock and input decisions, then return from native code
before copying memory. A normal-frame renderer observation and native idle-owner
controls are prerequisites, not browser restore evidence. Only a reviewed
one-tick pass can open a longer correction measurement; no combined budget
claim follows from separate timings. The existing terminal diagnostic remains
one-shot. The [sprint queue](https://github.com/ericvanlare/melee-web/issues/158)
owns work-item ordering and stopping rules.

Pokémon Stadium C3 [#251](https://github.com/ericvanlare/melee-web/issues/251) /
[draft #301](https://github.com/ericvanlare/melee-web/pull/301) remains the next
Track B milestone. Reuse the retained PadPoll/draw-order reduction and original
owner evidence; no duplicate capture. Track A has first claim on the shared
capture lane. Physical controllers, foreground timing, audible output and a
two-machine human session remain separate unrun gates.

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

**Immediate Track B queue:** complete these small milestones before further
fighter or stage depth:

1. [Sudden Death routing #291](https://github.com/ericvanlare/melee-web/issues/291):
   preserve original tied-timeout participation and resolution through Results
   and CSS, with non-tied timeout and elimination controls.
2. [Competitive Rules Plus receipt #292](https://github.com/ericvanlare/melee-web/issues/292):
   four stock, 8:00, all items and switches off, pause off, friendly fire on,
   damage ratio 1.0 and handicap off, followed by a natural non-tied timeout to
   Results and CSS. No accelerated timer or forced outcome.
3. [Sparse P1+P3 singles #293](https://github.com/ericvanlare/melee-web/issues/293):
   preserve source port identities through input, HUD, outcome and the full
   route, retaining contiguous singles and four-player Teams as controls.
4. [Competitive inventory/docs refresh #294](https://github.com/ericvanlare/melee-web/issues/294):
   reconcile implementation and evidence without promoting an unobserved gate.

Then [the weaker-fighter lineup #295](https://github.com/ericvanlare/melee-web/issues/295)
covers Sheik, Jigglypuff, Peach and Ice Climbers with existing strict comparison
tools. Timebox each first divergence; after two experiments at one boundary,
retain a reduced reproducer and move to the next fighter. Bowser's
[#274 checkpoint](https://github.com/ericvanlare/melee-web/issues/274) remains
unresolved after the reconstructed-prefix attempt budget. The owner-directed
FMA diagnostic above also leaves the target unresolved and has reached its
stopping rule. The failed reconstruction and both bounded investigations remain
retained; further fighter work follows the breadth-first queue.

**Current #304 boundary.** The bounded original Zelda-to-Sheik owner-prefix milestone is complete for [issue #304](https://github.com/ericvanlare/melee-web/issues/304), with evidence in the [scoped lineup status](status/2026-10-09-weaker-fighter-comparison-lineup.md); strict two-human browser comparison remains separate. Stadium C3 under [#251](https://github.com/ericvanlare/melee-web/issues/251) remains checkpointed while #318 rollback feasibility has first claim on the capture lane. Its existing [CSS/SSS preparation status](status/2026-10-06-pokemon-stadium-c1a-css-sss-preparation.md) records the route boundary; current C3 receipts remain in #251. Public #288 now uses the owner-cleared isolated staging relay.

The next Stadium milestone is [C3 idle in-match lifetime #251](https://github.com/ericvanlare/melee-web/issues/251),
with its remaining prerequisites counted and tracked inside that issue and PR.
First reduce the retained PadPoll-inside-draw mismatch against original source
ordering. Complete the actual original input/draw/state/RNG reference through
3,500 post-GO ticks, counted from the observed GO event, then compare runtime
lifetime A, checked retirement/recreation and lifetime B with per-owner RNG.
Historical no-draw lifetimes remain separate; do not invent neutral rows or
hardcode the historical GO tick. Prerequisite fixes are commits within C3,
not isolated ownership PRs. Transformations, broader rendering, browser
admission and whole-stage acceptance remain separate. Source-only
and documentation work may overlap when ownership permits; Track A retains
priority for shared capture resources.

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

Use one issue and PR per milestone, with prerequisite steps as checklist items
and commits. Aim for a handful of merges a day. From 2026-10-08 onward, keep one
status entry per boundary and update it in place as the boundary advances,
preserving historical results, failures and exact producer/evidence identities.
Do not rewrite or consolidate past entries. Evidence rules, independent review,
current-main validation and complete current-head CI before merge are unchanged.

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
