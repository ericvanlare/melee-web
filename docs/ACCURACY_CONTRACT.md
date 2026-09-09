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

The temporary HTML selection UI is disposable. Remove it from the player flow
when the native chain passes; retain only useful developer diagnostics. The
complete loop needs normal input testing, original selection/configuration
handoff, KO/respawn/outcome, and a second match after returning. A link probe or
automatically scripted match is only partial evidence.

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
| HTML CSS/SSS | Original CSS/SSS now render in a separate preview; source and rendered diagnostic repeat loops pass, while ordinary-input acceptance remains open | Execute and compare original menus and transitions; remove HTML player selection |
| Catch-up sampling | `gameplay_browser.cpp` polls once per browser callback and reuses that sample for multiple source steps | Specify and verify per-tick input acquisition and phase/order under actual browser constraints; never claim missing historical samples were recovered |
| Catch-up output | Source audio processes each tick, but late output is gated and re-primed | Explicit overload behavior plus measured input/audio timing; catch-up runs cannot count as uninterrupted performance acceptance |
| Fixed 60.0 clock | Host clock and sample production currently use exactly 60 | Establish selected retail VI mode/cadence and input scheduling from reference evidence, including long-run drift; do not assume the nominal label proves exact cadence |
| DSP coefficients | Generated replacement DROM is approximately equivalent, not identical to hardware | Validate the audio path against an independent reference and obtain exact coefficient/input support if needed; identical approximations on both sides are not an independent oracle |
| Native numerics | Bounded original jump, jab and stock traces exist; no complete match equivalence | Broaden bit-preserving semantic traces under identical initial conditions and input sequences; locate first divergence |
| Match initialization | Native menu handoff carries ports, costumes/tints, four stocks and RNG; remaining original match-init fields and startup behavior are not yet generally ported | Compare exact supported original start configuration and extend the source initializer before broadening modes or claiming start-state equivalence |
| Original HUD | Original Ready/Go, damage/stocks and markers now execute and render; source damage/intro/repeat-lifetime checks and two browser diagnostic loops pass | Compare authored interface rendering, timing and audio against the original; broaden beyond Mario/FD |
| Match ending | Original ordinary-VS ending callback, GAME!/Game Set interface/audio request, source process mask and exit request are integrated; two source loops verify 114 frozen ticks | Verify rendered/audio/reference behavior. The active match still uses a scoped end-only OnFrame adapter; full source bookkeeping and pause remain open. Only Results routing is skipped after the original exit |
| Rendering | Native HSD/GX path renders current match; no complete pixel equivalence | Reference camera, transforms, materials, blending, depth, effects, viewport and output timing on a declared baseline |
| Device/latency acceptance | Keyboard play and short warm timing runs; physical controllers and end-to-end latency remain unaccepted | Real-device routing, disconnect/reconnect, simultaneous players, analog thresholds and independently measured latency; cold/warm long-match tests |

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
