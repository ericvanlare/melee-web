<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Slippi render-dependency diagnostic profile

`slippi-render-dependencies-v1` is an intentionally incomplete diagnostic
identity. It covers two source-side dependency fixes and a source-draw decision
for the first correction-tick fixture. It does not identify a complete Slippi
gameplay, online, rollback, roster, or stage profile. The identity is fixed
before initialization; there is no runtime mode-switch API.

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

The two source fixes are compiled identically in drawn-present and correction
fixtures. The draw policy accepts only this profile identity: present ticks
allow source draw, correction ticks skip source draw, and invalid identities or
tick kinds return an error. The profile-compiled fixture cannot label itself
vanilla. This policy is not yet connected to an in-game rollback dispatcher.

## Other selected hooks reviewed, not implemented by v1

These entries bound the initial source-dependency review; they are not a whole
roster or stage call graph.

| Pinned Slippi source and injection | Source producer or consumer found | Classification and remaining gap |
| --- | --- | --- |
| `Online/Core/Hacks/FD/DesyncProofBGTransformations.asm`, `0x8021AAE4` | `.deps/melee/src/melee/gr/grlast.c`: `grLast_8021AAB0` calls `grLast_8021B2E8` in the Final Destination stage update path. `grLast_8021B2E8` mutates stage-owned state; in case 15, when `xC4_b27` is set, it calls `HSD_Randi` to choose background color and duration. The pinned patch wraps that exact call with save/restore of the shared HSD RNG seed. | Update/RNG edge around display-owned background state, not a direct draw-to-simulation read. The ASM comments place the call after `PlayerThink_Interrupt` and before `PlayerThink_Physics`, and say most gameplay RNG uses have already occurred. Other background RNG consumers and downstream stage-state closure remain unverified. |
| `Common/PSCameraIndependentMonitor/PSCameraIndependentMonitor.asm`, `0x801D24FC` | `.deps/melee/src/melee/gr/grpstadium.c`: `grStadium_801D2344` runs the monitor state machine; state 8 calls `grStadium_801D32D0`. That helper gets the active `HSD_CObj`, obtains the player's bone position through `ftLib_80086B90`, projects it with `lbVector_WorldToScreen`, clamps the monitor rectangle to the viewport, and writes wrapper coordinates. | Camera/view-derived display state feeds a Stadium update/state decision. The selected fixed-bounds patch is not part of v1's two implementations. The complete stage, RNG, and camera-state consumer closure is unverified. |
| `Online/Core/WhispyBlowDirFix/WhispyBlowDirFix.asm`, `0x8008653C` | `.deps/melee/src/melee/ft/ftlib.c`: the hook is inside `ftLib_800864A8`, which scans fighter entities, obtains bone positions through `ftLib_800866DC`, and compares their X positions to choose a horizontal direction. Callers include Green Greens stage logic (`grGreens_80213C10`) and other stage/item helpers. The helper also uses `HSD_Randi(2)` when the accumulated direction is tied. | Render-derived bone data can feed update-time direction, with a shared-RNG fallback; the pinned patch excludes Star KO and Screen KO states. Exact downstream force and all caller closure remain unresolved. |
| `Online/Core/FreezeDeadUpFallPhysics/InitHitVelocity.asm` (`0x800D4C1C`), `UpdateFallVelocity.asm` (`0x800D4D68`), and `UpdateModelPos.asm` (`0x80080E80`) | `.deps/melee/src/melee/ft/ft_0D31.c` identifies `ftCo_DeadUpFall_Anim` and `ftCo_DeadUpFall_Phys`; their shared `unk_deadup` overlay uses the stored position/velocity vectors also addressed by the ASM. The physics routine runs `ftCommon_Fall`, adds self velocity to the stored vector, advances position, then clears the temporary vector. `ftdrawcommon.c` maps `UpdateModelPos` to the `ftDrawCommon_80080E18` render callback: its inline helper transforms that stored vector through the inverse viewing matrix, writes `fp->cur_pos`, then sets the fighter JObj translation. | Direct draw-to-physics-position write plus camera-dependent model placement. The ASM splits stored Dead-Up-Fall velocity/position from the ordinary velocity and updates the model position independently of the view transform's write to physics position. Downstream gameplay readers include Ice Climbers' Nana follower logic (`ftCo_800B101C` → `ftCo_CpuUpdateNanaFollow` → `ftCo_800B0E98`), which reads Popo's `cur_pos`; full model, collision, and other reader closure is unresolved. |
| `Common/NanaDeterminism/NanaDeterminism.asm`, `0x800AC5B8` | `.deps/melee/src/melee/ft/kinds/ftCommon/ftCo_0A01.c`: `ftCo_800AC5A0` computes CPU throw-DI sticks and its source comment confirms `stick_x`/`stick_y` can remain uninitialized when the knockback vector is nearly zero. | Input initialization/determinism behavior, not a render dependency. It is outside v1. |
| `gm_80479D58.unk_4` presentation counter | The generated `gm_1A45.c` helper `melee_web_source_clock_present()` increments `unk_4`; the upstream render loop also increments it after `HSD_VICopyXFBAsync`. `gm_801A4B88(info)` only stores the active `GameSceneInfo*`; searches of pinned C found no read of `unk_4` outside its reset/increment sites. | Presentation-only counter in the inspected C, with non-decomp/ASM consumer closure unresolved. It is not advanced on correction ticks without source authority. |
| Display- and update-owned state across selected hooks | Confirmed examples include render callback state written by `ifMagnify_802FBBDC` and consumed by fighter offscreen-damage timing; fighter JObj matrices refreshed by the shadow-render path and read by later bone queries; camera/view-derived Stadium monitor placement; Final Destination background state with shared-RNG isolation; and the KO model callback that writes `cur_pos`. | These are distinct direct render-to-update, camera/view-to-update, and update/RNG edges. This bounded review does not establish that these are the only such edges or that any roster or stage family is complete. |

The initial executable scope is a Mario/Final Destination source-state
comparison. A passing fixture would establish only that fixture's declared
state and draw behavior; it would not establish original gameplay accuracy,
whole-session rollback agreement, online acceptance, other fighter behavior,
or other stage behavior.

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
fixed diagnostic checks 300 prefix samples and one restored sample-300 tick;
it is not a performance measurement or full-profile test. Retain the first
failure, exact build/source/assets and owner cleanup receipts. After two failed
experiments, reduce the boundary or obtain bounded review before another run.
A successful run must not be repeated without a new hypothesis. The current
[scoped receipt](../../docs/status/2026-10-12-match-only-rollback-feasibility.md#first-no-draw-profile-comparison)
records evidence and remaining gates.
