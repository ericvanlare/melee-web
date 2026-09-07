# From Mario inspection to a fighter and stage scene

The first fighter gate implements the shared CPU decoding and original HSD
runtime needed for Mario's neutral costume and Wait1 idle animation. It is a
source port: no PowerPC instruction interpreter or JIT is involved.

## Established boundaries

- `PlMrNr.dat` has 61 joints, 53 inverse bind matrices, 68 envelope-skinned meshes,
  257 envelopes, 379 influences, 59 materials and 60 texture objects. Geometry
  carries 314 primitive packets and 6,982 submitted vertices before visibility.
- `PlMr.dat` supplies normal versus alternate representation visibility. Its
  normal selection uses 43 of 59 DObjs and 52 of 68 meshes. DObj indices differ
  from mesh indices because one display object can own multiple polygons.
- The Wait1 action table selects bytes `[0, 4239)` of `PlMrAJ.dat`. The resulting
  FigaTree has 61 nodes, 111 tracks and end frame 50. Original HSD AObj/FObj/spline
  code evaluates it, with owned streams and checked descriptors.
- Default Mario's animation-node preorder is verified against `PlCo.dat` and
  original `ftParts_SetupParts` / `ftAnim_8006F4C8`. Costume identity comes from
  original source tables, not the DAT. The initial binding is deliberately
  restricted to that proven hierarchy; the decoders are generic.
- Inspection playback has fixed 60 Hz steps, explicit looping, bounded catch-up
  and pause/recovery. These controls do not implement gameplay scheduling.

Keep the rigid bucket, fan and bullseye as regression cases. Game bytes stay in
ignored `assets-local/`; tests use synthetic fixtures and pinned original code.

## Next parallel work

1. **Fighter binding and action data.** Add typed common-parts and action-table
   loading so costume identity, alternate joint maps and animation slices derive
   from original registries/data. Replace the initial Mario-only binding with
   a checked reusable binding. Exercise a second fighter and more than one action.
2. **Stage scene loading.** Decode `map_head` into typed scene entries and feed
   supported joint roots into the existing renderer. Use the local Battlefield,
   Final Destination and Fountain corpus to identify the first shared missing
   graphics feature. Keep rejection reasons precise.
3. **Graphics coverage and comparison.** Resolve source-backed restrictions
   exposed by that corpus, including vertex colors, transparency/PE state and
   material animation where required. Compare representative frames to the
   original game using its camera and lights before claiming scene fidelity.
4. **Integration and scheduling.** Connect these boundaries into one fighter and
   stage inspection scene with a single owner. Keep browser input and simulation
   scheduling independent of presentation; only broaden into action/physics logic
   after concrete dependencies and original reference traces are established.

Fox's neutral costume supplies a mixed rigid/skinned regression: 73 joints,
91 meshes, 86 skinned meshes and 65 inverse matrices. A shininess value of
296.363586 exceeds the current port's 128 limit; that limit is not a DAT-format
requirement. Stage archives also need scene adapters before their public symbols
can be meaningfully counted as model coverage.

Integrate each runnable boundary after its relevant tests and browser checks.
Do not wait for broad cleanup or treat parser acceptance as game coverage.
Audio, complete scene services, physics, collisions and the versus loop still
remain on the path to a playable game.
