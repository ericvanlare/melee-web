# Pokémon Stadium port notes

**Source identified** only. Nothing here is integrated, linked into a profile,
traced or rendered. These notes record the source, archive and service contract
an execution agent needs to add the stage through the
[stage checkpoints](../ADDING_STAGES.md) without rediscovering it. They serve
the Track B legal-stage item (B2) of the
[priorities tracker, #158](https://github.com/ericvanlare/melee-web/issues/158).

How each claim was established. Source claims were read from the pinned
doldecomp/melee `b43912cc78606f96c9569f5d6229bc9d7e265ea5` checkout
(`.deps/melee`, not tracked here; paths below are relative to `src/melee`
unless they begin `sysdolphin/`). Archive and disc claims come from a read-only
decode of the owned GALE01 revision 2 image into an untracked run directory.
Claims marked **derived** are arithmetic on those facts, and **unmeasured**
claims have no observation behind them. Every numeric claim must be reproduced
by the checkpoint-0 data trace before code depends on it. `grpstadium.c`,
`grdatfiles.c`, `granime.c`, `lbfile.c`, `lbdvd.c`, `lbspdisplay.c` and
`devcom.c` are `Matching` in the pinned `configure.py`, so their quirks are
authoritative and are reproduced, not repaired.

## Content identity

- `St_Kind_PStadium = 3` maps to `Gr_Kind_PStadium = 16` (`gr/forward.h:71,144`;
  `gr/stage.c:342`). The source callbacks are `grPs_StageData`
  (`gr/grpstadium.c:145-159`), which names the archive `"/GrPs"` with no
  extension.
- `lbFileGetFullName` keeps an existing extension and otherwise appends `.usd`
  when the **saved language** is US, else `.dat` (`lb/lbfile.c:56-90`). The
  transformation names carry `.dat` and are never localized. The stage archive
  is one of five localized stage archives on the disc, with Corneria,
  Home-Run Contest, Onett and Venom. The two variants are byte-identical before
  `SIS_GrPStadiumData` (data offset `0x13ca80`) and share every public name,
  extern and relocation there; only the screen text differs. The runtime
  archive is **`GrPs.usd`**. The world loader resolves English
  (`src/gameplay_world.cpp:79-98`), but the source request is made by the
  source predicate, so checkpoint 1 must assert that the source asks for
  `GrPs.usd` under the port's save profile.
- The row for StKind 3 is row 0 of the 18 `StageParam` rows behind
  `grGroundParam.stage_params` (data offset `0x3d380`, stride `0x64`). The 18
  StKinds are 3, 72, 100, 107, 115, 124, 142, 143, 161, 170, 172, 184, 208, 240,
  261, 269, 274 and 283; only StKind 3 is a competitive VS row. Its values are
  BGM words `x4=64`, `x8=63`, `xC=64`, `x10=63`, selection fields `x14=6`,
  `x16=12`, `x18=100`. The `GroundParam` scale is `y=1.0`
  (`GroundParam` and `StageParam`: `gr/types.h:1968-2027`).
- In the source `hps_files[]` table (`lb/lbaudio_ax.static.h:250-275`) ID 64 is
  `pstadium.hps` and ID 63 is `pokesta.hps`. The music closure must carry both,
  as Temple's does (`src/gameplay_asset_manifest.cpp:160-198`). The source
  selector chooses between them; see the RNG section.
- The stage sound bank is `pstadium.ssm`: `s32_arr_803BB6B0[16]` selects bank 48
  (`lb/lbaudio_ax.static.h:150-156`, `lb/lbaudio_ax.c:1692-1705`) and
  `ssm_files[48]` is `pstadium.ssm`. Its header declares two samples (sample IDs
  1453 and 1454; `src/dat_audio.cpp:35-40` reads this layout). The stage plays
  exactly SFX `480000` and `480001` (`0x75300`, `0x75301`;
  `gr/grpstadium.c:2138-2139`). Mapping those two SFX IDs onto the two samples
  goes through `smash2.sem` and has not been checked here.
- The English bank is `audio/us/pstadium.ssm`. The same-size Japanese
  `audio/pstadium.ssm` differs in bytes and must not be used.

## Files

Disc facts come from the FST (1,212 entries). The retail pseudo entry numbers
`0x7D2`-`0x7D5` used below are not FST entries.

| File | FST # | Disc offset | Bytes | SHA-256 | Role |
| --- | --- | --- | --- | --- | --- |
| `GrPs.usd` | 419 | `0x4a44bc28` | 1,461,024 | `aa740cfbbeced294f058449caca8ac0380532521dde06ca4a6dcc03080cf6a6d` | Stage archive (English) |
| `GrPs.dat` | 418 | `0x4cb88000` | 1,468,544 | `0b55e7ba6234b882a35edf80d2d6c7f84d9fc887cbd4fe9c85b38ed7c3ea4e18` | Japanese stage archive; not loaded under English |
| `GrPs1.dat` | 420 | `0x4ccf0000` | 327,617 | `ea94e509953a024cb72d6c917b9ad3bf9ebc7c6a40d5d091daa613adafdb3ad7` | Fire (map 3) |
| `GrPs2.dat` | 421 | `0x4cd40000` | 213,114 | `0232063e3393ee08cce329207e06bf237059b9585cfba96d2e4ab7274ca26375` | Grass (map 4) |
| `GrPs3.dat` | 422 | `0x4cd78000` | 297,517 | `aa167c82d65d93a7ac85aff513e3a0d02963a12bb50d1d8f76fc73b2e09205a8` | Water (maps 9, 7, 8) |
| `GrPs4.dat` | 423 | `0x4cdc8000` | 282,884 | `861a04b08dcaa98f098bdb5c5237e6710e27cbafbb74814005407789cb3bfce9` | Rock (map 6) |
| `audio/pstadium.hps` | 113 | `0x43db20c0` | 2,303,488 | `66d873dd38973f3cf7e328365f32120648ca6f779ccd849226ff4c19e3c058f4` | Primary BGM 64 |
| `audio/pokesta.hps` | 112 | `0x43a47ae0` | 3,581,408 | `00ff265d45dd8a72ecd1d80f192d3d32457e0990a5c93e699a42aac9e1ec5439` | Alternate BGM 63 |
| `audio/us/pstadium.ssm` | 186 | `0x49987d00` | 101,792 | `3b95045913d27469d87720f94b91e1bce4272e3ebabc743d74d0e8e863d34b25` | Stage SFX bank 48 (already in the menu bank map: `web/runtime-assets.mjs:37`, `src/gameplay_menu_browser.cpp:589`) |

The four transformation archives total 1,121,132 bytes, sit in consecutive FST
entries and start on 32-byte boundaries. Their `ROUND_UP_32` tails (31, 6, 19
and 28 bytes) are zero on this disc. The datfile order is
`datfiles[] = {GrPs1, GrPs2, GrPs3, GrPs4}` for fire, grass, water and rock
(`gr/grpstadium.c:1912-1917`; selection at `2078-2088`). It agrees with the
archives' own map entries and with Slippi's enum (fire 3, grass 4, normal 5,
rock 6, water 9; pinned slippi-js `src/common/types.ts:307-313`).

The `TyPoke*.dat` trophies, `pokemon.ssm` and `ff_poke.hps` are not read by the
stage. `pokemon.ssm` is the existing Poké Ball Pokémon bank already in the menu
registry.

## Archive contract

All five archives share one `map_head` whose twelve words are identical:
`{0x50, 1, 0x88, 10, 0, 0, 0x290, 0x30, 0, 0, 0x350, 44}` at data offset
`0x400`. That is one joint-reference row, ten map entries (stride `0x34`), no
splines, a light-override pointer with count word `0x30`, no shadows and 44
flagged objects. The tables occupy data offsets `0x00`-`0x3ff`. Every extern
slot (75/91/88/71/91 in GrPs.usd/1/2/3/4) lies inside that range; none lies in
model data. Each archive owns its own entries and reaches the others' entries
through externs. `lbArchive_InitializeDAT` resolves every extern to null
(`lb/lbarchive.c:17-36`), and the port already loads the stage archive under
`DatExternalPolicy::ResolveNull` (`src/gameplay_world.cpp:275`).

| Map | Archive owning the non-null joint | Callback row (`grpstadium.c:72-143`) | Created by |
| --- | --- | --- | --- |
| 0 | GrPs.usd (also non-null in GrPs1-4, never reached) | none | `OnInit` |
| 1 | GrPs.usd | screen: `801D1290`/`801D1390`/`801D13C4` | `OnInit` |
| 2 | GrPs.usd | base and scheduler: `801D13E0`/`801D1520`; flags `0xC0000000` | `OnInit` |
| 5 | GrPs.usd | default platforms: `801D1570`/`801D1604`/`801D1624` | map 2 `on_init` |
| 3 | GrPs1.dat | fire: `801D1840`/`801D19D8`/`801D19F8` | scheduler |
| 4 | GrPs2.dat | grass: `801D1648`/`801D16DC`/`801D16FC` | scheduler |
| 6 | GrPs4.dat | rock: `801D1720`/`801D17E8`/`801D1808` | scheduler |
| 9 | GrPs3.dat | water: `801D1A38`/`801D1B48`/`801D1D84` | scheduler |
| 7, 8 | GrPs3.dat | water children: `801D1DE4` | map 9 `on_init` |

`grDatFiles_801C6330(id)` returns the first of four archive slots whose entry
`id` has a non-null joint (`gr/grdatfiles.c:135-151`). GrPs.usd is slot 0, so
map 0 always comes from it. The transformation archive takes the next free slot
in `grDatFiles_801C6478` (`gr/grdatfiles.c:153-169`), which reads only
`map_head`. It also sets `0x4000000` in each non-null flagged object
(`grdatfiles.c:99-110`). Each transformation archive's own `coll_data`,
`grGroundParam` and `yakumono_param` copies are never read.
`Ground_GetStageGObj` takes the map count from slot 0
(`gr/ground.c:817-` `map_id < archive->unk4->unkC`) and dereferences the owning
slot without a null check, so a map can be created only while its archive is
resident. That makes the slot lifetime part of the contract.

Normal `OnInit` creates map IDs **0, 1, 2, 5** in that order. Map 5 is created
inside map 2's `on_init`, so the object order is 0, 1, 2, 5 and the process
registration order is map 1, map 5, then map 2 (`grpstadium.c:163-184,
202-228, 302-340`). Profile values: `required_map_ids = {0, 1, 2, 5}`,
`entry_count = 10`. Every `on_init` consumes animation slot 0 only, through
`grAnime_801C8138(gobj, map_id, 0)`, so the animation consumer counts are ten
explicit ones. Authored table lengths, as counted by the decode tool up to the first
unrelocated word (the profile uses the consumer count above, not these):
entries 1, 2, 3, 4, 5 and 9 have one material-animation slot; entry 6 has one joint slot; entries 7 and 8 have one
joint and one material slot; no entry has shape animation. Map 1 also calls
`grAnime_801C77FC(gobj, 0, 7)`. Case 4 freezes the outgoing map with
`grAnime_801C7A04(xE4, 0, 7, 0.0f)`. The per-entry `animation_flags` (`x28`)
slots exist in every archive, including for externally owned entries.

Map 2's `StageCallbacks.flags = 0xC0000000` makes it the owner of the stage-wide
light set (`flags_b0`, `gr/ground.c:2666`) and the fog selector (`flags_b1`,
`gr/ground.c:1101`). Its entry has lights and no fog. No callback row has
`flags_b2`, so the per-entry cameras (`x10`) are never instantiated
(`gr/ground.c:893`).

Other public roots of GrPs.usd (data offset, span to next root):

- `coll_data` `0x22c90` (0x30): 155 vertices, 136 lines, 8 joints. Category
  ranges: floor `(0,71)`, ceiling `(71,5)`, right wall `(76,15)`, left wall
  `(91,21)`, dynamic `(112,24)`; the `+0x2C` word is present and zero.
- `map_ptcl` `0x22cc0` and `map_texg` `0x23b80`: bank 30, base ID 30000, 30
  generators, 10 texture groups. They are published as source banks 0x40 and
  0x1E like the other stages.
- `grGroundParam` `0x3da88` (0xdc); `itemdata` `0x3db64` (one null word: no stage
  Articles); `ALDYakuAll` `0x3dbd8`; `map_plit` `0x3dc80` (three lights);
  `quake_model_set` `0x161388` (model plus four authored animations and a
  terminator, which satisfies the existing world check at
  `src/gameplay_world.cpp:515-523`).
- `yakumono_param` `0x3db68` (public-symbol interval `0x70`; the next referenced target begins at `+0x54`, as detailed under Data bounds).
- `GrdPStadiumBG_OVDummy_mat6962_GrdPStadiumDummy_0_image_desc` `0x89cc`: a 16×16
  I4 dummy descriptor referenced exactly once, from `0x8a30`.
- `SIS_GrPStadiumData` `0x13ca80`: a 22-entry `u8*` string table.

Per-archive measurements: GrPs.usd has 97 public symbols, 1,762 relocations and
75 externs. GrPs1/2/3/4 have 31/25/39/28 public symbols, 929/530/1,080/1,063
relocations and 91/88/71/91 externs.

Markers: the single joint-reference row binds 20 marker IDs to map 0's joint
tree: `0x95`-`0x98`, `0x7F`-`0x88` (`0x87` twice) and spawns `0`-`4`. The local
translations of the blast markers `0x97` and `0x98` are `(-230, 180)` and
`(230, -111)`, which give the expected blast box at scale 1.0. **No row binds
`0x94`.** `Ground_801C39C0` requires `0x95`, `0x96` and `0x94` together, so it
takes its source `"use dummy CamRange ...!"` default: camera bounds left -170,
right 170, top 120, bottom -60, offset (0, 0) (`gr/ground.c:2142-2247`).
Reproduce this; do not "fix" it. The checkpoint-2 trace must confirm
`stage_info.cam_info` against the original.

`Ground_GetStageGObj` documents that `alloc_user_data_ground` returns an
uncleared block (`gr/ground.c:841-845`). No `grpstadium.c` callback was found
reading a `Ground` field before its `on_init` writes it, but the native owner
must not rely on zeroed storage, and checkpoint 2 should assert that.

## Mid-match archive loading

**Answer: yes, the source loads each transformation archive asynchronously in
the middle of a match, through the DVD/DevCom path, and the browser as built
cannot serve that read.** There is no ARQ/ARAM and no busy wait for it.

What the source does. Map 2's process runs `grStadium_801D4548`
(`grpstadium.c:2007-2235`). It is gated by `xC4_b0` until the GO callback
(`fn_8016B7F8`, `gm/gm_16AE.c:487-501`) runs `Stage_802252E4` →
`Ground_801C0FB8` (`gr/ground.c:693-709`), whose `Ground_801C10B8` event list
runs `fn_801D13C8` and clears the bit. The state `xDC` then steps through:

0. Count down `xD8`; trigger when the value before the decrement is negative.
   In the default form pick the type with **one** `HSD_Randi(4)` over
   `{3, 4, 6, 9}`. The rejection loop compares against `xE2`, which is always -1
   or 5 at that point, so it never repeats and the same type can recur. Free the
   previous slot (`grAnime_801C65B0(xD0)`), set map 2's `xC4_b1` and call
   `lbFile_80016580("GrPsN.dat", xCC, &xC8, fn_801D4220, NULL)`
   (`grpstadium.c:2096-2098`). In a transformed form the pick is 5 (default)
   and the case jumps straight to 2 with no load.
1. Poll `grStadium_801D42B8` once per tick. Once `xC4_b1` is clear, call
   `grDatFiles_801C6478(xCC, xC8)` synchronously in that tick.
2. Point the screen at the type image (display mode 2-6) and clear `xD8`
   through its `u.display.xD8` alias.
3. Hold. The test `xD8++ > 300` makes this 302 ticks. Then mark the outgoing map
   (`xC4_b1`), play SFX 480000 and 480001, and continue.
4. Shrink the outgoing map's Y scale by `scale × 0.95 / 120` per tick to a floor
   of 0.05, hold for 62 ticks, then create the incoming map at Y scale 0.05,
   translate Y -10 and enable wall lines 85 and 111. Under float32 this is about
   119 + 62 = 181 ticks (**derived**; confirm by trace).
5. Rise for 120 ticks, then destroy the outgoing map (121 ticks).
6. Set the incoming map's `xC4_b0`, run `mpLib_80058560`, and draw the next
   countdown. When returning to the default form also disable wall lines 85 and
   111 again (`mpLib_800575B0`) and run the quirk below.

Total from trigger to case 6 is about `607 + L` ticks (**derived**), where `L`
is the number of case-1 polls. The first trigger falls at post-GO tick 3,602 to
3,801 (`xD8 = 3600 + Randi(200)`, then `xD8 + 2` ticks; **derived**). Later
countdowns are `3600 + Randi(200)` after a transformation and
`1200 + Randi(600)` after the default form, so a transformation starts 1,202 to
1,801 ticks after a transformed form finishes.

The read path is `lb/lbfile.c:121-129` (`lbFile_800164A4`) →
`sysdolphin/baselib/devcom.c:384` (`HSD_DevComRequest`) →
`devcom.c:322-375` (`HSD_DevComDVDWakeUp`). `lbFile_800164A4` takes the size
synchronously through `DVDFastOpen`. It then issues one
`HSD_DevComRequest(file, 0, buffer, ROUND_UP_32(size), type, priority 1,
callback, NULL)`. The type is `0x21` because the destination is at or above
`0x80000000` (retail heap addresses). `HSD_DevComDVDWakeUp` issues a single
`DVDReadAsyncPrio(..., 2)` per file, since every archive is below the `0x80000`
chunk size. `HSD_DevComDVDMemCallback` runs `fn_801D4220` from the DVD interrupt,
and that clears `xC4_b1` (`grpstadium.c:1869-1881`). `xC4_b1` is set before the
request, so a completion between request and the `xDC = 1` store is safe.
`GrPs.usd` itself loads before `on_init` through the normal stage path
(`Ground_801C0754`, `gr/ground.c:453-466`, preloaded by `Ground_801C06B8`).

The destination is `lbDvd_GetPreloadedArchive(0x7D5)`, or `HSD_MemAlloc(0x50000)`
when that preload is absent (`grpstadium.c:313-317`). In retail
`Ground_801C06B8` → `grStadium_801D511C` reserves four preload-heap-4 buffers
during scene preload under the pseudo entry numbers `0x7D2`-`0x7D5`
(`grpstadium.c:2278-2292`; `gr/ground.c:433-451`; `lb/lbdvd.c:116-160`). Their
sizes are 80,640 (250×160 RGB565), 522,240 (640×406), 19,840 (124×80) and
`0x50000` for the archive. The largest archive, 327,617 bytes rounded to
327,648, fits the 327,680-byte buffer with 32 bytes to spare.

**Completion timing is the critical determinism variable.** On hardware and in
Dolphin, `L` depends on DVD timing. HPS music reads use DevCom priority 0
(`sysdolphin/baselib/synth.c:1195-1226`), and `HSD_DevComDVDWakeUp` scans queue 0
before queue 1, so the priority-1 read waits for the in-flight operation and is
then passed over by each newly queued music block. Changing `L` shifts the
whole later schedule and changes how the five-per-tick `HSD_Randf` draws of
cases 4-5 interleave with fighter, item and particle draws on the shared RNG.
`L` has not been measured. It is expected to exceed one tick on a real drive,
but that is **unmeasured**. Slippi records the phase boundary as stage event
`0x41` (frame, phase, type), where phase values 2-6 are exactly `xDC` after
entering that case and the type is `xDE` (`types.ts:307-330`; replay versions
that have the event only; see
[Slippi replay validation](../SLIPPI_REPLAY_VALIDATION.md)). A replay therefore
exposes the case-2 frame but not the trigger tick, which must be inferred from
the RNG stream.

What the port does today, and why it fails explicitly:

- `DVDFastOpen` is a `STOP` stub (`src/gameplay_platform.c:132`), so
  `lbFile_8001634C` stops. `lbFile_80016580` and `lbFile_800164A4` are **not**
  patched; only `lbFileGetSize` and `lbFile_8001668C` are
  (`patches/melee-gameplay.patch`, the `lbfile.c` hunk).
- `DVDConvertPathToEntrynum` already maps an exact RuntimeFiles name to a
  private entry (`src/gameplay_audio_stream.c:133-137`), and `HSD_DevComRequest`
  routes owned entries to `melee_web_source_file_request`
  (`src/gameplay_audio_stream.c:143-148`). That function accepts only type
  `0x21`, priority 1, null `args`, 32-byte-aligned offset and size, a range
  inside the file and a destination inside a registered writable region
  (`src/gameplay_source_files.c:483-519`).
- Wasm pointers stay below `0x80000000` (no `MAXIMUM_MEMORY` override exists, so
  Emscripten's 2 GiB default applies), so the unpatched
  `type = dst >= 0x80000000 ? 0x21 : 0x23` picks the ARAM relay type, which the
  service rejects.
- The existing **external** (streamed) source files are the wrong tool. They
  read disc ranges through `window.menuStartSourceRead` and the pump blocks with
  `emscripten_sleep(1)` for up to 30 s per request
  (`src/gameplay_source_files.c:590-601`; `web/melee-runtime.mjs:464-500`),
  which would stall source ticks and touch the disc mid-match. They are for
  authored THP movies.
- Source requests are pumped only by the menu host before each step
  (`src/gameplay_menu_host.c:178-185`: pump, `lbAudioAx_80027DF8`, step, clock
  tick) and by the patched `lb_800195D0` (`lb/lb_0195.c`). The browser reads
  files only in the preparation transaction while ticks and draws are stopped
  ([scene asset loading](../SCENE_ASSET_LOADING.md)).
- `melee_web_source_files_end` refuses to close while a request or writable
  region is live (`src/gameplay_source_files.c:252-262`), so a request pending
  at match exit needs a defined drain.

The service the port needs, which keeps disc reads off the running clock and the
source's own polling loop unchanged:

1. Import GrPs1-4.dat (1,121,132 bytes) as ordinary RuntimeFiles in the
   selected-match transaction while the clock is stopped, as explicit
   content-row archives. No browser disc read happens mid-match.
2. Complete the source request from those bytes through the existing pumped
   `melee_web_source_file_request` queue, with a **deterministic completion
   rule**: complete at `request_tick + L_rule`, never at host I/O completion.
   Today's pump-before-step order gives `L = 1`: the request is made in step N,
   the pump completes it at the start of tick N+1 outside the source frame, and
   the first case-1 poll succeeds. The value of `L_rule`, constant or recorded
   per transformation for original comparison, is decided by checkpoint 4. The
   rule must be identical for both peers in online lockstep.
3. Required service changes: answer the size query by entry (a
   `lbFile_8001634C` patch mirroring the `lbFileGetSize` hunk); classify
   main-memory destinations as `0x21` by registered region rather than numeric
   address; register the 0x50000 destination for its owner's lifetime (the
   allocation is in unpatched source, so this needs a hunk in
   `grStadium_801D13E0`); accept the source's `ROUND_UP_32` length (the request
   check rejects reads past the file end today; supply the 6-31 zero tail bytes,
   which match the disc); and record the destination-to-filename association on
   completion, because only synchronous copies feed
   `melee_web_source_file_take_name`, which the patched
   `lbArchive_InitializeDAT` requires (`lb/lbarchive.c` hunk).
4. Hydrate all four `map_head` graphs natively at preparation (ResolveNull
   externs) and register their archive sections, so the in-tick bind does no
   decoding. Seed the pipelines and textures for the four models at preparation:
   a live pipeline creation or first texture upload mid-match breaks the stage
   admission gates.
5. Give `src/gameplay_stage_map.c` a second slot. `MeleeWebStageMap` already has
   `archives[4]`, but only `archives[0]` is populated, `melee_web_stage_map_lookup`
   consults only slot 0 and `melee_web_stage_map_set_public` requires one
   archive (`src/gameplay_stage_map.c:10-45`). The patched
   `grDatFiles_801C6330` and `grDatFiles_GetArchive` route to it
   (`patches/melee-gameplay.patch`, `grdatfiles.c` hunk). Release the slot at the
   source `grAnime_801C65B0` and keep it alive until the outgoing transformation
   map has been destroyed (`Ground_801C4A08`, `gr/ground.c:2791-2826`). The
   flagged objects get `0x4000000` written at bind time, so those native
   descriptors must be writable.

Quirk to preserve: on returning to the default form, case 6 passes the raw
buffer to `grAnime_801C65B0` (`grpstadium.c:2224`). Read as `UnkArchiveStruct`,
its first words are the DAT header: file size, then `nb_reloc` at +8, which is
929/530/1,080/1,063. That is neither 0 nor 1, so the only effect is zeroing the
first 12 bytes of the buffer. Raw big-endian bytes read on the little-endian
host also avoid 0 and 1. The trace must show no free on this path.

## Transformations: collision, effects, camera and audio

`OnInit` removes collision joints 1, 2, 3, 5, 7 and 0, then relinks joints 6 and
4 (`mpLib_800581DC(6, 4)`). Map 2 removes wall lines 85 and 111. Map 5 re-adds
joint 4. The default form therefore has joints {4, 6} and both wall lines off.
Joint bindings come from two places: `grPs_803E1248` rows `{1,3}`, `{2,3}`,
`{3,4}`, `{4,5}`, `{5,6}`, `{7,9}` (`grpstadium.c:67-70`;
`Ground_801C2ED0`, `gr/ground.c:1642-1672`) and each entry's own GrJoint
rows. Entry 9 carries the one row `{0, -1, 7}`. Joint 6 has no GrJoint row (map 2's entry has none), so the base stays
static. Every bound map is moving collision; its lines follow the JObj, including the Y scaling of the sink and
rise, through `Ground_801C2FE0` and `mpLib_80055E9C`.

| Form | Map | Collision joints | Joint contents (lines) | Effects |
| --- | --- | --- | --- | --- |
| Default | 5 | 4 | three floors 34-36 | none |
| Base (permanent) | none | 6 | floors 51-54, ceiling 71-75, right wall 85-90, left wall 106-111 | none |
| Fire | 3 | 1, 2 | j1 floors 0-16, right wall 76-79, left wall 91-99; j2 floors 17-18 | 30015 on joints 0x13, 0x16, 0x17, 0x19; 30025 on 0x14, 0x15, 0x18 |
| Grass | 4 | 3 | floors 19-33 | none |
| Rock | 6 | 5 | floors 37-50, right wall 80-84, left wall 100-105 | 30022 at the origin |
| Water | 9 (+7, 8) | 7, plus dynamic 0 | j7 floors 55-70; j0 dynamic lines 112-135, vertices 0-23 | 30020 at the origin |

Water's joint 0 is bound through map 9's own row to the depth-7 JObj, which
rotates -0.5° per tick. It is added at case 6 (`xC4_b0`) and removed when the
sink begins (`xC4_b1`), and `mpLib_8005667C(0)` runs every tick
(`grpstadium.c:558-591`). Maps 7 and 8 only copy map 9's Y scale each tick.

[PR #121](https://github.com/ericvanlare/melee-web/pull/121) adoption needs a
change here. `collision_source_dynamic_ready` requires every authored dynamic
joint to be bound to a JObj at adoption (`bound->x20`,
`src/gameplay_collision.c:136-181`). Joint 0 stays unbound and removed until
water's map 9 exists, so adoption fails at stage start with "Source dynamic
collision joint is not bound to its authored stage JObj". The gate must accept
exactly that deferred state and validate the binding when the source binds it.

Other effects (all bank 30): 30000 is a crowd flash with probability 1/200 per
tick at `(±(100…299), -100, -660) × scale` (`grpstadium.c:633-647`), and 30024
is five generators per tick along the `lbl_803E1630` path during cases 4-5, so
about 1,510 generator creations per transition (`grpstadium.c:1919-2005`). The
sound requests are the two SFX at the case 3→4 boundary.

Camera services: `Camera_80030E44(1, NULL)` runs every tick of cases 4-5 and
queues a quake through `quake_model_set` (no RNG in the call). The screen's
`CmSubject` is created at **GO**, not at `OnInit`: `fn_801D11E4` is queued with
`Ground_801C10B8` and runs from `Ground_801C0FB8`. It places the subject at
`(0, 30) × scale` with extents ±25 by ±10 and is active only while a type image
shows (`grpstadium.c:230-247`; modes 2-6). Under StKind 240 the scheduler returns
early and keeps the subject active (`grpstadium.c:2044-2055`).

**Background Pokémon.** `grpstadium.c` creates no background Pokémon objects.
Its only objects are the maps above, the screen's three render objects and the
particle generators. Anything Pokémon-like in the crowd is authored model and
material animation inside maps 1 and 2, driven by `grAnime_801C8138`. The
tracker's "background Pokémon" therefore has no source owner beyond the shared
animation, material and particle services.

## Big screen (map 1) and rendering

`grStadium_801D2278` creates three render objects (`grpstadium.c:816-838,
1178-1303`):

- **Text camera** (`xD4`, class 0x11/0x13, `gxlink_prios = 2`): an orthographic
  CObj (`grPs_803E14FC`, `HSD_CObjSetOrtho(0, -160, 0, 250)`) renders SIS slot 1
  from `SIS_GrPStadiumData`, then copies 250×160 RGB565 from EFB origin (0, 0)
  **with clear**, every frame (`fn_801D2ED0`, via `lb_800122C8` →
  `HSD_ImageDescCopyFromEFB`, `sysdolphin/baselib/tobj.c:1575-1593`).
- **Stage feed** (`xD8`, GX link 3): copies 640×406 from **(0, 36)** without
  clear, once per frame while display mode 7 clears its flag.
- **Player zoom** (`xDC`, GX link 3): copies 124×80 from a per-frame origin
  derived from `lbVector_WorldToScreen` of the focused player and clamped to the
  main viewport in even pixels (`grStadium_801D32D0`, `grpstadium.c:1381-1434`).

All three descriptors come from `lb_800121FC` (`lb/lbspdisplay.c:286-306`),
which asserts a zeroed `image_ptr` and takes the preload buffer or else
`HSD_MemAlloc((size + 31) & ~31)`.

The screen's render callback `fn_801D5074` → `grStadium_801D1EF8` toggles eight
child JObjs from the `xE6` mask: mode 2 shows depth 6, 3 depth 3, 4 depth 4, 5
depth 9, 6 depth 7, and the other modes show depth 2. The type images are
therefore model JObjs, not copied textures. The callback **replaces
`tobj->imagedesc`** with one of the three runtime descriptors for the face
(depth 2). That TObj is the one found by identity with the public dummy
descriptor `0x89cc`, and the callback recompiles the MObj TEV with diffuse
colour `(150, 180, 160)` for the camera feeds (`grpstadium.c:657-814,
1331-1379`). The callback body is guarded by `xF8_0`, which every mode change
clears and each copy callback sets (`grpstadium.c:669, 1260, 1280, 1301`), so
the descriptor swap waits for a completed copy. Port ordering still has to show
that no draw samples an unwritten buffer.

Aurora's `copy_tex` handles an arbitrary source rectangle, clear and an RGB565
destination, keyed by the destination pointer, and it resolves in-pass at the
point of the call, scaled to the backing resolution (pinned Aurora
`lib/dolphin/gx/GXFrameBuffer.cpp:37-85`; textures bind through
`copyTextures.find(obj.data)`, `lib/gx/texture.cpp:454, 855`). Each copy ends the
current render pass and opens a new one (`resolve_pass_into`,
`lib/gfx/recording.cpp:791`). Fountain covers only the first pattern: an 80×60
origin copy with clear, `lb_800122C8(refl->image, 0, 0, 1)`
(`gr/grizumi.c:732, 790`). Fountain has browser entry but no pixel comparison.
New for Stadium:

- A near-full-frame copy every frame in mode 7 and a moving-origin copy in mode
  8, each splitting the render pass, on top of the text copy every frame.
- A source-allocated descriptor swapped into an archive TObj.
- A text pass on a separate camera.
- Copy points inside Melee's multi-camera frame. They are fixed by source
  `GObj_SetupGXLinkMax(…, 3)` order and `gxlink_prios`, which run natively, so
  only a pixel comparison can verify that Aurora resolves the intended pass
  contents.
- **Copy-texture lifetime.** Aurora keys resolved copies by destination pointer
  and provides `GXDestroyCopyTex(dest)` to evict one. Nothing in `src/` or the
  patches calls it. Stadium frees its three `HSD_MemAlloc` copy buffers at
  teardown, so a recycled address could bind a stale copy to an unrelated
  texture. The stage owner must evict before release.

The screen runs its own state machine (`xE4`, `grpstadium.c:840-1157`).

| Mode | Shows | Ends |
| --- | --- | --- |
| 0 | Blank | Never |
| 1 | Player names and timer | After 600 frames |
| 2-6 | Type image (2 default, 3 fire, 4 grass, 5 water, 6 rock) | After 600 frames |
| 7 | Stage feed | After `600 + HSD_Randi(600)` frames |
| 8 | Player zoom | After `600 + HSD_Randi(600)` frames, or when the player is lost, defeated or clamped |
| 9 | "Player N Defeated" | After 240 frames |
| 10-13 | Static strings 0x12-0x15 | Replaced by the next caller |
| 14 | Standings | After 300 frames |
| 15, 16, 17 | No caller in this build (15 has weight 0) | Unreachable |

When a timed mode ends, `grStadium_801D2A60` draws `HSD_Randf` until the pick
differs from the current and previous mode. Weights: mode 8 is 5, mode 7 is 2,
mode 1 is 2, mode 15 is 0. The counter `xF2` forces mode 14 with no draw once it
reaches 7, then restarts at 1, so only the first cycle takes seven random picks
and later cycles take six. The type-image modes also end through this picker.
`gm/gm_16AE.c` drives the other modes: 10 or 13 from the first status callback
(`fn_8016B7B4`: 10 when its argument is 3, else 13), 11 at the GO callback
(`fn_8016B7F8`), 1 when the HUD switches on (`fn_8016B784`), 12 at match end
when `fn_8016B88C`'s argument is 0, and 9
when a player runs out of stocks (`if/ifstatus.c:1024`). `Ground_801C1158`
also refreshes mode 1's text at match end (`gr/ground.c:738-745`).

Mode 1 composes its title into SIS string 5. The source swaps that slot's
storage, `HSD_SisLib_804D1124[1][2].textures`, which aliases `u8*` index 5, to a
static 256-byte scratch (`grPs_8049F040`, `grpstadium.c:1469`). It then copies
string 6, appends the type string (8-12) and appends string 7
(`sysdolphin/baselib/hsd_3A64.c:15-77`). The native SIS table must therefore be a
**writable**, per-match `u8*[22]`. The authored stream 5 is empty. Composed
lengths including the terminator are at most 112 bytes in GrPs.usd and 66 in
GrPs.dat, both under 256.

The screen state machine is gameplay state, not only visuals. Mode 8 ends when
the focused player's projected position is clamped against the main camera's
viewport, so the RNG stream depends on camera and viewport values, and the port
viewport must be the source's logical one, independent of canvas size. When
rules flag `x5_3` is set and mode 8 is focused on Jigglypuff, Sing gains
`HitElement_Sleep` (`ft/kinds/ftPurin/ftpurinspecialhi.c:31, 66-95`;
`grpstadium.c:2240-2254`).

## Data bounds

`yakumono_param` is the source struct at `grpstadium.c:41-65`: seven `int`s,
`u8 r, g, b`, one padding byte, ten `u32`s and five `s16`s. It consumes 0x52
bytes; the source ABI size is 0x54, including two trailing padding bytes. In
GrPs.usd the next referenced target begins at `+0x54`, and the relocation at
`ALDYakuAll + 4` points to it. The next public symbol is at `+0x70`, which
defines a public-symbol interval rather than object ownership. Thus bytes
`+0x52..+0x53` are ABI padding and `+0x54..+0x6f` is a separate referenced
region, not an opaque yakumono tail. The source struct contains no relocations.
The values are identical in all six archives:

| Field | Value | Use |
| --- | --- | --- |
| `x0`, `x4` | 3600, 3800 | Default countdown |
| `x8`, `xC` | 1200, 1800 | Transformed countdown |
| `x10` | 300 | Case-3 hold |
| `x14` | 120 | Sink/rise rate and rise length |
| `x18` | 60 | Hold at minimum scale |
| `r, g, b` | 150, 180, 160 | Feed diffuse colour |
| `x20` | 600 | Modes 1, 15 and 16 |
| `x24` | 240 | Mode 9 |
| `x28` | 600 | Modes 2-6 |
| `x2C` | 300 | Mode 14 |
| `x30`, `x34` | 600, 1200 | Mode 8 |
| `x38`, `x3C` | 600, 1200 | Mode 7 |
| `x40`, `x44` | 600, 800 | Unused by the source |
| `x48`, `x4A`, `x4C`, `x4E` | 5, 2, 2, 0 | Weights for modes 8, 1, 7, 15 |
| `x50` | 7 | Picks before standings |

`map_head`'s light-override count word is `0x30`, but only 24 eight-byte rows
(`0x290-0x350`) precede the flagged table in every archive. All lights the source
queries match within those 24 rows: map 2's lights and `map_plit`. Keep the
existing bounded reader (`src/dat_lights.cpp:24-43`; `src/dat_stage.cpp:50-53`),
which fails explicitly before crossing the region. Do not reinterpret the
count.

What exists and what is new in `src/`:

- Existing: `DatArchive` ResolveNull externs (`src/dat_archive.cpp:46-176`);
  ten-entry `map_head` tables; joint, material and particle animation;
  lights; `quake_model_set` and particle banks; GrJoint rows and moving
  collision (Fountain, Yoshi's Story); bounded light-override identity lookup;
  `ALDYakuAll` (`DatStageYaku`); marker decode; and localized `.usd`
  resolution.
- New, in `DatNativeStage` (`src/dat_native_stage.cpp`): it currently requires
  every entry to own a joint (`:224`, "Native stage model missing") and every
  flagged-table slot to name a hydrated material descriptor (`:290`). In
  GrPs.usd entries 3, 4, 6, 7, 8 and 9 are extern slots, and flagged slots
  such as `GrdPStadiumField_Stage_Shadowmat1_mobjdesc` are externs too. The
  decoder needs an explicit, profile-declared per-entry owner (and skip) rule
  for resolved-null entries. A blanket relaxation would hide real missing
  models.
- New: the Stadium `yakumono` decoder; a SIS kind in `MeleeWebStagePublicKind`
  beside JOINT and IMAGE (the public catalog otherwise leaves
  `SIS_GrPStadiumData` unhydrated); four extra archives with the extern-dense
  `map_head`; and the writable SIS table.

Profile row for `src/gameplay_stage_profile.c`: `{St_Kind_PStadium,
Gr_Kind_PStadium, &grPs_StageData, required {0,1,2,5}, exchange wrapper,
decode_yakumono = Stadium decoder, yakumono_program_count = 0, entry_count = 10,
animation_counts = ten ones, allow_absent_particle_bank = 0, opaque_yakumono =
0, public_symbols = {dummy image (IMAGE), SIS_GrPStadiumData (new SIS kind)}}`.

## RNG draws (shared HSD seed)

`HSD_Randi(n)` is `n * HSD_Rand() / 65536` over the LCG `seed * 214013 +
2531011` (`sysdolphin/baselib/random.c`). Every draw below uses the one shared
seed. The `randi` helpers skip the draw when a range is zero
(`grpstadium.c:293-300`).

| When | Draws |
| --- | --- |
| Ordinary VS start, stage music (`Stage_80225074` with `r31 = 4` → `Ground_801C24F8`) | StageParam `x14 = 6`: if all unlockable characters are unlocked (`gm_80164ABC`), one `HSD_Randi(100)` and `12 > draw` selects `pokesta.hps` (`gr/ground.c:1342-1485`, `gr/stage.c:295-330`). No draw on a save that is not fully unlocked. |
| `OnInit`, map 2 | `HSD_Randi(200)` for the first countdown (`grpstadium.c:327-334`) |
| Every tick of map 1's process, not gated by `xC4_b0` | `HSD_Randi(200)`. On 0, also `HSD_Randi(2)` and `HSD_Randi(200)` (crowd flash) |
| Screen mode change | One or more `HSD_Randf` unless forced to mode 14. Entering mode 7 or 8 adds `HSD_Randi(600)`. Mode 8 also scans players without a draw |
| Leaving the default form | `HSD_Randi(4)` |
| Cases 4-5 | 5 × `HSD_Randf` per tick for about 302 ticks (about 1,510 draws per transition, **derived**) |
| Case 6 | `HSD_Randi(200)` into the default form, `HSD_Randi(600)` into a transformation |
| Particle systems | `particle.c`, `generator.c` and `bytecode.c` all draw `HSD_Randf`/`HSD_Randi` per spawn and update. Effects 30000, 30015/30025, 30020, 30022 and the 1,510 sparkle generators therefore consume the same stream, in a count set by the authored particle programs |
| Items on (`Ground_801C0A70`) | Random item position: `HSD_Randi(2)`, then `HSD_Randi(4)` and `HSD_Randi(100)` when a fighter is present. Stadium is in that function's stage list. Competitive rules turn items off |

The scheduler's own draws can be predicted by this table, but the particle
draws cannot be closed by hand. The checkpoint-3 and 4 ledger must attribute
draws by owner from a per-tick seed trace and compare it with the original,
rather than assert a total. Fighter, item and particle draws interleave with
these in source process order, which runs natively.

## Port status and gaps

- The SSS gate is the content row. `melee_web_menu_stage_available` returns true
  when `melee_web_stage_content(3)` exists (`src/gameplay_menu.c:527-529`), and
  `stage_selection_valid` rejects everything else (`:548-561`). The profile
  lookup also requires the row. A row therefore exposes the stage tile, and
  until checkpoint 4 passes a match would stop explicitly at the first
  transformation trigger, 3,602 to 3,801 ticks after GO. The lead decides
  whether the row follows the Fountain and Temple development-candidate practice
  or needs a separate development gate.
- Wiring to add:
  - Content row `{St_Kind_PStadium, Gr_Kind_PStadium, "Pokémon Stadium",
    "GrPs.usd", "pstadium.hps", 64, "pstadium.ssm"}` in
    `src/gameplay_content.h`, **and** the second stage array inside
    `melee_web_stage_content_by_ground`, which is hand-listed. The transformation
    archives need a new content field or profile list, consumed at
    `src/gameplay_asset_manifest.cpp:299-302`, which today adds one archive per
    stage.
  - Profile (above) and the yakumono exchange hunk
    `melee_web_grpstadium_exchange_yakumono` (lead).
  - `kStageMusicWords` entry `{64, 63, 64, 63}` and `music_file` cases 63 and 64
    (`src/gameplay_asset_manifest.cpp:160-198`).
  - `GrPs.usd`, `GrPs1-4.dat`, `pstadium.hps` and `pokesta.hps` in
    `NATIVE_GAME_DISC_FILES` (`web/runtime-assets.mjs`). `pstadium.ssm` is
    already mapped.
- The port has no source preload cache, so the four retail heap-4 reservations
  fall back to main-heap `HSD_MemAlloc`. By GX 4×4 tiling that is 327,680 +
  80,640 + 522,240 + 19,840 = 950,400 bytes (**derived**; the three screen
  buffers are 622,720 of it). Fountain's 80×60 buffer already does this. At
  Stadium's size it changes the mirrored source main-heap trace and headroom.
  The lead decides between owned preload reservations and a documented
  deviation.
- Training (`gm_8018841C`) disables the scheduler and asserts on modes 2-6.
  StKind 240 freezes the stage. Frozen Stadium is a modification. All three are
  outside competitive VS.

## Checkpoint plan

Sizes: **S** is about a day, **M** is two to five days and **L** is one to two
weeks of agent work. Total is roughly 8-10 serial weeks (**estimate**), less if
checkpoints 4 and 6 run beside the others. Each checkpoint retains its failures
and stops after two experiments at one boundary, then adds a smaller reproducer
or requests a bounded review. No checkpoint here claims retail equivalence until
checkpoint 8.

| # | Outcome | Pass criteria | Exclusions | Size |
| --- | --- | --- | --- | --- |
| 0 | Real-data decode trace over the five archives and three audio files | Every hash, table, count, marker (including the missing `0x94`), extern location, padding tail, SIS length and light-row count in these notes is reproduced, twice, by a checked-in test | Any runtime | S |
| 1 | Content row, manifest and SSS enablement | The original CSS → SSS, driven by raw PAD input, selects the Stadium tile and publishes `stkind = 3`; preparation imports exactly the manifest (stage, transformation, music and bank files) and no other stage's data; every later boundary still fails explicitly. The source asks for `GrPs.usd`. The lead records the development-gate decision | Match start, transformations | S-M |
| 2 | Static default map and collision | Maps {0,1,2,5} in order, joints {4,6}, wall lines 85/111 off, dummy CamRange, blast box, marker positions, lights, scale, collision queries on both joints, joint 0 accepted unbound and removed, immutable bytes, zero live objects after teardown, twice. Entry-owner decoder rule landed | Scheduler beyond tick 0, transformations, pixels | M |
| 3 | Idle lifetime and screen state machine (native, no draw) | Two lifetimes of at least 3,500 post-GO ticks (below the first trigger): countdown arithmetic, the GO gate, crowd-flash draws, mode sequence, timers, the scratch title and the SIS slot swap follow source arithmetic through Ready, GO, HUD-on, a stock-out and the standings pick; a per-tick RNG ledger by owner | Copies, pixels, the first trigger | M |
| 4 | Original latency measurement (can start after checkpoint 0) | Distribution of case-1 ticks `L` and per-phase ticks per type from repeated Dolphin captures with recorded disc-speed settings, plus Slippi `0x41` events where available. Stop if `L` is not stable under fixed inputs and escalate the rule choice | Port changes | S-M |
| 5 | Transformations: mid-match file service and second slot | Seeded lifetimes reach fire, grass, water and rock; one lifetime repeats a cycle. Per-phase ticks under the checkpoint-4 rule; map create/destroy; joint and line sets; effect and SFX requests; slot reuse; the case-6 quirk without a free; region drained; zero leaks after teardown, including a pending case-1 request. Fire, grass and rock first, so water is separable | Water collision, pixels, browser | L |
| 6 | Water dynamic collision (PR #121 gate change) | Joint 0 binds at map 9, rotates -0.5° per tick, is added and removed at the source points, answers queries on the rotated lines, tears down unbound, and the gate still rejects a genuinely unbound joint | Other forms | M |
| 7 | Screen and background rendering in the browser | Text, feed and zoom copies, descriptor swap, TEV colour and copy-texture eviction; pixel checkpoints against Dolphin per screen mode and per form; pipeline seed captured across all four forms; memory receipt; no live pipeline creation | Retail state | L |
| 8 | Browser match, sweep and original comparison | Original SSS raw-PAD selection, cold and warm, through at least one complete cycle (about 4,300+ post-GO ticks). All forms reached by long runs or recorded-start contexts. Every admitted fighter's action sweep, KO, pause, exit and a second match, with the ADDING_STAGES timing gates. Then per-tick fighter state, RNG, `xDC`/`xDE`, map set and collision set against Dolphin across at least one transformation, plus Slippi `0x41` frames, and a full match to Results | Physical input, PCM, hardware DVD variance | L |

## Top risks

1. **Load latency.** `L` decides the RNG interleaving of every later tick, and
   it is unmeasured. A host-timed completion would also break online lockstep.
   Recommend a rule-defined completion tick, with `L` recorded per
   transformation when comparing against a capture.
2. **Particle RNG.** The stage's effects draw from the shared seed through the
   source particle system, so determinism depends on the authored particle
   programs and bank 30 being exact, not only on `grpstadium.c` arithmetic.
3. **Archive slot lifetime.** The second native slot, its release order, the
   writable flagged objects and the entry-owner decoder rule are new ownership
   in a single-archive registry.
4. **Collision.** The PR #121 gate must accept deferred binding, and moving
   collision must follow scaled JObjs.
5. **Render-to-texture.** Three copy patterns split the pass every frame, copy
   points are fixed by source GX link order, and copied textures are keyed by
   pointer with no eviction on free.
6. **Preparation and memory.** Pipelines and textures for four mid-match archives
   must be prepared, and the import (about 1.1 MB) and fallback heap use (about
   0.95 MB) need receipts.
7. **Early exposure.** A content row makes the tile selectable before the
   scheduler can complete a transformation.

## Not verified here

The value and stability of `L`; the SFX-to-sample mapping through `smash2.sem`;
the exact tick counts of cases 4-5 under float32; which authored string each
screen mode shows; pause behaviour of the stage processes; the port's logical
viewport values against retail; and every Aurora pixel outcome. Each belongs to
the checkpoint named above.
