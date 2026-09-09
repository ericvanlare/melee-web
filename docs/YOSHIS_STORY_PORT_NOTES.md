# Yoshi's Story source-port notes

These notes record the exact owned Rev 2 inputs and source behavior used by the
browser port. They are intended to keep later stage work from rediscovering the
same archive and lifecycle details.

## Content identity

- `St_Kind_Story = 8`; `Gr_Kind_Story = 10`.
- Source callbacks are published as `grSt_StageData` in `gr/grstory.c`.
- `/GrSt.dat`: 824,648 bytes, SHA-256
  `1ef0ccc51fc69bf2e06f55377111ec1b67e00438df0597bf6195c3032ae29f83`.
- BGM id 96 is `audio/ystory.hps`: 4,347,232 bytes, SHA-256
  `a99e0c3b6164e6a3d0573c8b9fe87055965c7c859fc21caf1d5b351738acfb45`.
- `TyHeiho.dat` is unrelated to the stage-owned Shy Guy Article and is not a
  runtime dependency.

The normal-versus `StageParam` row is stage kind 8, primary and fallback BGM
96, alternate ids -1, and selection values `0, 1, 100`. `grGroundParam` has
scale 0.7, words `220, 20, 0, 100, 1000, -10`, an eight-row stage parameter
table, and the first/normal row above.

## Map archive

`map_head` contains four entries. The source creates map ids in order 0, 1, 3,
2 and requires all four. Consumer animation counts are `{1, 1, 1, 2}`. Entry
0 has no animation tables; entries 1 and 2 use joint animation slot 0, entry 2
also has material animation slot 0, and entry 3 uses joint animation slots 0
and 1. There are no map shape-animation tables.

The table counts are two joint references, one spline, twenty light overrides,
zero shadows, and seven flagged objects. Entry 2 has the only collision
binding: collision joint 0, source stage entry -1, render joint 1.

The source comments beside the map callback table are misleading. Map id 2 is
Randall and owns `Ground.u.randall`; map id 3 owns the Shy Guy scheduler and
`Ground.u.shyguys`. `grStory_801E3030` creates every map object immediately, so
there is no deferred background object state machine.

## Stage parameters and dynamic objects

`yakumono_param` is exactly nine floats (0x24 bytes): timer minimum 600, timer
range 1800, spawn-many rarity 8, then vertical positions
`{30, 45, 60, 75, 90, 0}`. The source later fixes the active Shy Guy timer to
120 frames. A spawn contains one Shy Guy unless `HSD_Randi(2)` returns zero; in
that case it contains `HSD_Randi(3) + 3`, or three through five. The source
spawns kind `0xd2` through `it_802D8618`, starting at x -292 or 304, z 2, with
the selected vertical position and per-item jitter.

Randall publishes its moving collision through `Ground_801C2FE0`. Its cloud
puff uses stage effect id `0x2c` and resets the timer to `HSD_Randi(20) + 10`.
Stage start also calls `grZakoGenerator_801CAE04(NULL)`. The line-touch callback
returns null. These paths require the existing source RNG, item, moving
collision, animation, and stage-effect services; there is no stage-local
substitute.

## Embedded Heiho Article and external symbol

The public `itemdata` root at 0x2a47c is a null-terminated list containing one
pair: item kind `0xd2` and Article root 0x32d80. The Article is fully embedded:
common attributes 0x32c48, special attributes 0x32ce0, hurtbones 0x32d48,
states 0x32d50, model descriptor 0x32d38, and model joint root 0x2a768.
Dynamics are null.

Heiho has a 24-byte special block and three Article animation descriptors. Its
five logical motions map through source animation ids `{-1, 0, -1, -1, 2}`;
the Article table itself therefore has three rows. Special word 0 is a relocated
pointer to an embedded signed threshold value 15. Words 1 through 5 are the
floats `0.3, 0.5, 0.75, 1.5, 2.0`. Native hydration must allocate the threshold
and write its Wasm32 pointer into word 0 rather than treating all six words as
floats.

`GrdStoryHeiho_TopN_shapeanim_joint` is the archive's sole external. Its chain
slots are 0x32d58 and 0x32d78, corresponding to unused shape-animation fields
in Heiho state rows 0 and 2. No DAT/USD on the owned disc exports that symbol.
This matches `lbArchive_InitializeDAT`, which validates each external chain and
passes null to `HSD_ArchiveLocateExtern`. GrSt must therefore use checked
resolve-to-null loading; it must not invent or load an external provider.

The original `Ground_801C0800` registers `stage_info.itemdata` through
`it_8026B40C`. The browser begin path calls the stage callback directly, so it
must decode and retain this list, publish it around the source stage lifetime,
and restore both `stage_info.itemdata` and the affected `it_804A0F60` slot. The
scope must remain alive until all Heiho objects have been removed.

## Collision, markers, and lights

`coll_data` begins at 0x29294 and ends at the next public root at 0x292c0. Its
source-consumed descriptor is 0x2c bytes, with 34 vertices, 29 lines, and two
collision joints. The optional word at +0x2c is absent; reading it would borrow
the first word of `map_ptcl`. Category ranges are floor `(0,7)`, ceiling `(0,0)`,
right wall `(7,11)`, left wall `(18,11)`, and dynamic `(0,0)`.

The marker tree has 22 nodes and 21 bindings. After applying scale 0.7, the
camera origin is `(0, 30.8, 0)`, camera bounds relative to that origin are left
-126, bottom -80.5, right 125.3, top 87.5, and blast bounds are left -175.7,
bottom -121.8, right 173.6, top 137.2. Player starts are `(-42,26.6)`,
`(42,28)`, `(0,46.9)`, and `(0,4.9)`.

There are two static source lights and no light animations. Their colors are
`(204,204,204,255)` and `(220,220,220,255)`. The second light's scaled position
is `(-3.5,5.6,4.9)` and its override flags are 0xe0.

## Focused verification

`tests/story_stage_data_trace.cpp` validates the exact null external identity,
map/table topology, animation consumers, collision binding and descriptor,
lights, scale, normal BGM row, and scalar yakumono payload directly against the
owned archive. The integrated Wasm content trace now checks all four live map
objects, Randall's bounded timer state, moving-collision ownership, the Shy Guy
scheduler, real Heiho item creation and repeat teardown. A Release browser run
selected the upper Yoshi's Story tile through the original SSS and rendered the
source match with its tree, platforms and stage geometry. Puff-effect inspection,
audible music and retail comparison remain open.
