<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Slippi render-dependency diagnostic profile (v2)

`slippi-render-dependencies-v2` is an intentionally incomplete diagnostic
identity. It covers three source-side dependency fixes and a source-draw
decision for the bounded correction fixture. It does not identify a complete
Slippi gameplay, online, rollback, roster, or stage profile. The identity is
fixed before initialization; there is no runtime mode-switch API. The v1
identity is historical and is rejected by the current profile implementation.

The C implementation in this directory is licensed under
[GPL-3.0-or-later](COPYING). The small source overlay is
[`patches/melee-profile-hooks.patch`](patches/melee-profile-hooks.patch). Its
upstream call-site context remains under the upstream file's own notice; this
directory does not relicense or modify `.deps/melee`. No assembly source is
transliterated into C. The pinned ASM repository's GPLv3 notice is reproduced
in `COPYING`; that notice does not replace per-file notices in Ishiiruka.

The overlay is opt-in through
`prepare_sources(..., slippi_profile=True)`. It creates the separate
`build/gameplay-source-slippi-profile` source checkout and composes this overlay
after the ordinary reviewed gameplay patch and boolean transform. The default
`prepare_sources()` path and ordinary builds keep their existing source tree
and behavior. The profile fixtures compile this checkout with
`MELEE_WEB_SLIPPI_PROFILE=1`; the source-hook code is absent when that define is
unset.

## Implemented behavior

| Pinned Slippi source and injection | Downstream patch site and source path | Behavior and dependency classification | Evidence limit |
| --- | --- | --- | --- |
| `Common/FastForward/DynamicsFix.asm`, `fcf47f10dc244152c2ebaa3a9dec142ea42243b7`, `0x8009E090` | [`patches/melee-profile-hooks.patch`](patches/melee-profile-hooks.patch); `.deps/melee/src/melee/ft/ftdynamics.c`, tail of `ftCo_8009DD94` after its dynamics and Flatzone work. `Fighter_8006D9AC` calls `ftCo_8009E0A8`, whose only call is `ftCo_8009DD94`. | Render-to-update dependency: fighter dynamics leave some JObj matrices dirty, and the shadow-render path normally resolves matrices. The profile walks root, child, then sibling; it calls `HSD_JObjSetupMatrixSub` only for `JOBJ_MTX_DIRTY` nodes without `JOBJ_USER_DEF_MTX`, using the upstream HSD flags. This runs in both present and correction fixtures. | The patch is placed at the source routine's exit corresponding to the pinned injection point. Downstream bone-position, collision, and hitbox reader closure remains incomplete; no claim is made that this closes every render-to-simulation dependency. |
| `Online/Core/BrawlOffscreenDamage.asm`, same pin, `0x8006A880` | [`patches/melee-profile-hooks.patch`](patches/melee-profile-hooks.patch); `.deps/melee/src/melee/ft/fighter.c`, existing `ifMagnify_802FC998` predicate in `Fighter_8006A360`'s offscreen-damage timer. | Render-to-update dependency: native `ifMagnify_802FBBDC` is a camera render callback that writes `is_offscreen`, then `Fighter_8006A360` reads it through `ifMagnify_802FC998`. The profile replaces that one predicate with strict camera-bound comparisons on fighter position, while excluding Home Run Contest in-game, the typed dead bit `fp->x221F_b1`, and action states 4 and 6. Equal-to-bound positions are inside; ordinary C comparisons leave NaN comparisons false. | Camera bounds are read through the existing stage getters. The first runtime fixture is Mario on Final Destination and does not validate all fighters, actions, or stages. |
| `Online/Core/FreezeDeadUpFallPhysics/`: `InitHitVelocity.asm` (`0x800D4C1C`), `UpdateFallVelocity.asm` (`0x800D4D68`), and `UpdateModelPos.asm` (`0x80080E80`) | [`patches/melee-profile-hooks.patch`](patches/melee-profile-hooks.patch); `.deps/melee/src/melee/ft/ft_0D31.c` cases in `ftCo_DeadUpFall_Anim` and `ftCo_DeadUpFall_Phys`, plus `ftdrawcommon.c`'s `ftDrawCommon_80080E18_inline2`. The C helpers are [`deadupfall_fix.c`](deadupfall_fix.c) and [`deadupfall_fix.h`](deadupfall_fix.h). | Case-2 initialization stores initial Y/Z in unused motion-union slots and zeros ordinary self velocity. Case-3 physics updates private Y with the original double subtraction, ordered strict terminal comparison, binary32 store/reload, and source-order vector accumulation, preserving the original later position update and early-break order. The draw helper transforms model position to a stack temporary for JObj translation and leaves `cur_pos` untouched; the original next-tick consumer and `fn_800D4DD4` path are included in the reduction. | The native fixture extracts actual case 2 and case 3 only. Its phase-1 no-dispatch check is a reduced-wrapper boundary control, not full phase-1 source coverage; full phase-1 behavior, interpolation, rotation, reader closure, and exceptional floating-point behavior remain unresolved. |

Both drawn-present and correction fixtures are configured to compile the same
three source fixes. The draw policy accepts only this profile identity: present ticks
allow source draw, correction ticks skip source draw, and invalid identities or
tick kinds return an error. The profile-compiled fixture cannot label itself
vanilla. This policy is not yet connected to an in-game rollback dispatcher.

## Other selected hooks reviewed, not implemented by v2

These entries bound the initial source-dependency review; they are not a whole
roster or stage call graph.

| Pinned Slippi source and injection | Source producer or consumer found | Classification and remaining gap |
| --- | --- | --- |
| `Online/Core/Hacks/FD/DesyncProofBGTransformations.asm`, `0x8021AAE4` | `.deps/melee/src/melee/gr/grlast.c`: `grLast_8021AAB0` calls `grLast_8021B2E8` in the Final Destination stage update path. `grLast_8021B2E8` mutates stage-owned state; in case 15, when `xC4_b27` is set, it calls `HSD_Randi` to choose background color and duration. The pinned patch wraps that exact call with save/restore of the shared HSD RNG seed. | Update/RNG edge around display-owned background state, not a direct draw-to-simulation read. The ASM comments place the call after `PlayerThink_Interrupt` and before `PlayerThink_Physics`, and say most gameplay RNG uses have already occurred. Other background RNG consumers and downstream stage-state closure remain unverified. |
| `Common/PSCameraIndependentMonitor/PSCameraIndependentMonitor.asm`, `0x801D24FC` | `.deps/melee/src/melee/gr/grpstadium.c`: `grStadium_801D2344` runs the monitor state machine; state 8 calls `grStadium_801D32D0`. That helper gets the active `HSD_CObj`, obtains the player's bone position through `ftLib_80086B90`, projects it with `lbVector_WorldToScreen`, clamps the monitor rectangle to the viewport, and writes wrapper coordinates. | Camera/view-derived display state feeds a Stadium update/state decision. The selected fixed-bounds patch is not part of this profile. The complete stage, RNG, and camera-state consumer closure is unverified. |
| `Online/Core/WhispyBlowDirFix/WhispyBlowDirFix.asm`, `0x8008653C` | `.deps/melee/src/melee/ft/ftlib.c`: the hook is inside `ftLib_800864A8`, which scans fighter entities, obtains bone positions through `ftLib_800866DC`, and compares their X positions to choose a horizontal direction. Callers include Green Greens stage logic (`grGreens_80213C10`) and other stage/item helpers. The helper also uses `HSD_Randi(2)` when the accumulated direction is tied. | Render-derived bone data can feed update-time direction, with a shared-RNG fallback; the pinned patch excludes Star KO and Screen KO states. Exact downstream force and all caller closure remain unresolved. |
| `Common/NanaDeterminism/NanaDeterminism.asm`, `0x800AC5B8` | `.deps/melee/src/melee/ft/kinds/ftCommon/ftCo_0A01.c`: `ftCo_800AC5A0` computes CPU throw-DI sticks and its source comment confirms `stick_x`/`stick_y` can remain uninitialized when the knockback vector is nearly zero. | Input initialization/determinism behavior, not a render dependency. It is outside v2. |
| `gm_80479D58.unk_4` presentation counter | The generated `gm_1A45.c` helper `melee_web_source_clock_present()` increments `unk_4`; the upstream render loop also increments it after `HSD_VICopyXFBAsync`. `gm_801A4B88(info)` only stores the active `GameSceneInfo*`; searches of pinned C found no read of `unk_4` outside its reset/increment sites. | Presentation-only counter in the inspected C, with non-decomp/ASM consumer closure unresolved. It is not advanced on correction ticks without source authority. |
| Display- and update-owned state across selected hooks | Confirmed examples include render callback state written by `ifMagnify_802FBBDC` and consumed by fighter offscreen-damage timing; fighter JObj matrices refreshed by the shadow-render path and read by later bone queries; camera/view-derived Stadium monitor placement; Final Destination background state with shared-RNG isolation; and the KO model callback that writes `cur_pos`. | These are distinct direct render-to-update, camera/view-to-update, and update/RNG edges. This bounded review does not establish that these are the only such edges or that any roster or stage family is complete. |

The initial executable scope is a Mario/Final Destination source-state
comparison. A passing fixture would establish only that fixture's declared
state and draw behavior; it would not establish original gameplay accuracy,
whole-session rollback agreement, online acceptance, other fighter behavior,
or other stage behavior.

### Additional bounded source review

The pinned Mario model-event table slot is null; the only populated slots in
that table belong to Kirby and Jigglypuff. Those callbacks change model
visibility and caches. The separate accessory-matrix callbacks also belong to
Kirby/Jigglypuff: they write an accessory JObj root matrix, and Jigglypuff's
costume-2/3 dynamics can read a parent matrix. Actual asset ancestry and all
indirect consumers remain unverified; the root matrix refresh does not itself
prove closure for accessory roots.

The inspected refraction reset reads the fighter's cloak flag and changes
GX/HSD graphics state. The cloak timer and gameplay readers are real, but this
reset does not write those flags. The inspected afterimage draw path reads its
history and emits local vertices/GX state; history is produced in the fighter
update path. Its bone-matrix reads retain the separate DynamicsFix dependency.
These bounded source findings do not establish full callback, alias, roster or
stage closure and do not justify additional fixes for the Mario fixture.

### Fighter drawing witness

The diagnostic records callback entry, normal fighter-model dispatch return,
and the special model-transform branch separately, keyed by authored source
player slot with processed PAD slot and fighter-kind metadata. The processed
slot is checked against the copied initializer's separate raw-controller map;
the recovered `controller_index` field is costume tint and is not input routing.
These counters sit
outside the 1,280-byte comparison observation. Invalid slots, metadata changes
and counter overflow fail explicitly. The no-draw lane must keep all counters
clear.

Each of the two interval ticks must advance callback and normal model-dispatch
counters for both selected Mario fighters. The observation must be ready,
unpaused and nonterminal, with four stocks and authored motion classifications
outside Entry, death and respawn. Raw observations, PCM and PAD plus decoded
fields are retained before comparison guards; screenshots cover the checkpoint
and final interval frame. A zero special-transform count leaves that branch's
live reachability unproved. The scoped two-tick result and retained first failure
are recorded in the [evidence receipt](../../docs/status/2026-10-12-match-only-rollback-feasibility.md#first-no-draw-profile-comparison).

### Historical v1 capture, not v2 validation

The retained v1 browser receipt records a 300-sample prefix and one restored
sample-300 tick, with 1,280 bytes of declared simulation state, 4,272 PCM
bytes, and a 44-byte raw PAD record. Its restore applied only to Node's
128 MiB snapshot; the browser lane was never restored, and its first screenshot
showed the starfield. The reported `corruptedLowByte` value 108 was observed
after the step; the pre-step mutation was `195 ^ 1 = 194`. This evidence
predates the v2 DeadUpFall hooks and does not validate them. See the
[scoped receipt](../../docs/status/2026-10-12-match-only-rollback-feasibility.md#first-no-draw-profile-comparison)
for that historical result and its limits.

## Build and run the bounded diagnostic

Follow `docs/LOCAL_RESOURCES.md` and verify external storage first. Reuse an
owned Mario/FD snapshot asset directory; assets stay outside Git. The private
build entry composes the isolated source overlay and builds both lanes:

```sh
python3 scripts/build.py --configuration Release \
  --trace-target gameplay_slippi_synctest \
  --slippi-synctest-assets /path/to/owned/snapshot-mario
```

Use a fresh output parent on verified external storage and put the parent Node
process's `TMPDIR` there. Playwright creates its automatic Chrome profile inside
that directory; an explicit `--user-data-dir` argument is not supported by its
`launchServer` API. Wrap the complete runner in
`scripts/agent_workspace.py run -- python3 scripts/capture_owner_deadline.py`
with `--overall-timeout-ms 180000`, `--cleanup-reserve-ms 30000`, the installed
Chrome executable, `--require-browser-process`, `--require-cdp-attribution`, and
`--browser-report` pointing to the runner's `evidence.json`. Supply separate
owner-report, log and process-trace paths. The runner starts its own loopback
HTTP server; no external server process is required.

The supervised command is Node 22 or newer running
`src/slippi_profile/tests/run_synctest.mjs` with:

- `--node-runtime build/slippi-synctest-release/gameplay_slippi_synctest_node.js`
- `--browser-runtime build/slippi-synctest-release/gameplay_slippi_synctest_browser.mjs`
- `--assets /path/to/owned/snapshot-mario`
- `--out /path/to/fresh-output-parent/runner` (must not already exist)
- Optional `--playwright /path/to/installed/playwright` for the shared browser loader.

The headless browser retains audio processing with speaker output muted. This
fixed diagnostic checks 300 prefix samples and a restored two-tick interval at
input samples 300 and 301;
it is not a performance measurement or full-profile test. Retain the first
failure, exact build/source/assets and owner cleanup receipts. After two failed
experiments, reduce the boundary or obtain bounded review before another run.
A successful run must not be repeated without a new hypothesis. The current
[scoped receipt](../../docs/status/2026-10-12-match-only-rollback-feasibility.md#first-no-draw-profile-comparison)
keeps the historical v1 result separate from the corrected v2 two-tick result
and its retained attribution failure. Neither establishes complete profile or
live KO-path coverage.

The DeadUpFall spec repair supersedes earlier candidate wording about retaining
ordinary X accumulation: the retained hook resumes at `0x800D4D84`, after the
entire original `temp += self_vel` call. The profile preserves existing temp X,
adds `tempY + storedPrivateY` and `privateZ + tempZ` in that order, then retains
the original early-break, position update and clear. Focused native controls are
synthetic source reductions; they do not establish PPC NaN payload/FPSCR parity
or browser gameplay acceptance.
