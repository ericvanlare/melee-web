# Fountain of Dreams port notes

Fountain of Dreams is the sixth stage being integrated on
`codex/full-game-integration`. Its source profile, world and browser asset
manifest are wired. Native construction, source scheduling and repeated teardown pass with the
original map, reflection, star and animated-light owners. Cold and warm browser
entry now reaches Ready/Go and advancing gameplay without timing resumes.
This is a development candidate, not admitted gameplay.

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

The stage reuses the original joint/material/shape animation consumers,
dynamic platform collision, reflection image and camera, and particle bank
`0x40` from `map_ptcl`/`map_texg`. The checked native map owner preserves public
symbol and texture image-descriptor identity for the original reflection
lookup. Global and per-map light tables keep their own animation owners and
source overrides. The archive's static collision
range has no dynamic-line entries even though the source callback installs
moving collision ownership. The source itemdata root is present but empty, and
there is no Fountain-specific item/article consumer in `grizumi.c`.

The source reflection uses an 80×60 RGB565 image shared by the camera and its
TObj. Map 2 and both map-4 instances also share a camera GObj. Teardown retires
every map instance and the unindexed star, then releases each distinct camera
once while its descriptor owner remains alive. The public catalog cannot close
while either a source stage instance or an external archive handle remains.
Temple's two stage-owner and two match lifetimes pass after this shared teardown
change; the refreshed Final Destination map/public-symbol checks also pass.

The star model requires the original VAT0 GX point primitive. Native geometry
validation now accepts points and line primitives using the existing HSD/Aurora
draw path, while retaining exact vertex-count, index and byte bounds. Its focused
35-case parser suite passes. The restricted asset viewer remains a separate
surface; its earlier point-primitive rejection is retained.

Earlier native runs rejected animated global light tables and the authored
spline carrier flags `0x4018` (`SPLINE | CLASSICAL_SCALE | HIDDEN`). Checked
animation hydration now preserves those flags and borrows storage until all
light owners retire. The next fixture failure treated yakumono x4 as integer
`-1`; its actual word is `0xbf800000`, or signed `-1082130432`, which the existing
typed decoder already preserves. A later fixture confused the global fighter
lights with the separate map-3 stage lights: the former has RGB channels on
light 2 and no WObj path; the latter owns the spline animation. These failures
are retained. Reviewing the two light owners exposed two omitted host calls:
original `Ground_801C466C` construction before stage `on_init`, and
`Ground_OnLoad` after initialization. The host now invokes both original
routines and owns the selected map-light GObj through teardown. Fountain
therefore receives its source RGB/path loop flags, while the global fighter
light chain receives the separate `grIzumi_OnLoad` loop update. Existing stage
load callbacks are empty. The next fixture failure
compared immutable authored flags `0x4018` with live flags `0x4058`: original
`JObjInit` adds `JOBJ_MTX_DIRTY` (`0x40`). Its scale stays `1,1,1`; the
source stage routine scales the WObj position separately. The corrected fixture
checks those source-owned bits explicitly.

The [native scheduler run](../work/full-game/fountain-native-v9.log) passes
two 7,200-tick stage lifetimes. It observes both dynamic platforms and their
collision, RGB color changes, WObj path motion and 899-frame animation looping,
then verifies all map owners retire and immutable archive bytes remain intact.
The existing five-stage native lifecycle regressions and Luigi in both player
orders also pass after the shared light setup correction. A fresh 603-frame
Ganondorf original-state comparison remains exact; drawing is excluded from
that comparison.

## Browser entry and retained failures

The first two browser attempts stopped on the first source draw. The retained
Wasm stack and disassembly locate the trap in `grDisplay_801C5DB0`: its GameCube
cached-address check treated a valid Wasm camera offset as an invalid console
address, then called an unresolved weak `OSReport`. The host patch excludes
only that console-address diagnostic. It retains camera identity checks and
draw order. The platform now implements the original variadic `OSReport`,
`OSVReport` and fatal `OSPanic` interfaces; focused tests check formatted output
and panic termination.

The [third discovery run](../work/full-game/fountain-browser-discovery-v3/report.json)
renders the stage, completes Ready/Go, advances 30 gameplay frames and unloads.
It fails performance: three diagnostic timing resumes, a 1,837.570 ms native
callback, a 1,838.270 ms browser interval, four browser gaps, two long tasks,
149 audio underrun frames and 28 new pipelines. These observations remain
retained separately from the corrected run.

The [cache review](../work/full-game/fountain-cache-review-v1.json) preserves
all 722 base rows and their payloads and adds exactly 28 portable pipeline
descriptors. The resulting seed has one shader and 749 pipelines, with SHA-256
`122eaece4fce109e9f2c958de8b0bb7315ccb670dab349eb2090da1f2fd589a6`.
The [cold/warm entry report](../work/full-game/fountain-browser-entry-pair-v1/report.json)
then passes original CSS/SSS selection, Ready/Go, 30 advancing gameplay frames
and owned-world unload on both application starts. The warm run follows a
complete unload, cache persistence and page reload. Both runs retain focus and
report zero timing resumes, native budget misses, browser gaps, audio underruns
or browser errors. Native/browser maxima are 14.84/31.50 ms cold and
7.74/25.14 ms warm. The cold run still records two browser long tasks, worst
68 ms; the warm run records none. Match preparation is reported separately at
218.64/184.78 ms. This is a short functional-entry pass, not a performance gate.

The visible machine profile is Apple M4, macOS 26.6.2, Chrome
153.0.8010.50, 640×480 backing pixels, DPR 1, with development audio enabled.
Independent original comparison, a complete ordinary match loop, the full
stage/action matrix, rendering/PCM equivalence, physical input and cold/warm
performance remain separate gates.
