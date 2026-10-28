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
decision supersedes the earlier online sequence. The October 17 decision adds
no-draw Slippi-profile correction ticks, authorizes the isolated GPL profile,
and parks official-service cross-play (see the [architects’ log](ARCHITECTS.md)).
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

**October 28 priority:** The owner has directed autonomous work toward working
rollback and parked the next Stadium experiment. The browser restore boundary
in #341 remains open; previously rejected renderer-owner and UCF reference
follow-ups remain frozen. [#356](https://github.com/ericvanlare/melee-web/issues/356)
advances the independent bounded match input timeline, reusing the existing PAD
format and synchronous source-only fixture. Its
[scoped correction evidence](status/2026-10-28-match-input-timeline.md) does not
admit a live restore adapter or satisfy A3/A4. Keep integration and full
validation at the milestone boundary.

**Target.** Two people on different machines play the **Slippi rules** profile
in the browser with rollback confined to the match. **Vanilla is offline-only**
and remains the accuracy reference. Both profiles use the player's unmodified
USA revision 1.02 disc. The planned settings toggle selects Vanilla or **Slippi-compatible rules
(unofficial)**, with a short notice that this project is not affiliated with or
endorsed by Project Slippi. No Slippi logos or branding. Profile implementation
is in progress; the toggle and complete profile are not yet available.

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
| **A3. No-draw rollback and profile dependencies** | [#318](https://github.com/ericvanlare/melee-web/issues/318) / [#115](https://github.com/ericvanlare/melee-web/issues/115), with [#9](https://github.com/ericvanlare/melee-web/issues/9): inventory draw-to-simulation writes and implement their isolated Slippi-profile equivalents. Prove drawn source execution versus no-draw restored-interval checksum agreement with falsification controls. Only the present frame draws. Then implement UCF 0.84 and the remaining versioned online-profile behaviors under #9, checked against a pinned Slippi Dolphin reference. Historical drawn measurements remain distinct. |
| **A4. Match-scoped online play on our relay** | After A3, measure state capture/restore, up to seven no-draw corrections plus one present draw, and remaining audio/file/host effects under #318/#115. With an explicit go decision, deliver the unofficial profile toggle, negotiated match start, restore/prediction and one complete two-browser match on our own relay, with corrections, confirmed outcome, local Results and clean disconnect. No official Slippi service traffic. |
| **A5. Official Slippi-user cross-play — parked indefinitely** | Requires a separate explicit owner decision. No official matchmaking, accounts or replay-upload traffic, and no contact on the project’s behalf. Retain prior source research; it authorizes no service use. |

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
human PvP trial and directed the next autonomous sprint. Continue the no-draw
profile rollback and Stadium milestones below without waiting for human testing; keep
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
explicit go decision after those gates. The October 17 pivot replaces drawn corrective ticks with simulation-only
corrections in the Slippi profile; presentation and remaining host effects still
need explicit ownership. The owner has authorized profile implementation in the
isolated GPL scope; complete semantics and reference coverage remain open under
#9. Keep component measurements separate from browser acceptance.

The first-use respawn preparation component merged in [#322](https://github.com/ericvanlare/melee-web/pull/322).
Continue broader common-effect and sustained-play work under H1/#116, using the
[existing H1 boundary](status/2026-10-13-h1-first-use-respawn.md). Preparation must not consume
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
records the historical Slippi behavior mapping and licensing question. The
October 17 owner decision now permits the isolated profile implementation and
parks A5 official-service cross-play indefinitely. Copy no related-project code, and send
no automated traffic to Slippi production services or real accounts.

The October 16 [execution sprint](https://github.com/ericvanlare/melee-web/issues/158#issuecomment-6227720936)
is checkpointed. **Items 2–9 are paused**: texture-cache generations, selected
renderer ownership and drawn corrective ticks are no longer the active path.
Resume them only for a demonstrated dependency the no-draw model cannot remove,
with the evidence and reasoning posted on #158 before implementation.

The next coherent milestone combines render-to-simulation dependency fixes with
a no-draw synctest. Start with the actual fighter dynamics/matrix producer and
the offscreen/magnifier damage path; inventory other draw writes and consumers.
Implement the profile behavior in our own C under `src/slippi_profile/`, alongside
its tests and per-behavior patch mapping, under GPL-3.0-or-later. The separate
[license scope](../LICENSE_SCOPE.md) and [notices](../THIRD_PARTY.md) preserve the
rest of the repository's existing terms. Builds including the profile follow
GPL terms with public corresponding source and reproducible build instructions.

Compare the drawn forward interval against restored no-draw re-simulation under
the same profile, setup and literal inputs. Preserve strict simulation checksums,
first-divergence reporting and negative controls; render-only bytes do not become
simulation coverage. Fail on unknown simulation owners or unsafe external effects
before restoration. Never claim a source-only repeat proves draw independence.
After two failed experiments reduce the reproducer or obtain bounded review.
Only a reviewed pass opens a longer correction-budget measurement. The existing
[rollback boundary](status/2026-10-12-match-only-rollback-feasibility.md) owns all
historical measurements and the current scope; no whole-session claim follows.

Pokémon Stadium C3 [#251](https://github.com/ericvanlare/melee-web/issues/251) /
[#301](https://github.com/ericvanlare/melee-web/pull/301) merged after independent
review, full regression, affected builds and complete publication-head CI. The
[C3 boundary](status/2026-10-07-pokemon-stadium-profile-oninit.md) retains exact
identities, raw failures and coverage limits. C4 original request/callback
observations remain scoped under [#343](https://github.com/ericvanlare/melee-web/issues/343)
and its [evidence boundary](status/2026-10-27-stadium-first-form-latency.md).
The conditional first-Fire runtime component belongs to
[#349](https://github.com/ericvanlare/melee-web/issues/349). The separate first-Water
route under [#351](https://github.com/ericvanlare/melee-web/issues/351) has a
scoped recorded-callback browser result, independent raw review, final regression,
and merged [PR #353](https://github.com/ericvanlare/melee-web/pull/353)
with exact-head CI passed; see the
[C5 boundary](status/2026-10-27-stadium-archive-service.md). Issue
[#354](https://github.com/ericvanlare/melee-web/issues/354) has one completed
default-off native Water collision-query component. The existing route comparator
and native query validator replay, independent raw review and full regression
passed; see its [scoped evidence boundary](status/2026-10-27-stadium-water-collision.md).
No original collision-query counterpart is available, so original collision-query
parity remains unobserved and outside #354. This component does not extend the
#351 browser comparison. Neither route establishes per-owner RNG parity or a
general completion schedule; ordinary/public transformations and general loading
remain unadmitted. Exact-head CI for the publication containing the #354 docs is
tracked on that PR.
Ranked/Unranked use unfrozen Stadium, so C4–C8 transformations remain in scope.
Track A retains first claim on the shared capture lane. Physical controllers,
foreground timing, audible output and a two-machine human session remain separate
unrun gates.

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

**Current #304 boundary.** The bounded original Zelda-to-Sheik owner-prefix milestone is complete for [issue #304](https://github.com/ericvanlare/melee-web/issues/304), with evidence in the [scoped lineup status](status/2026-10-09-weaker-fighter-comparison-lineup.md); strict two-human browser comparison remains separate. Public #288 uses the owner-cleared isolated staging relay.

**Stadium C3 → C4.** The defined C3 idle, selected owner/RNG lifetime, natural
stockout/mode9 and natural mode14 functional comparisons are complete at their
declared scopes, including checked Results/CSS and retirement. Independent raw
and scoped gate review support those outcomes; #301 is merged. The scoped receipt
records the integration validation. Broader allocator/arbitrary-writer coverage is a limit,
not a newly waived gate; full allocator journaling remains separate. Whole-stage
accuracy, public Stadium admission and C4–C8 are not accepted by C3.

The first **C4 (#343)** original recording and its fixed-input repeatability
step pass at the declared scope. The [C4 receipt](status/2026-10-27-stadium-first-form-latency.md)
tracks observed forms, retained failures and remaining coverage. Resolve the
general scheduling rule and missing form samples before runtime transformation
release; stop at the first changed loading latency under fixed inputs/settings.
The archive-service milestone [#345](https://github.com/ericvanlare/melee-web/issues/345)
composes exact disc tails, owned destinations and quiescent teardown; its
[component receipt](status/2026-10-27-stadium-archive-service.md) records the
merged service evidence and current source-slot boundary. The [#347 follow-up](https://github.com/ericvanlare/melee-web/issues/347) adds
owned form catalogs, exact source-slot/header/buffer binding, original file-entry
metadata and checked retirement. The component receipt owns its review and
validation results. The selected first-Fire and first-Water diagnostic routes
exercise World integration and checked graph retirement only for their declared
profiles. Ordinary/public transformations remain disabled pending a justified
general completion schedule and remaining-form coverage. Reuse the existing
route and retained evidence; do not repeat accepted functional captures or
invent neutral rows/GO coordinates.
Track A retains priority for shared capture resources.

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
