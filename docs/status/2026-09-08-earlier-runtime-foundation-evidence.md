# Earlier runtime foundation evidence

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
or full-game performance. See [reproduction and scope](../FIGHTER_RUNTIME.md).

Historical checkpoint: **192 tests passed with no skips**, the browser/gameplay
and full fighter targets built, and both gameplay and owned-asset fighter runners
passed. That suite count belongs to the earlier checkpoint. Current verification is
recorded in Active integration above.
