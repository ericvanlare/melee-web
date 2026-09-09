# Fox source-port notes

These notes record the revision 2 source and asset contract used to admit Fox.
They complement `ADDING_CHARACTERS.md`; extracted files remain local and are
never repository inputs.

## Source identity

| Role | Revision 2 identity |
| --- | --- |
| Character/Fighter kinds | `CKIND_FOX` 2, `FTKIND_FOX` 1 |
| Fighter data | `PlFx.dat`, public root `ftDataFox`, 327 action rows |
| Animation container | `PlFxAJ.dat` |
| Costumes | `PlFxNr.dat`, `PlFxOr.dat`, `PlFxLa.dat`, `PlFxGr.dat` |
| Model roots | `PlyFox5K_Share_joint`, `PlyFox5KOr_Share_joint`, `PlyFox5KLa_Share_joint`, `PlyFox5KGr_Share_joint` |
| Material roots | The matching model names with `_matanim_joint` in place of `_joint` |
| Effects | `EfFxData.dat`, `effFoxDataTable`, bank 3, six entries |
| Audio | `audio/us/fox.ssm`, `ssm_files` slot 11 |
| Fighter dynamics | Four authored chain-parameter rows on bone 17; one auxiliary row on bone 41 |

Fox and Falco execute the same original `ftFx` special-move family and decode
the same 0xd4-byte `ftFox_DatAttrs` layout. They do not share attribute bytes or
article identities. Fox's `PlFx.dat` stores laser item kind `0x36` and blaster
item kind `0x4a`; Falco stores `0x37` and `0x4b` respectively.

Fox's `ftData.x48_items` has nonnull roots in slots 0, 1 and 2. The original
`ftFx_Init_OnLoad` sets wall-jump ability, copies Fox's own extension into the
Fighter, and registers those roots as Fox laser, Fox blaster and
`It_Kind_Fox_Illusion` (`0x38`). Slot 3 is null. Falco instead invokes
`ftFx_Init_OnLoadForFalco` and registers Phantasm from slot 3.

## Part-animation ownership

Fox has five source part-animation groups. Each group owns its own part list
and FigaTree variants:

| Group | Start | Parts | Part indices | Variants |
| --- | ---: | ---: | --- | ---: |
| 0 | 27 | 12 | 27 through 38 | 4 |
| 1 | 57 | 13 | 57 through 69 | 4 |
| 2 | 41 | 1 | 51 | 3 |
| 3 | 41 | 4 | 42 through 45 | 4 |
| 4 | 41 | 4 | 46 through 49 | 4 |

The existing `GameplayFighterAssets` owner hydrates these groups from the
selected fighter DAT. It does not reuse Falco's part table.

## Fox-only dynamics

Fox is the first admitted fighter in this port whose `ftData.x2C` is nonempty.
The source header declares one dynamic bone descriptor on bone 17. Its
`DynamicsDesc` owns four 0x3c-byte authored parameter rows and position
`(1, 1, 0.0436332)`. The auxiliary count is one and its `ftData_x38` row is
bone 41, offset `(0, 2, 0)`, scalar `3`. The optional dynamics animation table
is null.

This differs from Falco even though both use the Fox special-move family.
Before the Fox audit, `gameplay_fighter_data_decode` rejected every nonempty
dynamics header. Fox therefore required a real native hydration path for these
owned descriptors. The runtime allocation performed later by
`ftCo_8009CF84` remains per Fighter; the DAT owner retains only the immutable
authored parameter rows that `lb_80011710` copies into that allocation.

## Action command audit

The checked action store admits Fox rows 295 through 326, covering blaster,
Fox Illusion, Fire Fox and reflector scripts. Across those rows the source
graphs use opcodes `0, 1, 2, 3, 4, 7, 8, 10, 11, 16, 17, 19, 20, 25, 26, 35,
37, 38, 43, 46, 52, 55`. The current checked decoder constructs all 32 graphs
from the owned `PlFx.dat` and `PlFxAJ.dat`; no new fighter-command schema was
required after Falco. Fox Illusion's separate item animation scripts did expose
two missing one-word item commands: opcode 12 changes an enabled hitbox's damage,
and opcode 14 clears one hitbox. Both now use the original checked bitfield
layouts.

The integrated source match trace now executes laser, reflector, Illusion and
Fire Fox with source item/effect/audio services live across all four costumes,
and repeats teardown. A Fox-versus-Falco case keeps both fighters resident and
verifies shared effect-bank ownership. A Release browser run selected Fox in the
original CSS and rendered Fox on Yoshi's Story with the correct stock icons.
Uninterrupted browser audio inspection remains open.

## Owned revision 2 asset hashes

```text
846b075d3379de041ecfb5504f5442484c23783422a0851851c96489d697b690  PlFx.dat
a9f3865a0c085b321876543900625d7d54dfef5fe2438ea8068f6221073a6c7d  PlFxAJ.dat
f1ebe2af74d34be113614da3204730ea6d09b078ebf8a82d2a9036d58d8e4f8b  PlFxNr.dat
3d233ecd659a8fa4462baf6795a37f8ec29a0db87da653b2cf2d68c086f269e2  PlFxOr.dat
4fc636ad964a8011b3cec119adfdbf92128f9ced8034146c6293415a45925b19  PlFxLa.dat
b8a41b28a72c5ce2adc0705075662f73763553838356557ecd302e40c066e6d4  PlFxGr.dat
6029fc0740155310322c4313c29cfed0d2acc3a6cb90c8a4945b9cb605f7570e  EfFxData.dat
cb012129694c3317e6aaa2fa5b2d8a050e505cca4a0af25198696f710aad2e4d  fox.ssm
```

Extract missing local files with:

```sh
OUT=assets-local/next-gate
for file in PlFx.dat PlFxAJ.dat PlFxNr.dat PlFxOr.dat PlFxLa.dat PlFxGr.dat EfFxData.dat; do
  python3 scripts/extract_disc_file.py /path/to/owned.ciso "$file" --output "$OUT/$file"
done
python3 scripts/extract_disc_file.py /path/to/owned.ciso audio/us/fox.ssm --output "$OUT/fox.ssm"
```

Run the focused portable metadata/model test and Wasm32 command test with:

```sh
python3 -m unittest tests.test_fox_real_assets tests.test_fox_gameplay_action -v
```

## Shared integration record

The shared integration completed these steps:

1. Add `{ CKIND_FOX, FTKIND_FOX, 4, "Fox", "EfFxData.dat",
   "effFoxDataTable", 3, 6, "fox.ssm" }` to `gameplay_content.h`, and add
   `CKIND_FOX` to its FighterKind lookup list.
2. Add `PlFx.dat`, `PlFxAJ.dat`, the four costume files and `fox.ssm` to the
   browser disc manifest and native menu key list. `EfFxData.dat` is already
   present for Falco.
3. Add `fox.ssm` to menu-world audio residency/name lists. The match session
   already selects unique fighter banks from the content row.
4. Change the player-context Fox rejection check into a successful
   `FTKIND_FOX` to `CKIND_FOX` mapping check. The source player map already
   contains this pair, and Fox's stock-icon base is 2.
5. Exercise Fox construction, OnLoad article registration, all four costumes,
   laser, reflector, Illusion, Fire Fox, damage/stocks, teardown and a second
   construction on every admitted stage. Keep Fox and Falco simultaneously
   resident in at least one trace to verify shared effect-bank deduplication.
6. Select and render Fox through the original CSS/SSS route before describing
   the character as browser-validated. The Release run completed this check with
   Fox versus Mario on Yoshi's Story.

`gameplay_world.cpp` and `gameplay_match_session.cpp` already derive fighter
DAT, costume, effect and audio ownership from the registry and content row.
`cmake/FighterRuntime.cmake` already compiles the original Fox sources as part
of the full source closure. They need no Fox-specific branches unless the
integration trace exposes a source service that the focused tests cannot
reach.
