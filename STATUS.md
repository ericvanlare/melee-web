# Current status

Melee is not yet playable in the browser. The browser inspector renders original
models and animations; a separate Wasm runtime now completes original Mario
construction, neutral simulation, teardown and restart. No emulator is shipped.
The first playable goal remains Mario versus Mario on Final Destination at 60 fps.

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
fighter is not yet connected to this browser input/presentation path.

## Next

Connect the validated fighter world to browser rendering and original controller
processing while capturing original-game movement traces. Expand the shared
command/service boundaries needed for movement, jumping and landing. See the
[bounded parallel plan](docs/NEXT_PHASE.md) and [acceptance roadmap](docs/ROADMAP.md).
