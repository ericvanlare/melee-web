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
costume and checks stage construction and teardown. The Release browser gate
selects the Past Stages Dream Land tile through source SSS input and runs the
46-case Marth inventory plus at least 4,200 source frames, which crosses the
stage's deterministic bird, tree, wind and blink scheduler intervals. With the
344-pipeline seed and a cleared origin, the run passed 6,070 source frames with a
22.785 ms worst browser interval, 19.38 ms worst native callback, and zero timing
gaps, long tasks, audio underruns, live pipeline creation, automatic pauses or
focus loss. Wasm heap growth is recorded separately for timing correlation. The
second-application warm run also passed all 46 cases across 6,109 source frames,
with a 21.06 ms worst browser interval, 15.325 ms worst native callback and every
hard gate at zero. Match preparation took 314.77 ms in the cleared-origin run
and occurred with the source clock stopped; the first draw took 7.11 ms with no
queued or newly created pipelines.

```sh
python3 -m unittest -v tests.test_dream_land_stage_data tests.test_gameplay_content_match
```

For later stages, inspect map ids and per-entry animation consumers before
adding the profile, then decode only the stage-specific parameter block required
by its linked source callbacks. Add native descriptor support at the shared DAT
or HSD layer when the archive proves a new representation; do not special-case
the stage in the renderer.
