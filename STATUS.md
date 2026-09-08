# Current status

The browser runs two original Mario instances on Final Destination through
compiled WebAssembly. The complete acceptance milestone below is still open:
source stage and item rendering, stock/respawn/outcome flow, and the optional
renderer cache are integrated, while cold first-use stalls, audible and physical
controller verification, and full original-game equivalence remain open. No
emulator is shipped; Dolphin is used only as a separate original-game reference.

The current stopping milestone is the **complete local Mario-versus-Mario stock
match on Final Destination**, including original gameplay behavior, two local
controllers, camera, sound, match outcome/restart, and measured stable 60 fps on
the reference machine. The smaller gates below are checkpoints, not completion.

## Active integration

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
- Workers use GPT-5.6 Luna xhigh; the lead handles integration and difficult blockers.
- The integrated runtime is checkpointed on the private remote. A clean Ubuntu
  GitHub Actions run builds the browser runtime and passes the source/ABI checks.
- A 102-frame retail comparison of a full jump/landing matches source motion IDs,
  non-idle animation frames, and vertical-velocity bits on every frame. The
  original match counter removes duplicate scheduler samples. Retail uses the
  unlocked Stadium side platform; the port uses FD, so this is a scoped vertical
  movement comparison, not full match equivalence. A second retail capture repeats
  all 102 frames, and the updated intrinsic build still matches the comparison.
- Local suite: 224 tests passed without skips after refreshing the affected
  source trace binaries. The strengthened projectile regression passes actual P2 impact,
  expiry and teardown in two worlds. All six ground/air side/up/down specials
  also pass source action entry, finite state and two-world teardown checks.
  Browser rendering remains a separate gate for those additional specials.

## Latest runtime evidence

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
passed. The current suite is the 224-test result above. Full-source linking has
no remaining function-signature mismatch warnings.

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

Complete rendered combat and cold first-use preparation, then verify audible
output and two physical controllers. The latest saved-cache full-stage cycle at
1280×960 has a 30.50 ms worst interval, zero intervals above 33.3 ms and zero
audio underruns across 14,452 ticks; the separate saved-cache stock run has the
same zero-over-budget result across 1,985 ticks. These warm runs do not close the
full playable-match milestone, and full original-game equivalence remains open.
See [the acceptance roadmap](docs/ROADMAP.md).
