# Historical core gameplay and first-menu integration

The active work queue is the [roadmap](ROADMAP.md) and its Reliable local
versus v1 milestone. This page preserves early implementation notes; statements
about the next step, missing menu integration, or a Results-skipping deliverable
below describe that earlier checkpoint and are not current instructions.
[STATUS](../STATUS.md) links current evidence. Do not restart these historical
tasks without identifying a current issue and reproducer.

At this historical checkpoint, the browser ran a two-player Mario stock match
on Final Destination. The then-active deliverable was original CSS → original
SSS → a playable
four-stock Mario/FD match → original CSS, while preserving the
[accuracy contract](ACCURACY_CONTRACT.md). Direct disc import, native menu
archives, scene lifetime and transitions are integrated. The runtime also has
narrower Falco/Battlefield source-loop and browser-rendering evidence. The full
acceptance milestone remains open until the checks in [ROADMAP.md](ROADMAP.md)
pass, including complete ordinary input, physical controllers, audible output,
reference comparison, and cold and warm performance.

The implementation notes below preserve the sequence used to reach the current
native-menu path. [STATUS.md](../STATUS.md) is the authority for current coverage.

## Independent work boundaries

1. **Disc import.** The bounded Blob reader handles ISO/GCM/CISO and checked FST
   lookup independently of the runtime's match-specific asset manifest. Validate
   the original executable, keep data local, and retain malformed-input tests.
2. **First-use rendering.** Measure death, respawn, and specials with the existing
   frame, upload, pipeline, and audio diagnostics. Prepare required graphics before
   play through shared HSD/GX paths. CPU pipeline drainage does not prove GPU
   completion. Do not advance a hidden match or change the 60 Hz source clock.
3. **Acceptance.** Exercise stock loss, respawn, outcome and restart with both
   keyboard and physical controllers. Preserve original-game trace comparisons;
   successful rendering or average FPS does not establish gameplay equivalence.

The first browser character selection → stage selection → stock match → character
selection loop was implemented alongside performance work. It deliberately used
HTML presentation around the original native match, without a results screen or
full rules menu. That scaffold has been removed from the canonical player and
replaced by the original HSD CSS/SSS scenes, assets, and input behavior.
`web/match-flow.mjs` owns immutable selections, phase guards, original selection
IDs, and separate unlock/availability flags. All characters were unlocked while
only Mario and FD were available in that historical scaffold. `web/match-menu.mjs`
owns presentation and delegates
launch/unload through the native command boundary. Stocks range from 1 to 99.
Selections survive return, and controller navigation reuses the gameplay PAD
mapping with release required across screen transitions. Physical controller
acceptance remains pending. Falco is now the first expanded runtime fighter, with
the scoped evidence recorded in [STATUS.md](../STATUS.md).

## Native menu work now implemented

- `DatNativeMenu` hydrates CSS nine and SSS twelve model groups, joint/material/
  shape animations, camera, two lights and fog. Original HSD loading, animation
  and teardown pass across two SDK worlds. The shared shape path retains source
  morph interpolation and canonical GPU bytes, with host-order copies only for
  the original CPU scalar reader. Referenced scalar bits are checked against the
  original arrays. This descriptor gate does not render a screen.
- `native_sss_callbacks` runs original SSS entry, 120 neutral input/scheduler/audio
  ticks and exit across two SDK worlds with original menu music. It does not test
  selection or rendering. Its linked preload-alarm entry points fail explicitly
  if reached and exist only in the test harness; they are not runtime services.
- `DatMenuSupport` supplies the original card icon table and the camera/model
  subset consumed by `lb_8001CF18`. The original card archive loader, camera,
  model and animation consumers pass twice; this does not establish card saving
  or general SceneDesc light/fog support.
- Shared archive publication supports checked opaque handles and original locale
  filename resolution. Explicit handles must close before their asset scope.
  Original heap-scoped card handles are reclaimed only after SDK teardown; early
  release, shared ownership and stale handle access are rejected. Catalog-only
  public names may be declared, but accessing an unhydrated root fails explicitly.
- Palette validation scans each unique indexed image once. Exact synchronized
  TIMG/TCLT programs permit checking the image/palette pairs the original HSD
  animation selects; other programs retain Cartesian validation. Pixel bounds
  and precision remain checked.
  Material trees use the native joint loader's existing 256-node bound; the CSS
  contains a 173-node graph exceeding the earlier fighter-only 140-node limit.
- `DatSis` hydrates the CSS font/text pointer table while keeping bytecode and
  font bytes in their original representation. The original layout interpreter
  now reads its big-endian operands and style stack explicitly. Synthetic
  scaling/spacing/pointer-stack tests and all 85 original CSS text entries pass
  through the real SIS loading/layout/release routines across two SDK lifetimes.
  The local text gate measures the first line of each entry; it does not render
  every string or establish pixel equivalence. Branch bytecode in imported SIS
  archives remains explicitly unsupported.
- `gameplay_menu.c` prepares the source CSS/SSS configuration and lifecycle
  boundary, with explicit runtime/scheduler/transition prerequisites and fixed
  four-stock Mario/FD validation. Its stubbed contract test is not native-scene
  execution evidence. Browser scene ownership and source availability guards are
  still to be integrated.

SSS x20 has seven image/palette entries but retains an authored value seven at
frame seven. Original `mnstagesel.c` selects only frames two through six for
these five unlocked icons, then `do_anim` stops every TObj AObj. Neutral source
scene ticks therefore never dispatch seven. Native menus preserve those bytes;
the TObj boundary checks every dispatched index before conversion/table access.
Other decoder callers default to validating all encoded values. Synthetic tests
exercise both fatal image and palette dispatches, rebind/release, and strict
decoder rejection. No table padding, value clamping or skipped animation is used.

Next bounded integration work:

1. Supply real named audio-bank residency and original load callbacks for CSS
   entry/exit. The current match audio is preloaded; it does not implement the
   dynamic bank requests. Preserve real readiness, capacity and unload behavior.
   Then complete scene preloading against owned, validated runtime assets.
2. Install the SDK menu world, save/unlock state, raw PAD queue and source scene
   transition observer. Enforce Mario/FD availability before original selection
   or archive requests; rejecting an unsupported selection after its callbacks
   ran is only a diagnostic, not a safe UI filter. Preserve all-unlocked status.
3. Run actual CSS/SSS callbacks and rendering, then hand their selected state to
   the native match owner and return after teardown. Replace the HTML selector
   only after the original visible chain works twice with normal input.

Reproduce the local CSS descriptor/consumer gate after extracting `MnSlChr.usd`
into ignored local assets:

```sh
.venv/bin/cmake --build build/browser-release --target native_menu_scene_trace
python3 scripts/check_native_menu.py --css assets-local/native-menus/MnSlChr.usd \
  --sis assets-local/native-menus/SdSlChr.usd \
  --sss assets-local/native-menus/MnSlMap.usd \
  --cards assets-local/native-menus/LbMcGame.usd assets-local/native-menus/NtMemAc.usd
```

## Original menu integration boundary

A strict link probe retaining both `mnCharSel_Scene_OnEnter` and
`mnStageSel_Scene_OnEnter` passes against the current Release runtime libraries.
The original neutral SSS callback gate now runs as described above. CSS callbacks
and rendered menu selection remain untested.

The source VS scene chain is in `melee/gm/gmvsmode.c`, with hooks in `gmscdata.c`.
Use `CSSData`, `SSSData`, `VsModeData`, `PlayerInitData`, and original stock-rule
setup to carry configuration into the current match. The match currently owns
its SDK arena, rendering, audio and stage lifetime; a menu must not initialize
another source scene inside that live world.

The visible screens require these English disc files (all verified present):

- CSS: `MnSlChr.usd`, `MnExtAll.usd`, `SdSlChr.usd`.
- CSS initialization also loads `LbMcGame.usd` and `NtMemAc.usd` through
  `lbCardGame_LoadArchive`; loading their graphics does not establish save support.
- SSS: `MnSlMap.usd`.

`MnSelectChrDataTable` begins with camera, two lights and fog, followed by nine
CSS animation sets at offset 0x10. `MnSelectStageDataTable` has the same scene
prefix followed by the model/animation table in `mnstagesel.static.h`.
Other named roots include `SIS_SelCharData`, `MemCardIconData` and
`ScNtcCommon_scene_data`. All archive bytes must remain owned locally.

The checked archive adapter now supports `lbArchive_80017040`,
`lbArchive_LoadArchive`, `lbArchive_LoadSymbols`, `lbArchive_80016DBC`,
`HSD_ArchiveGetPublicAddress`, and explicit release through owned typed symbols
and checked opaque handles. CSS, SSS, SIS and the consumed card graphics are
hydrated. Unused MnExtAll subscene roots remain unhydrated and must be guarded
from entry; do not overlay big-endian bytes with host structs or introduce
successful file stubs.

Neither screen exposes a roster/visible-stage filter. Initializing Mario and FD
only seeds selection; original cursor code can still select other entries.
A narrow menu restriction must preserve the original input and confirmation paths.
Keep SSS `force_stage_id=-1`: forcing FD skips the visible screen and does not meet
this gate. FD is stage-menu entry 25, `St_Kind_Last=0x20`, mapped to runtime
`Gr_Kind_Last=0x25` (37). Do not conflate these identifiers.

## Foundation to reuse

- [Fighter runtime](FIGHTER_RUNTIME.md): common/fighter/costume graphs, native action
  commands, stage collision, items, effects and source process/destructor lifetime.
- Checked archive extern preservation and typed named-section publication.
  Unsupported references and services still fail explicitly.
- Immutable original archive bytes, mutable owned native display lists and
  descriptor-ID cleanup across complete heap restarts.
- [Testing](TESTING.md): source ABI comparisons, focused native tests, real-data
  probes, browser rendering checks and fixed dependency pins.

Keep worker assignments independent and bounded. Use GPT-5.6 Luna xhigh unless
Eric changes that preference; the lead reviews and integrates their work.

The original neutral SSS callback gate uses separately extracted owned audio:

```sh
.venv/bin/cmake --build build/browser-release --target native_sss_callbacks
.deps/emsdk/node/24.19.0_64bit/bin/node build/browser-release/native_sss_callbacks.js \
  assets-local/native-menus assets-local/next-gate
```

The menu directory contains `MnSlMap.usd` and `menu01.hps`; the audio directory
contains `smash2.sem`, `main.ssm` and locally supplied DSP coefficients. This is a
neutral scene fixture with all-unlocked save settings, not an accepted game flow.
