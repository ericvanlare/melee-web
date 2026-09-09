# Adding a source stage

Stages enter the port through the same source-owned boundary as Final
Destination and Battlefield. A stage is supported only when its original
callback, map descriptors, numeric parameters, collision, light identities,
effects, and match handoff have all been checked. A DAT that parses or a
source callback that links is evidence for implementation work; neither makes
the stage playable.

## Source facts to record first

Use the pinned Melee DOL and the stage archive from the local CISO. Record the
following facts in the stage change before adding a content row:

* the CSS/SSS `StKind`, original `GrKind`, archive filename, and primary BGM
  ID/audio asset;
* the `grGroundParam` row for the selected `StKind`, including `y`, BGM words
  `x4`, `x8`, `xC`, `x10`, and selection flags `x14`, `x16`, `x18`;
* every `map_head` entry and its source joint, animation, light, fog,
  collision, and flag services;
* the map IDs created by the stage's normal `on_init` callback and the IDs
  created later by its source background scheduler;
* the source `yakumono_param` exchange and every callback that reads the
  stage's `stage_info` union;
* camera/blast/player/platform marker bindings and the collision scale path.

Keep all source offsets and IDs as source identities. Viewer entry numbers,
archive table positions, and CSS/SSS grid positions are not substitutes for
`StKind` or `GrKind`.

## Runtime owner changes

Add a row to the shared `gameplay_content.h` registry with the source stage
kind, ground kind, archive, primary BGM asset, and primary BGM ID. Do not add a
second stage registry in a stage decoder or world owner.

Add one `MeleeWebStageProfile` in `gameplay_stage_profile.c`. The profile must
provide the source `StageData`, the map IDs required immediately after normal
`on_init`, its `yakumono_param` exchange wrapper, the complete map entry count,
and the source animation consumer count for each entry. The map decoder
hydrates every descriptor entry, while `on_init` may consume only a subset.
For example, a stage with seven entries and one animation slot per entry uses
seven explicit counts of one; do not infer a count from an archive relocation
boundary.

Construct `DatNativeStage` with the selected `StKind`. Its one-argument form
exists only for the Final Destination inspection fixture. The world must pass
the selected `GrKind` to `GameplayWorldSelection`, publish that stage's
ground parameters, lights, collision, native map, and effect bank, then enter
the profile callback scope with `melee_web_stage_begin_kind`.

The numeric owner must load marker joints below the same synthetic uniform
`GroundParam::y` joint used by `Ground_GetStageGObj`. The source collision
loader scales vertices and bounds through `Ground_801C0498`; source light
creation and light animation apply the same scale. A positive scale check by
itself is insufficient: a new stage must demonstrate that marker, collision,
light, and rendered model coordinates follow the original owner paths.

The source stage callback is authoritative for object creation and background
transitions. Require the normal live map IDs only. Do not require deferred
background IDs before their source transition occurs, and do not create
placeholder objects to satisfy a descriptor table. Teardown must remove every
source object, restore the exchanged yaku pointer, release map/effect owners,
and then restore the saved `StageInfo`.

## Validation and dependencies

Add a focused real-data trace under `tests/` that repeats the complete stage
owner lifecycle at least twice. It should check, in order:

1. the archive and `grGroundParam` decode against bounded source tables;
2. profile entry coverage and source animation consumer counts;
3. finite marker positions and the expected source scale;
4. collision line/bounds queries and source light override identity lookups;
5. normal `on_init` map IDs and any scheduler transitions reached by the test;
6. source match entry/exit, immutable archive bytes, and zero live objects
   after teardown.

The trace depends on the prepared source runtime, the local stage DAT,
`PlCo.dat`, the selected fighter and effect DATs, `PdPm.dat`, item data, font
data, and the source effect bank. Keep the trace target and source table entry
with the lead's central build integration; stage work must not edit
`CMakeLists`, source-generation scripts, or the shared gameplay patch.

The Battlefield trace is the current reference for this bounded lifecycle
check: it runs two source-backed owner lifetimes, each for 7200 ticks, and
observes the `Waiting`, `Transitioning`, and `Done` background states, the
deferred map IDs, scaled spawn/platform bounds, collision queries, and teardown
ownership. This evidence covers the Battlefield source boundary exercised by
the trace; it does not claim full stage coverage or a full-reference-equivalence
result for every stage service.

Yoshi's Story adds several reusable cases. Its `map_head` has two joint-reference
rows, so the marker owner must select and validate the first marker tree without
requiring the entire table count to be one. Marker joints use
`JOBJ_CLASSICAL_SCALE`. The stage archive's two references to
`GrdStoryHeiho_TopN_shapeanim_joint` have no public provider; the original DAT
loader validates the external chain and resolves those slots to null. Preserve
that behavior rather than guessing a `TyHeiho.dat` dependency. Its map models
also require native packed RGB direct colors and an animation PATH reference to
the exact hydrated spline-joint descriptor.

Stage-owned Articles use the archive's null-terminated `itemdata` table. Publish
those decoded Articles through `stage_info.itemdata` and `it_804A0F60` before
the source `on_init`, keep them alive through item teardown, then restore both
globals. The Yoshi's Story content trace checks that this produces actual
`It_Kind_Heiho` objects after the source 120-frame timer. Its exact archive
contract is in [YOSHIS_STORY_PORT_NOTES.md](YOSHIS_STORY_PORT_NOTES.md).

Run the focused native/data probes and ask the lead to run the browser and
central CMake/Ninja targets. Browser SSS selection must use raw PAD input and
observe the resulting source selection payload. A direct write to selection
globals, a menu tile index, or a parsed archive row is not a lifecycle check.

## Unsupported behavior

Until all dependencies and validations above pass, leave the stage out of the
shared content registry and match gate. The runtime should reject an unknown
profile, a missing archive/service, a mismatched `StKind`/`GrKind` pair, an
incomplete map table, unsupported animation or light payloads, invalid scale,
or missing source callback wrapper with a bounded error. Asset parsing,
compilation, a linked `StageData`, and a visual-only map preview must not mark
the stage playable.

When a stage has deferred objects, alternate music, event-only rows, unlock
conditions, or dynamic collision not covered by the current match boundary,
document that behavior as unsupported and preserve the existing deterministic
start path until a source-equivalence change has its own trace.
