# Core gameplay loop and the first menus

The browser now runs a two-player Mario stock match on Final Destination.
The immediate cycle is direct disc import, first-use rendering preparation,
and complete match/restart verification. The full acceptance milestone remains
open until the checks in [ROADMAP.md](ROADMAP.md) pass, including physical
controllers, audible output, and cold and warm performance.

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

After this cycle, build Mario-only character selection → FD-only stage selection
→ stock match → return, initially without the full rules menu. Use shared original
menu and roster interfaces. Falco is the first planned roster expansion after
that flow and the core loop are stable.

## Original menu integration boundary

A strict link probe retaining both `mnCharSel_Scene_OnEnter` and
`mnStageSel_Scene_OnEnter` passes against the current Release runtime libraries.
This is link evidence only; neither original menu has run in the port yet.

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

The checked archive adapter currently intercepts only `lbArchive_80017040`.
Menus also use raw `lbArchive_LoadArchive`, `lbArchive_LoadSymbols`,
`lbArchive_80016DBC`, and later `HSD_ArchiveGetPublicAddress`. Extend this shared
boundary with owned, checked typed data and explicit lifetime; do not overlay
big-endian archive bytes with host structs or introduce successful file stubs.

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
