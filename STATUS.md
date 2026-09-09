# Current status

The browser runs original fighters and stages through compiled WebAssembly. The
accepted first slice is two Marios on Final Destination; narrower raw-PAD runs
also selected and rendered Falco versus Mario on Battlefield and Fox versus
Mario on Yoshi's Story. The complete
acceptance milestone below is still open:
source stage and item rendering, stock/respawn/outcome flow, and the optional
renderer cache are integrated, while clean cold-cache/full-match first-use
stalls, audible and physical controller verification, and full original-game
equivalence remain open. No
emulator is shipped; Dolphin is used only as a separate original-game reference.

The typed content path now carries Falco, Fox, Battlefield and Yoshi's Story
source IDs, runtime manifests and focused development traces. Source stock icon IDs are
selected from the typed character/fighter identity rows. In a fresh Release
browser route, raw PAD input selected P1 Falco and P2 Mario in the original CSS,
selected Battlefield in the original SSS, and entered a four-stock match. The
rendered frame showed Falco, Mario, Battlefield geometry and the correct four
Falco and four Mario stock icons. A complete ordinary-input loop, uninterrupted
audio and retail-reference comparison have not passed, so the accepted
first-deliverable claim remains Mario on Final Destination. The integrated
source match trace completes Fox on Final Destination and Battlefield,
including all four costumes, laser, Reflector, Illusion and Fire Fox. A combined
Fox/Falco run proves shared effect-bank ownership. Yoshi's Story completes
repeated source lifetimes with four map objects, Randall state, moving collision
and real Shy Guy creation after the original 120-frame scheduler. A Release
browser route selected Fox in the original CSS, selected the upper Yoshi's Story
tile in the original SSS, and rendered the source Ready countdown with Fox stock
icons and Yoshi's Story geometry. The lower adjacent tile is Yoshi's Island and
remains unavailable. Uninterrupted audio and retail comparison for these
additions remain open.

The active first deliverable is **original in-game CSS → original in-game SSS
→ a playable four-stock Mario-versus-Mario match on Final Destination → original
CSS**, without a results screen. Real menu assets, original scene/input behavior,
transitions and repeat-match lifetime are required. The former HTML selector is
removed from the player flow. Full accuracy, physical controllers, sound and stable performance
remain required acceptance work under [the accuracy contract](docs/ACCURACY_CONTRACT.md).

## Active integration

The canonical `runtime.html` player uses original CSS/SSS assets, callbacks and
transitions. `native-menu.html` redirects to it; the asset inspector remains at
`viewer.html`. The runtime availability gate now includes Mario, Fox and Falco,
all of their source costumes, and Final Destination, Battlefield and Yoshi's Story under the
four-stock, no-items, no-timer rules. Menu selection carries the complete typed
`StartMeleeData`, including source RNG and costume/tint settings, into the match.
The original menu preloader is not supplied by this resident-asset integration.

The match now calls original `fn_8016DCC0` for the supported start configuration
and the original VS `OnFrame`, replacing the prior end-only adapter. Original
stage markers are initialized before fighter creation. Original Entry, Ready/Go,
damage/stock/player-marker/magnifier interface, pause screen and pause camera,
No Contest, and the GAME!/Game Set ending run through scoped source owners.
The host returns to CSS only after the original exit request, skipping Results.
This does not establish full `gm_Scene_Vs_OnEnter` or retail scene-manager
equivalence across all modes.

The current source lifecycle trace passes two complete original CSS → SSS →
four-stock match → CSS cycles. It checks Ready/Go at **124 ticks**, blocked early
movement, original entry accessory lifetime, an in-range fireball whose damage
matches the HUD, three respawns and the winner, and **114 frozen ending ticks**.
It also passes source pause with frozen fighter state, wrong-player Start
rejection, same-player resume, original L+R+A+Start No Contest, SSS B cancellation
and re-entry, and unload at 0, 60 and 100 intro ticks. The source trace is
`work/native-active-telemetry-run.log`; these are shared-source lifecycle checks,
not original-game equivalence evidence.

The expanded native menu trace selects Mario/Falco through raw PAD, enters SSS,
and completes a four-stock match back to CSS twice on both Final Destination and
Battlefield. The content trace repeats all three stages with every Falco and Fox
costume, exercises their laser, Reflector, side-special and up-special action
families, pause and No Contest, checks Yoshi's Story's Randall/Shy Guy lifecycle,
and verifies teardown before the next cycle.
These native traces establish the checked source lifecycle; they do not replace
browser input, audio or retail-reference evidence.

Scene transitions now use an explicit preparation boundary for CSS, SSS and
match entry. The browser shows the phase and waits for the audio worklet's
disabled-state acknowledgement before owner/session construction; no source
simulation ticks or source draws run during the wait or construction phase.
Preparation time is reported separately, and the live clock arms on a later
callback. The scoped [scene-entry profile](work/scene-entry-profile.md) measured
106.555 ms for initial resource preparation, 83.140 ms for isolated SSS owner
construction and 160.100 ms for isolated match construction. Its 7,604 active
callbacks had a 24.200 ms worst callback, no callback above 33.3 ms and zero
audio underruns. This is scoped application/driver-cache evidence rather than
cold-cache or full-match acceptance. The subsequent MarioReady operation-55
representation defect is fixed, and the fresh native whole-match routes above
pass on both admitted stages.

A correlated Battlefield cold-entry probe measured fresh application
construction at 182.380 ms, the preparation boundary at 200.095 ms and first
draw at 10.025 ms, with 399,360 texture-upload bytes and eight queued/five
created pipelines. A source-world reload in the same page measured
157.685/175.265/3.880 ms with no new uploads or pipelines; a full page reload
measured 160.485/178.040/4.870 ms and repeated the texture upload while retaining
the browser/driver pipeline cache. Diagnostics now distinguish application-state
reload from clearing the persisted renderer cache. Neither clears browser or
GPU-driver caches, so this explains the first-load shape without claiming a
clean machine/browser cold-cache pass.

The current Release browser separately passes original pause/resume and two
four-stock loops, each at **2,125 diagnostic input ticks**, with three respawns
and automatic return to CSS. Source pause was explicitly observed changing
from 0 to 1 and back to 0 before the first stock run. An earlier overlapping
Start/stock diagnostic attempt stayed source-paused and failed its tick bound;
it is not counted as a pass. Stock diagnostics now reject host/source pause,
Ready/ending transitions and queued Start input, preventing that overlap.
These browser runs use labeled raw-PAD diagnostics, not physical input.

Native descriptor support now retains original interleaved NBT geometry and
bump texture flags for the pause screen and common entry accessory. It uses the
original HSD/GX path, with unsupported forms still rejected. The common root16
accessory has a world-owned descriptor that outlives its original fighter/effect
consumers. Eye telemetry distinguishes an authored base image from an animated
table entry; this changes observation only, not the game's texture commands.

The current local regression suite passes **266 tests without skips** (700.484
seconds), recorded in `work/full-suite-2026-09-09.log`. Default and Release browser, native-menu,
content-match, Battlefield, player-context and fighter-runtime targets build.
The focused browser run used the Release build. The integrated change also
received a primary review of ownership, input handoff, construction, callback
timing and teardown paths.

Ordinary keyboard Start and SSS B cancellation work in the browser. Complete
ordinary keyboard play, physical-controller acceptance, original-game
visual/audio comparison and clean cold-cache/full-match timing acceptance remain
open. The inspected Falco/Battlefield match reported an active callback below
10 ms and none above 33.3 ms, with preparation reported separately; automation
disrupted that run and produced audio underruns, so it is not uninterrupted
performance or audio evidence. First-use
graphics can cause explicit timing pauses. The native player retains the strict
pause policy and nominal 60 Hz clock; neither establishes original timing
equivalence. Catch-up input
sampling and replacement DSP coefficients also remain open accuracy gaps under
[the accuracy contract](docs/ACCURACY_CONTRACT.md). The optional IDBFS cache saves
renderer resources after unload; disc assets remain local and unpersisted.

The bullets below include earlier checkpoints; their narrower evidence must not
be mistaken for newer browser or full-loop acceptance.

- Native-menu preparation now passes both complete scene tables: CSS nine and
  SSS twelve model groups with camera/lights/fog, material animation and original
  PObj morphing descriptors. Every referenced CPU morph scalar is compared bit
  for bit against the original big-endian array. Original HSD loading/animation
  and teardown pass across two SDK worlds. Original SIS loading/layout/release
  passes for all 85 CSS text entries, plus scaling, spacing and style-stack checks.
  The original SSS OnEnter, 120 neutral raw-PAD/scheduler/audio ticks and OnExit
  also pass twice with original menu music. This diagnostic does not render or
  confirm a stage; scene preloading remains an explicit unsupported path.
  Original card archive loading and camera/model/animation consumers also pass
  across two worlds with checked scene-heap ownership. The full regression suite
  passed 243 tests; subsequent focused lifetime/card checks and the Release build
  pass. Original CSS entry, 120 neutral raw-PAD/scheduler/audio ticks and exit
  now pass across two worlds as well. Its cursor code accesses declared source
  globals directly, removing a cross-global GameCube layout assumption. Original
  SSM bank startup, cancellation, menu-to-Mario-to-menu switches and actual PCM
  publication pass twice with all seven exact local bank paths. Source loading
  still owns queues, accounting and sample publication; host bridges provide
  checked bytes, native descriptor fields and scoped auxiliary storage. The full
  regression suite passed 245 tests before the new focused bank/CSS checks.
  That checkpoint did not yet render menus or enter a match; the later native
  preview and source-loop checks above extend it. The original scene preloader
  remains open. The HTML selector remains disposable scaffolding.
  Shared typed archive handles and a checked CSS/SSS lifecycle boundary are
  integrated. Texture indices are checked before original table access; unused
  authored values are preserved. The accuracy contract records catch-up
  input/audio, nominal cadence, approximate DSP coefficients and missing
  reference/device evidence as open gaps; no tournament-acceptance claim is made.

- Temporary browser selection scaffolding now exercises character selection →
  stage selection → original stock match → character selection, without a results
  screen. This is **not the original in-game CSS/SSS** and will be removed when
  those scenes, assets and behavior are ported. All 26 source character entries
  are unlocked; only Mario is available, with FD as the only stage. An immutable
  configuration boundary separates unlocks, availability and original source IDs.
  Real-browser checks passed one-stock launch, selection preservation, invalid
  stock rejection, stage back-navigation and repeated automatic outcome returns.
  The original stock diagnostic reached 1,985 ticks, three respawns and P2 winner
  on both cycles. Physical menu/controller acceptance is still pending.

- Render scale now controls the internal framebuffer through Aurora's VI API,
  independently of Retina presentation density. The former 1× window setting
  still incurred approximately 1280×960 internal rendering on this display;
  explicit 1× now reports 640×480 and 2× reports 1280×960. This reduces default
  pixel workload without changing the 60 Hz simulation. Long-frame diagnostics
  also defer formatting their history until the diagnostics panel is open.
  A warm 1× stock cycle measured 17.68 ms worst interval, zero intervals above
  33.3 ms and zero audio underruns. A final exact-2× stock cycle measured
  17.84 ms worst with zero long frames or underruns. These short warm runs do not
  establish a rendering-speed comparison. This does not resolve the user's reported
  175.14 ms worst interval / seven long frames in 3,207 frames, nor establish
  cold-cache performance. All 238 regression tests and the Release build pass.

- A fresh-origin stock run without persisted application pipelines passed at
  32.46 ms worst interval with no audio underruns; Cape, Super Jump Punch and
  Tornado also stayed below 33.3 ms. The browser driver cache was not reset.
  A longer run after fireballs reproduced four gaps (74.58 ms worst) around
  tick 3,740, with zero texture upload/new pipelines and 64 audio-underrun frames.
  The first prior frame spent 49.715 ms in end-frame; later gaps exceeded native
  work. No further long frames appeared through tick 21,188. This does not prove
  that first-use stalls are solved, and does not justify speculative texture
  warmup. An opt-in queue-completion sampler now distinguishes asynchronous
  queue/callback latency from native frame work; it is not GPU execution timing.
  Its isolated stock check passed at 1,985 ticks: 17.81 ms worst interval, zero
  long frames/underruns, and 33 successful queue samples (15.83 ms p95, 16.68 ms
  worst). Enable/disable, unload and restart discard stale samples correctly.
  All 237 regression tests and the Release browser build pass.

- Direct browser ISO/GCM/CISO import validates GALE01 revision 2 and the original
  executable, selects the English audio paths, extracts the font from that
  executable, and generates Dolphin's free replacement DSP coefficients.
  All fifteen inputs from the owned CISO match the existing local bundle byte for
  byte (13,068,490 bytes). No disc or extracted data is uploaded or persisted.
  Synthetic tests cover sparse blocks, malformed filesystem tables and bounded
  reads. Extracted-folder import is now a developer diagnostic. The real browser
  completed the 1,985-tick stock diagnostic (three respawns, P2 winner) after
  direct CISO loading: saved-cache worst interval 18.43 ms, no intervals above
  33.3 ms and no audio underruns. A rejected image disabled launch; reimporting
  the valid image restored launch and the ground/air fireball check dealt 6%.
  This validates import/restart, not cold-rendering acceptance. The complete
  regression suite passes 237 tests with no skips; the Release browser build passes.

- Normal keyboard play exposed an overly strict timing policy: more than eight
  elapsed source ticks forced a manual pause. Gameplay now retains bounded timing
  debt and runs at most eight original 60 Hz ticks per browser callback, catching
  up across callbacks. More than one second of debt still pauses explicitly.
  Browser audio re-primes during catch-up instead of accumulating late PCM; the
  source audio engine still processes every simulation tick. Repeated injected
  200 ms and 800 ms delays recover without manual resume or audio overflow.
  These are recovery checks, not evidence that rendering hitches are fixed.

- One shared asset/world owner now serves the browser and Node regression harness.
- Two original fighters run together. Raw PAD input uses the original HSD queue,
  clamp/scale processing, player activation and fighter processes. A 60-frame
  walking trace moves only the selected fighter and survives world restart.
- The browser imports fifteen local runtime files, starts/stops/restarts the world,
  and runs the original standard/fixed camera initialization and controller.
- All nine initial FD objects and native background graphs now run original
  Ground startup and rendering passes. The stage advances at tick1801 and passes
  3600 scheduled ticks and teardown in two worlds. A longer probe found that
  the first-animation lookup lost its result across longjmp. The corrected
  volatile pointer and typed callback restore the lookup. A terminal single-CON
  compatibility rule removes an original uninitialized visibility output while
  preserving the observed hidden result. Two worlds each pass 36,000 frames,
  every background state, cycle restart, archive immutability and teardown.
  Historical checkpoint: the release browser continued past 16,400 ticks after
  resuming a 150 ms interval pause at tick13,017. The initial 788×592 run had
  no interval above 33.3 ms through tick11,940; that earlier pause prevents it
  from serving as stable-60-fps evidence. The current saved-cache measurement
  below is the applicable timing result.
- Original SEM/synth/AX source produces SFX PCM with original SDK reverb/delay.
  The integrated HPS stream runs the original three-slot scheduler through the
  FD intro and loop, with identical complete PCM hashes across two 100-second
  runs. Browser AudioWorklet transport is connected with a 1536-sample prefill
  (one additional source-frame of latency) and per-frame transport gating.
  Saved-cache runs report zero audio underruns; audible output and
  physical-controller verification remain pending. Replacement DSP coefficients are
  identified explicitly; full hardware DSP equivalence is not established.
- Source action readiness now covers 160 Mario submotions. All47 common effect entries and both Mario effect entries are hydrated.
  Original jab, jump/landing, shield and damage traces pass across two worlds. Actual damage,
  stocks and shield values are exposed directly for observation.
- Original run-off, stock loss, respawn platform and grounded return pass in two
  worlds after hydrating common root8. The isolated edge probe omitted stage
  updates, leaving the collision generation counter unchanged and selecting the
  wrong source floor-check path. With full stage/camera startup, the precise
  run-off phase captured from the original falls continuously without regrabbing
  the floor. Regular post-respawn running also passes. The full-stage stock probe
  passes three respawns and the original elimination outcome in two worlds.
- Normal browser launch now reads the authored Final Destination player markers
  through the original Ground API: `(-60, 10, 0)` and `(60, 10, 0)`. The
  close-range `+/-20` positions remain explicit diagnostic fixtures.
- B-button failures reached missing Mario article publication and original item
  common-data/allocator startup. Ground/air B simulation now passes in two
  worlds. A subsequent browser-only particle crash exposed raw GameCube FIFO
  stores and a mismatched item material callback. Both are repaired: the rendered
  ground/air B sequence reaches impact. Correcting the shared reciprocal-square-root
  intrinsic restores fireball bounce velocity and P2 damage (6%); the full browser
  subsequently exposed a burn-effect table copied as two instead of five bones.
  All five are now owned; the browser ground/air B sequence completes twice with
  6% P2 damage, and restart/unload succeed.
- Corrected the source secondary-costume-color setting: its recovered function
  name says ControllerIndex, but gm_16AE supplies sub_color. Controller routing
  remains owned by the raw PAD queue.
- Original raw-input jab, shield, grab and all four throw directions pass in two
  worlds. Cross-fighter animation streams, row identities and commands remain
  owned after the source fighter store is destroyed. Importing the shared
  FTKIND_NONE part-remap row fixes the grab/throw crash without changing roster size.
- Sixteen additional raw-input move cases cover tilts, smashes, dash attack,
  all aerials, air dodge, rolls and spot dodge in two complete passes with a fresh
  full-stage world per case. Air dodge exposed missing original crowd-manager
  startup; match creation now owns that manager through match teardown.
- The release browser stock diagnostic uses raw movement/jump input and completes
  four stock losses, three respawns and the normal P2 winner message at tick1985.
  A cold run pauses during the first death/respawn rendering. Restarting with
  the same renderer resources completes the identical1985 ticks with no pauses,
  no intervals above33.3 ms and a worst interval of21.26 ms at640×480.
  The saved renderer-cache run completes the same 1,985 ticks, three respawns
  and four losses with a 22.23 ms worst interval and no interval above 33.3 ms.
  First-use rendering preparation remains open; warm timing is not cold acceptance.
- The renderer-only `/melee-render-cache` is optional IDBFS state. It is mounted
  separately from game files, saved after unload when the pipeline queue is idle,
  and loaded 81 cached pipelines before assets in the latest browser cycle. No
  game assets are persisted.
- Current saved-cache timing was captured in Chrome 152's IAB at DPR1 on the
  reference machine at 1280×960. The selectable 2× SDL render scale remains
  available for scale-specific checks.
- Earlier parallel checkpoints used GPT-5.6 Luna xhigh; the lead retained
  integration and difficult blockers.
- The integrated runtime is checkpointed on the private remote. A clean Ubuntu
  GitHub Actions run builds the browser runtime and passes the source/ABI checks.
- A 102-frame retail comparison of a full jump/landing matches source motion IDs,
  non-idle animation frames, and vertical-velocity bits on every frame. The
  original match counter removes duplicate scheduler samples. Retail uses the
  unlocked Stadium side platform; the port uses FD, so this is a scoped vertical
  movement comparison, not full match equivalence. A second retail capture repeats
  all 102 frames, and the updated intrinsic build still matches the comparison.
- A bounded four-stock Final Destination trace from the original GALE01 rev2
  collector uses four stocks, items NONE, Mario yellow at -60 and Mario red at
  +60. Across 541 samples (original game frames 12..552; port after 120 neutral
  settle ticks), the reusable comparison reports exact position, velocity,
  motion, ground/air, stock and damage bits for both fighters, including one
  loss and one grounded respawn. Animation idle phase and RNG are excluded from
  this bounded comparison; it is not a full-match equivalence claim. An extended
  754-sample recipe also matches all compared fields through a P1 jab and P2
  3% damage, including the exact action-entry and impact ticks.
- Local suite: 234 tests passed without skips after refreshing the affected
  source trace binaries. The strengthened projectile regression passes actual P2 impact,
  expiry and teardown in two worlds. All six ground/air side/up/down specials
  also pass source action entry, finite state and two-world teardown checks.
  The browser now also completes ground/air Cape, Super Jump Punch and Tornado
  action checks. First-use Cape and Tornado presentation intervals reach about
  55–58 ms; a saved-cache Cape run has no interval above 33.3 ms or audio
  underrun. Isolated warm Jump Punch, Tornado and fireball restarts each stay below
  19 ms worst interval and add no audio underruns; fireballs still deal 6% to P2.
  Visual effect fidelity and other-special opponent impacts remain separate checks.

## Earlier runtime foundation evidence

The real-data gate passes **four fighter lifecycles across two complete worlds**:

- Original Player_80031AD0 calls Fighter_Create with actual Mario assets.
- Original Fall/physics/collision settles into grounded Wait after 10 ticks.
- Every registered fighter process then runs for 120 neutral Wait ticks.
- Each eye changes image and palette seven times through original commands;
  both native texture tables and eight inserted metal DObjs remain valid.
- Original GObj disposal calls Fighter_Unload, clears player entities and camera
  subjects, and restores scoped player/PAD/RNG/counter state.
- Both SDK worlds shut down and restart. All seven retained DAT archives remain
  byte-identical; original reflection code mutates owned display-list copies.

These are source execution and ownership checks under Node/Wasm. They do not
establish original-game simulation equivalence, browser gameplay, sound output,
or full-game performance. See [reproduction and scope](docs/FIGHTER_RUNTIME.md).

Historical checkpoint: **192 tests passed with no skips**, the browser/gameplay
and full fighter targets built, and both gameplay and owned-asset fighter runners
passed. That suite count belongs to the earlier checkpoint. Current verification is
recorded in Active integration above.

## Runtime foundation

- Pinned Melee, Aurora and Emscripten; checked generated gameplay patches preserve
  the original int-bool ABI and packed flag aliases. Pristine dependencies stay intact.
- Actual full common initialization consumes owned supported PlCo roots, native
  material owners and stage lights. Missing roots retain explicit readiness.
- Typed Mario ftData, all action rows, independent source action-loader bindings,
  common attributes, hurtboxes, visibility tables, costume and metal graphs.
  Wait/Fall scripts use decoded native operands; unsupported scripts fail.
- Original item registration consumes the real ItCo.usd registry; Mario effect
  banks, native effect models/animation graphs, item materials and particle
  rendering load from the authored data. Ground/air fireball impact, expiry and
  restart are covered by browser and source checks.
- Final Destination uses real collision geometry, original priority-4 collision
  updates, light overrides, marker transforms, camera/blast bounds, ground
  parameters and full-stage render objects. The browser cycle includes all stage
  transitions and the normal authored player markers.
- Source camera subjects, player settings, PdPm bonus thresholds, original font
  atlas and scoped RNG/PAD state have explicit owners and restart checks.
- Native descriptor owners retain borrowed data through original destruction.
  Metal aliases are freed through the original ID allocator before world reset.
- A bounded owned I/O queue has copy/completion/cancellation tests. It is not yet
  wired into the original archive loader. Unavailable device services abort with
  named errors instead of reporting success.

## Browser foundation

The inspector renders original HSD transforms, skinning, materials, texture
layers and polygons through Aurora GX/WebGPU. The runtime now also renders the
authored Final Destination stage, Mario effects and ground/air fireballs through
the integrated stage and item services. Mario/Fox Wait and walking clips and the
bucket/fan/bullseye fixtures were visually checked in earlier milestones. The
static inspector still uses its own inspection cameras and lights.

Browser keyboard input reaches Aurora PAD with distinct raw/clamped diagnostics
and focus neutralization. Physical controllers remain untested. The runtime
fighter now uses this browser input/presentation path; visual correctness remains open.

## Next

Complete ordinary input acceptance of the native menu/match loop, then compare
its actual menu-derived start, source frames and ending against the original.
Measure cold/warm full-match performance and input/audio latency on a named
reference configuration. Existing short warm timing runs do not close those
gates. Broaden the roster only after the reusable lifecycle and accuracy
boundaries are verified. See [the acceptance roadmap](docs/ROADMAP.md).
