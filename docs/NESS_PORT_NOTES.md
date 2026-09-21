# Ness development checkpoint

Ness is integrated as a development candidate on
`codex/enable-ness-peach-yl` (base `e8c756a`, ahead of the PR #52/#53 merges;
the branch deliberately excludes the Mewtwo integration on `origin/main`). This
checkpoint preserves the original `CKIND_NESS` (0x0B) / `FTKIND_NESS` (8)
identity, four costumes and authored source callbacks. It is **not**
independent original equivalence, complete fighter acceptance or a deployment
change.

## Checkpoints

| Checkpoint | Status | Evidence |
| --- | --- | --- |
| 2. Source contract | Passed | `work/NessPrep/ness_source_contract.md` (byte-level, parser-backed); re-verified during implementation |
| 3. Real construction | **Passed** | `gameplay_content_match_trace` Release, Battlefield Ness(Mario) 32/11/8: exit 0 — both orientations' data/article/effect-bank construction, PK Flash ground+aerial charge/fire lifetimes, PK Fire ground+air lifetimes, Yo-Yo smash, all four costumes, combat, pause and repeat teardown ("Mixed source content intro, costumes, stage lifecycle, combat, pause and repeat teardown passed") |
| 4. First advancing browser frames | Not run | Browser-side route gate still open |
| Integration (build + suite) | Partial | Release and RelWithDebInfo trace targets rebuilt from the repaired decoder; focused real-asset attribute test passes; full suite result recorded in the commit message |
| Comparison / pixels / PCM / performance | Not run | Separate gates, untouched |

## Source data and shared boundaries

`PlNs.dat` supplies `ftDataNess` (base attrs exactly 0x184, extension exactly
0xDC = `sizeof(ftNessAttributes)`), 326 action rows, blend table 652 bytes
with all-zero dynamics selectors, `dynamicsNum=0` with a null modes table (the
existing gate already admits this without any Ness-specific branch), a null
Wait table (root+0x24, semantically empty — `wait_choices()` empty, no
invented rows) and a nonnull two-choice Squat Wait table `(31,50),(32,50)`.
Integer PK Flash / PK Thunder / PSI Magnet loop counters and gravity delays
keep their source categories; the x98 `AbsorbDesc` (bone 1, offset (0,6.5,0),
size 8.5) and xB8 `ReflectDesc` (bone 4, max damage 50, size 6.5, damage mul
1.5) are checked against the 140-part bound at decode and at part-index
validation. `PlNsAJ.dat` is the animation container. Costumes are
`PlNs{Nr,Ye,Bu,Gr}.dat` with the `PlyNess5K[_Ye/_Bu/_Gr]_Share_joint` /
`_matanim_joint` symbols (no Bk/Re/Wh archives). The authored costume texture
map count is 2, so the original two-eye telemetry contract is untouched.

## Articles (eleven slots — first variable-slot fighter)

`ftNs_Init_OnLoad` consumes x48 slots 0-10, all nonnull in the authored
44-byte table: PK Fire, PK Fire pillar, PK Flash, PK Thunder, four Thunder
trails (sharing one single-float special record = 100.0), PK Flash explosion,
baseball bat (one scalar) and Yo-Yo. The portable decoder now allocates the
authored slot count (11 for Ness, 7 elsewhere), `melee_web_fighter_data_article`
admits indices 0-10 only for kind 8, and the asset owner registers all eleven
`It_Kind_Ness_*` articles. The Link slot-6 joint special case is now gated to
the Link family. Authored special-attribute extents (from the DAT referenced
regions, matching the original consumers in `itPKFlash.h`/`itPKThunder.h`/
`itYoyo.h`/`itCommonItems.h`): PKFire 0x8, Flame 0xC, Flush 0x2C (3 state
rows), Thunder 0x14, trails 0x4, Explode 0x14, Bat 0x4, Yoyo 0x5C (no state
table). The Yo-Yo record ends with three relocated descriptors (string joint,
yoyo joint, matanim) hydrated like the Link boomerang specials; the material
animation binds the yoyo joint graph per `it_802BE65C`/`it_802BE5D8`.

## Shared item-command and texture relaxations (source-cited)

- `DatItemCommands` now admits item opcode 16 (`it_8027978C`): a one-word
  command whose embedded sub-opcode (bits 25..18) dispatches through the
  original item interpreter; the checked conversion preserves the whole word
  verbatim, exactly like the ops 14/17/18/19 precedent.
- The item script walk now recomputes its conservative referenced-region
  bound at each referenced boundary instead of rejecting: Ness's PK Thunder
  ball script runs into an op16+END tail that another authored pointer
  references as its own script start, so the fixed first bound fell mid-script.
  Every consumed word is still validated as authored, non-relocated script
  data, bounded by opcode 0 and the 1024-step cap.
- `dat_texture.cpp` no longer clamps the authored TObj `blending` float to
  [0,1]: the original loads it verbatim (`HSD_TObjMakeDesc`) and consumes it
  as an unclamped f32 TEV constant (`HSD_TExpCnst`, TEX_COLORMAP_BLEND path).
  Only nonfinite bits are rejected. No previously admitted fighter authored an
  out-of-range value, so existing behavior is unchanged.

## Effects and audio

`EfNsData.dat` / `effNessDataTable`, bank 10 (`ftData_UnkBytePerCharacter[8]`),
model-only (both particle roots null, the Link precedent). The authored
referenced region (0x60 bytes) admits exactly four 20-byte entries: rows 0-2
populated, row 3 a complete null row (the accepted Pikachu-row-7005 pattern).
The original `efSync` dispatch reaches bank 10 entries 0-2 only (gfx ids
10000-10002: 0x2710/0x2711 from PK Thunder `ftnessspecialhi.c:679,780,849,989`,
0x2712 from PSI Magnet `ftnessspeciallw.c:100,134,734,769`); no source
consumer spawns entry 3, and a fifth row would cross the referenced region.
Audio uses `audio/us/ness.ssm` (`ssm_files` slot 21).

## Action store and the empty-victim-row trap

Ness's branch admits rows 295-325 (Yo-Yo smash 295-298, SpecialN 299-306,
SpecialS 307-308, SpecialHi 309-316, SpecialLw 318-325); the shared
`group(295,326)` fall-through would overrun his 326-row table. Trap evidence:
Ness's own victim-side rows 259-261 and 266-285 are empty motions carrying
only an END command word (verified byte-for-byte; Mario's table has the same
shape). The store admits the shared victim groups 267-275 / 278-283; those
rows carry no commands for Ness, so a captured/shouldered Ness can never reach
the unsupported-command sentinel from his own store — the animation identity
comes from the thrower's store via the transfer path. `store.command_ready`
for those rows is exercised in `tests/gameplay_action_store_trace.cpp`, and
the Donkey cargo branch now derives the victim kind from the selection so
`p1=Donkey, p2=Ness` drives a real shouldered-Ness capture through the same
code paths that pass for Mario.

## Checkpoint-3 segfault: root cause and repair (resolved)

The retained construction segfault reduced to the shared item-command
decoder, not the blending relaxation: reverting the TObj relaxation did not
restore the clean rejection (the crash is upstream of texture loading), and
instrumented runs localized the fault to the item anim-command dispatch
(`it_802799E4`, `itanimlist.c:384`) reading an out-of-bounds handler-table
entry (native opcode 36) and calling the item render callback with a garbage
index. The decoder's consumed-length table disagreed with the original
interpreter:

- **Opcode 16** (`it_8027978C`) consumes its command word plus one operand
  word, plus a second operand word when the sub-opcode in source bits 25..18
  is 0..2 (three words total); the decoder consumed one, so every subsequent
  dispatch walked into operand bytes.
- **Opcode 10** (`it_80278F2C`) consumes five words; the decoder consumed one.
- Common opcodes 5/6/7/8/9 (subroutine, return, goto, SetTimerAnimation,
  flash) were rejected outright; 5/7 additionally require following the
  relocated jump target and patching the emitted pointer word to the target's
  native index.
- An unterminated SetLoop (body executes once; Ness's PK Flash explosion
  script authors exactly this) was rejected although the original runtime
  never validates loop balance.

The repair lives in `src/dat_item_commands.{hpp,c}`: exact consumed lengths
for 10/16, admission and native encodings for 5/6/7/8/9 (jump targets patched
to native indices), and tolerance for the source's unbalanced SetLoop. The
trace harness also needed Ness-specific input recipes in the shared damage
loop: close approach (the released flash detonates where it spawned) and a
hold-charge-then-release B pattern (the pulsing pattern restarts
`SpecialNStart` forever and never reaches the charge loop). The earlier
"Release vs RelWithDebInfo" discrepancy was a stale served binary: the
browser-release target was not rebuilt by the unqualified
`--trace-target` invocation; always pass `--configuration Release` and verify
the served directory matches the built configuration.

## Honest scope

Development candidacy only. Independent original comparison, pixels, PCM,
live scheduling, performance admission, CSS/SSS round trip and complete
move/costume/stage coverage remain open. Ness is enabled in content
registration and menu availability by the source-owned content row; the
native checkpoint-3 lifecycle now passes end to end and the browser route
gate remains the next admission step.
