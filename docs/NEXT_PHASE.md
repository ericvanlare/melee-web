# Next acceptance gate: a complete fighter model

Render Mario's neutral costume in its bind pose, then evaluate one original idle
animation. Use `PlMrNr.dat` / `PlyMario5K_Share_joint` from the user's local disc.
This moves beyond rigid inspection geometry while reusing the completed original
HSD material path. It still does not establish gameplay or simulation accuracy.

## Local evidence, 2026-09-07

The local audit extracted seven files totaling 3,473,184 bytes: Mario and Fox
fighter metadata and neutral costumes, plus Final Destination, Battlefield and
Fountain of Dreams (`PlMr.dat`, `PlMrNr.dat`, `PlFx.dat`, `PlFxNr.dat`, `GrNLa.dat`,
`GrNBa.dat`, `GrIz.dat`). They remain in ignored `assets-local/next-gate/`.

Mario's costume contains 61 joints, 68 meshes and 53 inverse bind matrices. All
68 meshes use envelope skinning. Its 59 materials are opaque, contain 60 texture
objects and have no custom pixel-engine state. Material preflight accepted three;
the other 56 contain TEV descriptors with `active == 0`. Decode and validate that
descriptor state using original semantics instead of a per-file exemption.

Fox has 73 joints, 91 meshes, 86 envelope-bound meshes and 65 inverse matrices.
It provides a later mixed rigid/skinned regression. One material has shininess
296.363586, demonstrating that the current 128 limit is a port restriction and
must not be treated as a DAT-format requirement.

Stage archives require a typed `map_head` scene adapter before their model entries
can be passed to joint loading. They additionally exercise vertex colors,
transparency/custom pixel-engine state and animation, making Mario the narrower
first gate. Running every public symbol through the current rigid-joint checker
is useful for rejection details but is not meaningful game-coverage scoring.

## Parallel work boundaries

- Decode inverse bind matrices, envelope references, matrix-index attributes and
  inactive TEV descriptors into checked CPU types. Preserve original payloads.
- Retain original HSD skinning and matrix-palette routines behind owned runtime
  objects, with source/reference traces for matrix and weight behavior.
- Establish original animation descriptor/evaluation requirements independently;
  add the idle animation after the complete bind pose passes.
- Keep browser integration and acceptance with one owner: compare pose/geometry,
  reject unsupported paths visibly, verify replacement/freeing and collect browser
  errors. Run relevant tests and integrate once this gate passes.

Keep the static bucket, fan and bullseye as regression cases. Continue sampling
input independently of rendering; scheduling, disc services, audio and the versus
loop remain subsequent game-integration work.
