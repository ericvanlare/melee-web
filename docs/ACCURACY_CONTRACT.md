# Accuracy contract and native-menu deliverable

The intended product is an accurate source port of vanilla Melee for the browser,
without a shipped PowerPC interpreter or JIT. Smooth performance, successful
compilation, and passing the current test suite do not establish that accuracy.
Tournament acceptance is an external organizer decision, not a label this
project can award itself. Publication/provenance work is separate from both
technical accuracy and event acceptance.

## First deliverable

Original in-game character select → original in-game stage select → playable
four-stock Mario versus Mario on Final Destination → original character select.
Skip the results screen. Preserve the original menu assets, animation, cursor,
confirmation and transition behavior for the supported path. HTML imitations,
static screenshots and a forced-stage bypass do not satisfy this deliverable.

Both players use human input. Retain four stocks across the loop. All characters
are unlocked in the intended default profile, while unsupported characters and
stages are unavailable in this development slice. Keep these as separate concepts:
do not disguise absent implementation as unlock progression. No additional rules
or name-entry menus are required yet; unsupported exits must not enter incomplete
code. This restricted slice is not the final full vanilla product.

Falco and Battlefield now have typed source-content rows, runtime asset
manifests and focused development traces. Raw PAD input in a Release browser has
selected Falco/Mario and Battlefield through the original CSS/SSS and rendered
the fighters, stage and their distinct four-stock icons. Native menu loops also
complete this route on both admitted stages. Falco/Battlefield remain outside
this first-deliverable claim until complete ordinary input, uninterrupted audio,
browser-loop and reference checks establish their behavior. Stock icon identity
must continue to come from the source character/fighter mapping; an icon index or
linked asset alone is not acceptance evidence.

The native CSS/SSS chain is the canonical player path. The temporary HTML
selection UI is removed from the player flow; useful developer diagnostics and
the legacy native-menu URL redirect remain. The complete loop needs normal input
testing, original selection/configuration handoff, KO/respawn/outcome, and a
second match after returning. A link probe or automatically scripted match is
only partial evidence.

Each requested CSS, SSS or match transition may enter an explicit preparation
phase. The phase must be visible, wait for the audio transport's disabled-state
acknowledgement, keep source simulation and source drawing out of construction,
and report its duration separately from live callbacks. It must preserve source
ownership, RNG and input order and arm the live clock without hidden ticks.

The current validated checkpoint carries the full source `StartMeleeData`
through `fn_8016DCC0`, runs the original supported VS `OnFrame`, and passes
Entry/Ready, source pause/resume, No Contest, and match-ending checks in two
Node cycles. Expanded native routes also complete Mario/Falco matches on Final
Destination and Battlefield and exercise Falco's laser and special-action
families across every hydrated costume. The evidence is recorded in
`work/native-active-telemetry-run.log`. The Release browser also passes original pause/resume and two four-stock
diagnostic loops with three respawns and return to CSS. These use raw-PAD
diagnostics, not physical input; cold runs have shown timing pauses. This validates the
accepted Mario/Final Destination slice and narrower Falco/Battlefield source and
rendering scopes. It does not establish full
`gm_Scene_Vs_OnEnter` or retail scene-manager equivalence, and it is not
tournament acceptance.

The scoped [scene-entry profile](../work/scene-entry-profile.md) records the
measured preparation and first-use phases. It is application/driver-cache
evidence from an isolated run, not a clean cold-cache, uninterrupted full-match
or retail-reference result. The later MarioReady operation-55 representation
defect is fixed, and fresh native whole-match routes pass on both admitted
stages.

## Optimization policy

- Keep source action logic, collision, damage, RNG consumption, object/process
  ordering, and numerical semantics. No fast-math or relaxed precision as a
  performance fix. Changes to execution order require reference comparison.
- Record input at an explicit original sampling boundary. Presentation callbacks
  are not simulation ticks. Never manufacture missed historical controller
  samples from the most recent sample and call the result timing-equivalent.
- Cache or pre-create renderer resources without advancing a hidden match,
  consuming gameplay RNG, changing global source state, or omitting effects.
  Resource lifetime and invalidation must survive scene exit and repeat matches.
- Preserve all required audio engine updates and independently validate emitted
  PCM. Replacement algorithms or coefficients need their own equivalence evidence.
- Treat an observed mismatch as an open defect. Do not widen tolerances, remove
  compared fields, lower resolution, or suppress diagnostics merely to turn a
  correctness or performance check green. Optional enhanced resolution is a
  presentation option; it cannot substitute for baseline rendering comparisons.
- A necessary temporary compromise must be named with its scope, evidence and
  removal gate. New accuracy tradeoffs require explicit owner agreement before
  implementation. This is not required for behavior-preserving optimization.

## Known gaps and removal gates

| Gap | Current evidence / consequence | Required closure |
| --- | --- | --- |
| Native menu acceptance | Original CSS/SSS is the canonical player flow; source and browser diagnostic repeat loops pass, including original pause/resume, and raw PAD selects and renders Falco/Mario on Battlefield. A pinned read-only retail trace matches the port's CSS → SSS → CSS → SSS → match order, uninterrupted menu-audio ownership, exact raw and normalized match data, and RNG at all nine boundaries | Broaden the pinned comparison beyond Mario/FD and complete ordinary browser/physical input acceptance |
| Scene-entry preparation | CSS/SSS rebuild mutable source scenes in two callbacks while retaining one original audio owner and continuous HPS clock; match ownership is constructed in explicit phases after audio acknowledgement; simulation remains stopped while unchanged frames discover uploads and drain pipelines through two quiet callbacks | Repeat on clean application/driver caches, verify cold entry plus death/respawn without a manual hitch resume, and include full-match tails and audio underruns; CPU queue drainage is not a browser GPU completion fence |
| Catch-up sampling | `gameplay_browser.cpp` and `gameplay_menu_browser.cpp` poll once per browser callback and may reuse that sample for multiple source steps | Specify and verify per-tick input acquisition and phase/order under actual browser constraints; never claim missing historical samples were recovered |
| Catch-up output | Source audio has an independent bounded catch-up clock; CSS/SSS retain its fractional phase, while a true owner change pauses and re-primes the worklet | Explicit overload behavior plus measured input/audio timing; catch-up runs cannot count as uninterrupted performance acceptance |
| Fixed 60.0 clock | Host clock and sample production currently use exactly 60 | Establish selected retail VI mode/cadence and input scheduling from reference evidence, including long-run drift; do not assume the nominal label proves exact cadence |
| DSP direct-mode phase | The port retains phase; original DSP accumulator-dependent block writeback is not yet verified. The [audio replacement](AUDIO_REPLACEMENT_EVIDENCE.md) preserves existing port behavior | Compare a focused direct-to-filtered transition against the original DSP before claiming equivalence or a confirmed deviation |
| DSP coefficients | Generated replacement DROM is approximately equivalent, not identical to hardware | Validate the audio path against an independent reference and obtain exact coefficient/input support if needed; identical approximations on both sides are not an independent oracle |
| Native numerics | Bounded original jump, jab and stock traces exist; no complete match equivalence | Broaden bit-preserving semantic traces under identical initial conditions and input sequences; locate first divergence |
| Match initialization | The supported native handoff carries full `StartMeleeData` through `fn_8016DCC0`, including ports, costumes/tints, four stocks and RNG; broader modes and configurations remain outside the validated slice | Compare exact supported original start configuration and extend the source initializer before broadening modes or claiming general start-state equivalence |
| Original HUD | Original Ready/Go, damage/stocks and markers execute and render; source damage/intro/repeat-lifetime checks and two browser diagnostic loops pass, and Falco/Mario stock identities render on Battlefield | Compare authored interface rendering, timing and audio against the original; complete ordinary-input and reference checks beyond Mario/FD |
| Match ending | Original supported VS ending callback, GAME!/Game Set interface/audio request, source process mask, pause/resume, No Contest and exit request are integrated; two Node cycles pass the source ending checks | Verify rendered/audio/reference behavior and broader source modes. Results routing remains skipped after the original exit; this does not claim full `gm_Scene_Vs_OnEnter` or retail scene-manager equivalence |
| Rendering | Native HSD/GX path renders the current match and browser original pause artwork/camera; no complete pixel equivalence | Reference camera, transforms, materials, blending, depth, effects, viewport and output timing on a declared baseline |
| Device/latency acceptance | Keyboard Start and SSS cancellation pass; complete ordinary native-menu play, physical controllers and end-to-end latency remain unaccepted; cold timing pauses are observed | Real-device routing, disconnect/reconnect, simultaneous players, analog thresholds and independently measured latency; cold/warm long-match tests |

Moving input/simulation to a dedicated worker is a candidate architecture, not
an approved accuracy fix by itself. Browser gamepad sampling availability, thread
ordering, rendering/audio communication, and new latency must be demonstrated.
During an unsampled main-thread stall, information may simply be unavailable.
Define an honest overload policy and prevent stalls on the supported competition
configuration; do not promise exact uninterrupted behavior from missing input.

## Evidence required for an accuracy claim

1. Pin GALE01 revision 2, owned executable/assets hashes, source revision, port
   commit, build flags and schema. Pin the separate retail-reference executable,
   emulator/configuration and capture boundary. Use hardware or an independent
   reference when emulator behavior or shared replacement code is in question.
2. Repeat each original capture and require repeatability before treating it as
   a reference. Match players, costumes, stage, rules, starting state, controller
   mapping and RNG. A diagnostic placement is not an original match start.
3. Compare semantic state at the same source tick/phase: action/subaction,
   position/velocity, collision/ground state, damage, shield, stocks, animation
   and commands, items, RNG and outcome as applicable. Preserve float bits;
   explain any field-specific tolerance before adoption. Exclude host addresses
   and padding, not meaningful state. Report the first divergent tick/field.
4. Compare menu selection and transition sequences, including cancellation,
   held-button behavior and repeated cycles. Match-start state must reflect the
   real menu selection rather than being silently replaced by fixture defaults.
5. Validate rendering, audio, physical input and latency independently of state
   traces. Software timestamps alone do not measure controller-to-photon delay.
6. Publish performance evidence for a named machine, browser/version, OS, input
   device, resolution, release build and power/display settings. Separate cold
   and warm application/driver caches. Include tails, stalls, audio underruns,
   drift and memory over repeated full matches. Coverage instrumentation runs
   do not establish shipping-build performance.
7. Version the eventual competition profile (revision, rules, device mappings,
   allowed browser/configuration and immutable build identity). Keep experimental
   options separate, expose failures, and seek community/organizer evaluation
   only when evidence supports it. A partial Mario/FD slice is not tournament-ready.

For compiled gameplay replay, recorded controller-queue snapshots may be supplied
as explicit platform input fixtures. Label such evidence conditional on that
recorded schedule and retain exact game-state and source-boundary comparisons.
It does not establish original CPU interrupt scheduling, live controller timing
or performance acceptance. See [recorded queue replay](RECORDED_QUEUE_REPLAY.md).

Acceptance is scenario- and field-scoped. Finite testing cannot prove every
possible game state; report the precise verified scope and known failures rather
than an unqualified 100% claim. The engineering target remains full original
behavior, including quirks relied upon in competitive play.

## Existing issue boundaries

- [#1: deterministic comparison](https://github.com/ericvanlare/melee-web/issues/1)
  is the basis for expanding state evidence. Reuse the existing reference setup
  and comparison work; do not build a parallel oracle or claim the whole issue is
  complete from a menu smoke test.
- [#2: publication readiness](https://github.com/ericvanlare/melee-web/issues/2)
  tracks licensing, provenance and safeguards. Keep the repository private;
  this deliverable does not resolve or authorize publication.
- [#3: coverage reporting](https://github.com/ericvanlare/melee-web/issues/3)
  will separate compiled, reference-tested and accepted scope. Save reproducible
  evidence now, but defer dashboard implementation during native-menu work.

Do not take on those entire issues as prerequisites to displaying the menus.
Apply the relevant constraints at each changed boundary and keep remaining
acceptance work visible.
