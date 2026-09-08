# Current status

The browser runs two original Mario instances on Final Destination through
compiled WebAssembly. The complete acceptance milestone below is still open:
the reported projectile and post-respawn crashes have fixes, while broader
combat, edge collision and long stage-animation validation remain open. No emulator
is shipped; Dolphin is used only as a separate original-game reference.

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
  Full rendered-cycle fidelity still needs browser/original comparison.
- Original SEM/synth/AX source produces SFX PCM with original SDK reverb/delay.
  The integrated HPS stream runs the original three-slot scheduler through the
  FD intro and loop, with identical complete PCM hashes across two 100-second
  runs. Browser AudioWorklet transport is connected; audible browser verification
  is pending the current scene-loading fixes. Replacement DSP coefficients are
  identified explicitly; full hardware DSP equivalence is not established.
- Source action readiness now covers 160 Mario submotions. All47 common effect entries and both Mario effect entries are hydrated.
  Original jab, jump/landing, shield and damage traces pass across two worlds. Actual damage,
  stocks and shield values are exposed directly for observation.
- Original run-off, stock loss, respawn platform and grounded return pass in two
  worlds after hydrating common root8. Certain run-off positions expose an
  edge collision oscillation. An independent four-stock run with jumps passes
  three respawns and the original elimination outcome in two worlds; it does
  not resolve the plain-running collision defect.
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
- Workers use GPT-5.6 Luna xhigh; the lead handles integration and difficult blockers.
- The integrated runtime is checkpointed on the private remote. A clean Ubuntu
  GitHub Actions run builds the browser runtime and passes the source/ABI checks.
- A 102-frame retail comparison of a full jump/landing matches source motion IDs,
  non-idle animation frames, and vertical-velocity bits on every frame. The
  original match counter removes duplicate scheduler samples. Retail uses the
  unlocked Stadium side platform; the port uses FD, so this is a scoped vertical
  movement comparison, not full match equivalence. A second retail capture repeats
  all 102 frames, and the updated intrinsic build still matches the comparison.
- Local suite: 221 tests passed without skips after refreshing the affected
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

Checkpoint validation: **192 tests passed with no skips**, the browser/gameplay
and full fighter targets built, and both gameplay and owned-asset fighter runners
passed. Full-source linking has no remaining function-signature mismatch warnings.

## Runtime foundation

- Pinned Melee, Aurora and Emscripten; checked generated gameplay patches preserve
  the original int-bool ABI and packed flag aliases. Pristine dependencies stay intact.
- Actual full common initialization consumes owned supported PlCo roots, native
  material owners and stage lights. Missing roots retain explicit readiness.
- Typed Mario ftData, all action rows, independent source action-loader bindings,
  common attributes, hurtboxes, visibility tables, costume and metal graphs.
  Wait/Fall scripts use decoded native operands; unsupported scripts fail.
- Original item registration consumes the real ItCo.usd registry; Mario effect
  banks and both native effect models/animation graphs load from EfMrData.dat.
  Item spawning and full effect rendering remain incomplete.
- Final Destination uses real collision geometry, original priority-4 collision
  updates, light overrides, marker transforms, camera/blast bounds and ground
  parameters. Full render-object stage initialization remains incomplete.
- Source camera subjects, player settings, PdPm bonus thresholds, original font
  atlas and scoped RNG/PAD state have explicit owners and restart checks.
- Native descriptor owners retain borrowed data through original destruction.
  Metal aliases are freed through the original ID allocator before world reset.
- A bounded owned I/O queue has copy/completion/cancellation tests. It is not yet
  wired into the original archive loader. Unavailable device services abort with
  named errors instead of reporting success.

## Browser foundation

The inspector renders original HSD transforms, skinning, materials, texture
layers and polygons through Aurora GX/WebGPU. Mario/Fox Wait and walking clips,
Final Destination's opaque geometry, and the bucket/fan/bullseye fixtures were
visually checked in earlier milestones. Those views use inspection cameras and
lights; translucent stage passes and unapplied services are reported explicitly.
The current build also passes a real-browser synthetic rendering smoke check.

Browser keyboard input reaches Aurora PAD with distinct raw/clamped diagnostics
and focus neutralization. Physical controllers remain untested. The runtime
fighter now uses this browser input/presentation path; visual correctness remains open.

## Next

Finish the long FD stage cycle and broader raw-input combat regressions, resolve
the phase-dependent edge collision against an FD original-game trace, then run
a sustained release-browser match and measure active-frame timing. Verify audible
output and two physical controllers before closing the playable-match milestone.
See [the acceptance roadmap](docs/ROADMAP.md).
