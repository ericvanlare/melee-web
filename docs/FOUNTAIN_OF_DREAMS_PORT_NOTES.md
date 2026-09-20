# Fountain of Dreams port notes

Fountain of Dreams is a source scout and decoder-only boundary. It is not in the
shared stage registry, match selection, world owner, build graph, or gameplay
patch.

The pinned source is GALE01 revision 2, commit
`b43912cc78606f96c9569f5d6229bc9d7e265ea5`. The source identity is
`St_Kind_Izumi` (`2`) mapped to `Gr_Kind_Izumi` (`12`) by
`melee/gr/stage.c`; its archive is `/GrIz.dat` and its selected versus row uses
primary BGM `49` (`audio/izumi.hps`). The selected `grGroundParam` has `y=0.75`,
`x4=49`, `x8=-1`, `xC=49`, `x10=-1`, `x14=0`, `x16=1`, and `x18=100`. The
complete archive and disc receipts are retained in the ignored
`work/full-game/stage-scout-fountain-001/contract.json` and
`capture-manifest.json`.

The source `grIzumi_YakumonoParam` is a complete named struct: `float x0`,
32-bit signed `int x4`, and floats `x8` through `x50`, for a consumed size of
`0x54` bytes. The DAT public root spans `0x70` bytes to the next public symbol;
the decoder in `src/gameplay_stage_fountain.c` validates and copies only the
source-consumed prefix, preserving the trailing bytes as opaque archive data.
The source-derived layout assertions in `tests/test_fountain_yakumono.py`
compile the exact pinned struct declaration against the portable header. The
resulting ignored receipt is
`work/full-game/stage-scout-fountain-001/yakumono-layout-proof.json`.

The archive has five `map_head` entries. Normal `grIzumi_801CBB88` setup creates
map IDs `0`, `1`, and `3`; its row-3 callback creates map `2` and two dynamic
map-4 platform objects. Row 3 binds source collision through
`Ground_801C2ED0`, creates the reflection and particle owners, and starts the
platform objects. Row 4 owns their source state machine and calls
`mpLib_80055E9C`; the scalar fields used by these paths remain identified by
source offsets in the ignored contract. Callback rows 5 through 10 are repeated
source callback slots without corresponding archive map entries.

The remaining runtime dependencies are source-owned and still unintegrated:
map joint/material/shape animation consumers, dynamic platform collision,
reflection image and camera, particle bank `0x40` from `map_ptcl`/`map_texg`,
source light override identity and `GroundParam.y` scaling, null fog behavior,
audio handoff, and full object/global teardown. The archive's static collision
range has no dynamic-line entries even though the source callback installs
moving collision ownership. The source itemdata root is present but empty, and
there is no Fountain-specific item/article consumer in `grizumi.c`.

Current evidence covers only the scalar decoder and its focused synthetic and
source-layout tests. Adding a profile, registry row, world wiring, CMake target,
patch, or playable stage requires the complete source lifecycle boundary and a
separate source-backed trace.
