# Peach development checkpoint

Peach is integrated as a development candidate on
`codex/enable-ness-peach-yl`, with current `origin/main` (`fc1184a`) and the
Ness commits merged locally before validation. This checkpoint
preserves the original `CKIND_PEACH` (0x0C) / `FTKIND_PEACH` (9) identity, five
costumes and authored source callbacks. It is **not** independent original
equivalence, complete fighter acceptance or a deployment change. The source
contract was prepared in `work/PeachPrep/` (CONTRACT.md, decode_peach.py,
decode_out.txt) and re-verified during implementation.

## Checkpoints

| Checkpoint | Status | Evidence |
| --- | --- | --- |
| 2. Source contract | Passed | `work/PeachPrep/CONTRACT.md` (byte-level, parser-backed); re-verified during implementation |
| 3. Real construction | **Passed** | `work/pr61-native-v2/peach-p1-mario-p2.log` and `peach-p2-mario-p1.log`: Release `gameplay_content_match_trace`, stage kind 32, both orientations exit 0 after the decoder ownership/stack repair — data/article/effect-bank construction, ground+aerial Toad counter and vegetable lifetimes, Bomber and Parasol states with the Parasol article lifetime, the authored Float state, all five costumes, combat, pause and repeat teardown ("Mixed source content intro, costumes, stage lifecycle, combat, pause and repeat teardown passed") |
| 4. First advancing browser frames | **Partial** | Headless installed Chrome reached original CSS → SSS → Final Destination match after a source-grounded idle settle, then observed Peach Toad counter, vegetable pull, Bomber and Parasol motions. The broad action sweep remains outside this bounded receipt, so no complete move or original-equivalence gate is claimed. |
| Results lifecycle | **Passed** | `docs/evidence/results-pr61-ness-peach-v1.json`: authored Peach Results clips plus 38 enabled-roster constructions/confirmations and teardown checks. |
| Integration (build + suite) | **Passed for this batch** | Release runtime and Results targets rebuilt after the decoder repair; focused fighter-asset, Results, manifest and launch tests pass (one stage-asset test is skipped when its local asset is absent); runtime disc checks pass. |
| Comparison / pixels / PCM / performance | Not run | Separate gates, untouched |

## Source data and shared boundaries

`PlPe.dat` supplies `ftDataPeach` (base attrs 0x184, extension exactly 0xC0 =
`sizeof(ftPe_DatAttrs)`), 318 action rows and five costume owners
`PlPe{Nr,Ye,Wh,Bu,Gr}.dat` (symbols `PlyPeach5K[_Ye/_Wh/_Bu/_Gr]_Share_joint`).
Authored facts that required Peach-specific decoder work, each bound from the
decoded DAT and cited consumers:

- **0xC0 `ftPe_DatAttrs` extension.** The pinned header annotates
  `speciallw_item_table` at +0x1C, but that placement cannot hold three
  8-byte entries before the +0x30 member, the real C layout is +0x18, and
  both the decoded pairs (odds 2/3/1, kinds 0x06 Bob-omb, 0x07 Mr. Saturn,
  0x0C Beam Sword) and the `pickVeg` consumer (ftpeachspeciallw.c:41-57)
  land at +0x18. `x14` (128) is the `getVeg` special-turnip threshold;
  `xAC` is the original `AbsorbDesc` (bone 3, offset (0,1,3.5), size 6).
  `floatfallf/b_anim_start` are authored zero and refilled at load from
  motions 18/19 by `ftPe_Init_OnLoad`.
- **Dynamics `dynamicsNum=9`.** Nine body chains (bones 19/31/61/49/37/43/55/
  25/88), 86 authored mode rows derived from the blend selectors, and a new
  mode-schema arm beside the sword/cape and Donkey arms. Mode[0] bones 2-7
  author cutoff 0x100: `ftdynamics.c` compares the value as an int against the
  chain index, so 0x100 selects the whole chain (it is also
  `ftCo_8009CB40`'s own sentinel). Marth/Roy/Ganondorf/Donkey never author it,
  which is why the previous bound never fired.
- **Empty auxiliary dynamics.** Peach's `x4` auxiliary count is zero while the
  +0xC pointer is nonetheless relocated (it points just past her bone table).
  The original ignores the pointer when the count is zero, so the empty-aux
  gate tolerates that shape instead of inventing an auxiliary row.
- **Five Article slots.** `ftPe_Init_OnLoad` binds x48 slots 0-4 in order:
  Explode (0x28), Turnip (0x29), Parasol (0x2D), Toad (0x2E), ToadSpore (0x35).
  The authored 20-byte table and `item_table_bytes=20` are read exactly; the
  seven-slot shared accessor capacity still covers Peach's five.

## Articles (five slots, extents from the authored DAT regions)

| Slot | Kind | Special record | State rows | Model |
| --- | --- | --- | --- | --- |
| 0 | Explode | none | 2 command-only | null-joint ItemModelDesc |
| 1 | Turnip | 0x48 (lifetime 140.0, count 8, eight {odds,damage} pairs) | 2 animated + 1 command-only | joints @0x17948 |
| 2 | Parasol | 0x4 (unread scalar) | 2 animated | joints @0x1e2a8 |
| 3 | Toad | 0x4 (unread scalar) | 2 animated | joints @0x20b48 |
| 4 | ToadSpore | 0x10 (`itPeachToadSporeAttributes`: 10, 6, 0.7, 1.74533) | 1 command-only | null-joint ItemModelDesc |

Every command stream decodes inside the repaired item command interpreter
(opcodes 0-19 only); no Peach script required opcodes beyond that set.

## Effects and audio

`EfPeData.dat` / `effPeachDataTable`, bank 15
(`ftData_UnkBytePerCharacter[9]`), model-only (both particle roots null, the
Link precedent) with exactly one authored 20-byte entry (lifetime 10.0, one
joint and one anim). The original `efSync` dispatch reaches bank 15 entry 0
(gfx id 15000) from the vegetable pull; the float sparkle (1236 → generator
286) and Toad spores (1235 → generators 370/371) resolve inside already-parsed
banks. Audio uses `audio/us/peach.ssm` (`ssm_files` slot 22).

## Action store

Peach's branch admits rows 295-317 (Float 295, float aerials 296/297, the
three AttackS4 weapons 298-300, SpecialN 301, SpecialS 302-307, SpecialHi
308-311, SpecialLw 312-315, ItemParasolOpen/Fall 316/317); the shared
`group(295,326)` fall-through would overrun her 318-row table. Her victim-side
rows 259-261 and 266-285 are empty motions exactly like Ness's and Marth's;
the shared victim groups 267-275 / 278-283 are admitted and her grab/throw
behavior runs on the already-admitted common rows (Catch family 242-246,
throws 96-103, light pickup 78-88).

## Shared repairs surfaced by Peach

- **Eye-telemetry base palette ownership** (`gameplay_match_context.c`): a
  playing TIMG animation repoints the runtime eye TObj's `imagedesc` into the
  borrowed animation table, so a base descriptor can no longer be found by
  imagedesc identity. Peach's indexed eye textures (base palette, no palette
  animation) are the first exercise of that path. The telemetry now proves
  palette ownership by matching the runtime TLUT against the retained costume
  descriptor with the same texture id and identical authored LUT storage —
  `HSD_TObjLoadDesc`/`HSD_TlutLoadDesc` copy the descriptor's data pointer, so
  that identity is the authored ownership fact.
- **Fighter command opcode 42** (`gameplay_action_store.c`): the parasol item
  animation-rate command. `ftAction_80072894` feeds the decoded {index,
  divisor} pair into `ftCommon_8007E83C`, which scales the held parasol item's
  animation rate; the consumer and the parasol item runtime are
  source-compiled, and the native `unk9` decode already matches the source
  bitfield layout. Peach's authored ItemParasol rows execute it.

- **Shared item-command flow** (`dat_item_commands.{hpp,c}`): opcode 16 now
  follows the source's two-versus-three-word sub-opcode lengths, and opcodes
  5/6/7 preserve authored entry roots while validating the source three-slot
  return/loop stack. Backward targets, cache hits, unmatched returns,
  recursive/deep calls and active-loop nested-call overflow are covered by the
  decoder regression.

## Honest scope

Development candidacy only. The trace exercises the specials above; the Toad
spore counters (which need an opponent hit on the counter), float-aerial
attacks, the common-Parasol item scope (rows 134-136 aliases) and a complete
move/costume/stage matrix are not evidenced. Independent original comparison,
pixels, PCM, live scheduling and performance admission remain open. The
browser CSS/SSS/match route and the four targeted Peach action recipes are
covered by `docs/evidence/results-pr61-ness-peach-v1.json`; the broad action
sweep and original-equivalence gate remain open.
