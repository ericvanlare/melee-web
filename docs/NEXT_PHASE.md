# From separate inspections to a fighter and stage scene

The current gate is complete: Mario and Fox use the same source-derived costume,
common-part and action-table binding; original HSD evaluates idle and walking
clips from full AJ containers. Final Destination entry 3 renders its complete
opaque pass through the shared model loader, reporting its remaining passes.
This still does not boot Melee or execute a match.

## Established interfaces

- The generated source registry maps exact model symbols to fighter kind,
  costume index, metadata identity, action count and animation-container identity.
  Build-time regeneration checks reject source drift and unsupported syntax.
- Typed common layouts and action records provide explicit node mappings and
  bounded clip slices. Ordinary NULL-alternate layouts are supported; alternate
  insertion and motion-mask behavior remain explicit future work.
- Mario and Fox Wait1/WalkSlow work through the same parser/evaluator path.
  Normal visibility selects 43 DObjs / 52 meshes for Mario and 45 / 59 for Fox.
- `DatStage` enumerates source entries rather than interpreting arbitrary public
  symbols as joints. Optional services are reported as unapplied.
- A model can load from a checked joint offset and an explicit render pass.
  Opaque selection keeps mesh/envelope dependencies and ancestors, preserves
  source DObj identities, and reports omissions. It never clears unsupported
  flags on a required joint.
- Final Destination entry 3 retains 4 joints, 13 meshes, 5 textures, 84 packets
  and 4,619 vertices; 13 translucent meshes and one unused billboard joint are
  omitted. The vertex-color extension and removal of an invented shininess cap
  follow original HSD behavior and also benefit other assets.

## Next parallel work

1. **Translucent rendering and billboard context.** Implement original HSD
   translucent/depth behavior for the remaining platform meshes. Establish the
   source ordering and camera-dependent transform requirements before calling
   the entry complete. Extend shared behavior using representative source traces.
2. **Stage animation and scene services.** Decode stage HSD_AnimJoint and
   MatAnimJoint tables, which differ from fighter FigaTree records. Reuse original
   evaluators and owned runtime objects. Preserve source entry selection; original
   stage callbacks decide which entries are active together.
3. **Fighter action semantics and reference data.** Audit original action commands
   for visibility/material changes and motion flags using actual action-table
   records. Establish original-game reference poses and state traces before
   claiming complete action behavior. Keep unsupported commands explicit.
4. **Integration and acceptance.** Compose one fighter and stage under a shared
   camera and scheduling boundary as the required services become ready. Keep
   rendering and browser input independent of the eventual simulation clock.
   Integrate each working boundary after focused checks instead of batching all
   unfinished systems into one large change.

The ordinary registry/layout contract can admit more fighters, but their model
features, normal variants and action channels still require validation. Kirby,
Link and Young Link have nonnull alternate-part descriptors in the current disc;
implement their original insertion/motion-mask path before accepting those layouts.

Keep Mario/Fox idle and walk, the platform opaque pass, and bucket/fan/bullseye as
regressions. Use explicit `--stage-entry` / `--opaque` batch checks to select the
next shared capability, and keep game bytes in ignored `assets-local/`.
Audio, collision, physics, scene initialization and the versus loop remain on the
path to a playable port; no successful inspection substitutes for those systems.
