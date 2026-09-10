# Dream Land source-port notes

These notes record the GALE01 revision-2 inputs and source behavior used to add
Dream Land, called Old Pupupu by the source.

## Content identity

- `St_Kind_OldPupupu = 28`; `Gr_Kind_OldPupupu = 28`.
- Source callbacks are published as `grOp_StageData`.
- `GrOp.dat`: 656,823 bytes, SHA-256
  `44ef32a76216c3f47b79c430167e915953c3da0664f121c270be11fd5af2574d`.
- BGM id 35 is `audio/greens.hps`: 3,732,448 bytes, SHA-256
  `a1c5eeb2da6dd5c91600191042dbd694f0c1d2675bfffb5252fbb71022dae463`.
- Stage audio is `audio/us/pupupu.ssm`: 123,488 bytes, SHA-256
  `605f096c82faf0c38d976bfebcabf3914a29b4bef9e81aff6a73d566a543ac90`.

## Archive and source contract

The map archive has eight entries. Source creation order is map ids
`{0, 1, 3, 4, 5, 6, 7, 8}` with animation consumer counts
`{1, 6, 1, 1, 1, 1, 2, 6}`. Entries 1, 2, 4, 6, and 7 have joint animation;
entries 2, 4, and 7 also have material animation. No entry has shape animation.

The shared stage tables contain one joint reference, no splines, 38 light
overrides, ten shadows, and eight flagged objects. Collision has 14 vertices,
11 lines, and four joints, with no moving collision binding. The stage scale is
1 and the light graph contains three lights.

`yakumono_param` is an exact 0x34-byte payload. It defines bird timers 3000 and
4000, bird height 30, tree timers 600 and 1200, wind speed 0.2, inner bounds
-17 and 76, outer bounds -18 and -74, vertical bounds 40 and -10, and blink
timers 180 and 360. The native owner decodes the original integer and float
types and exchanges that allocation only for the scoped source stage lifetime.

Dream Land exposed three reusable native-renderer cases: old point/spot light
descriptors, `JOBJ_PBILLBOARD`, and `POBJ_SKIN` meshes that refer to a shared
joint elsewhere in the model graph. The archive owner resolves shared-joint
identity only after the complete joint preorder exists, while the renderer
still validates the resulting direct matrix palette. The PObj metadata bit used
by this archive is retained as source metadata rather than rejected as geometry.

## Verification

`tests/dream_land_stage_data_trace.cpp` pins the archive topology, animation
services, collision, lights, table counts, and full yakumono payload. The
integrated source-match trace repeats Dream Land with every admitted fighter and
costume, checks stage construction and teardown, and exercises Marth's complete
representative move set. A Release browser run selected the Past Stages Dream
Land tile and rendered Marth versus Mario with zero newly created pipelines on
match entry from the expanded cache. Match preparation took 187.25 ms, with
98.28 ms of source-owner construction, and the first draw took 4.04 ms.

```sh
python3 -m unittest -v tests.test_dream_land_stage_data tests.test_gameplay_content_match
```

For later stages, inspect map ids and per-entry animation consumers before
adding the profile, then decode only the stage-specific parameter block required
by its linked source callbacks. Add native descriptor support at the shared DAT
or HSD layer when the archive proves a new representation; do not special-case
the stage in the renderer.
