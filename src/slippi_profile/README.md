<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Slippi render-dependency and UCF diagnostic profile (v5)

`slippi-render-dependencies-dbooc-shield-drop-v5` is an intentionally incomplete
diagnostic identity. It retains the render-dependency and DBOOC behaviors from
v4 and adds plain UCF 0.84 shield drop. Shared input-history behaviors and
extended shield drop remain outside this identity. It does not identify complete UCF,
Slippi gameplay, online, rollback, roster or stage support. The identity is fixed
before initialization; there is no runtime mode-switch API. Earlier identities
are historical and rejected by the current implementation. The retained v2
browser result does not validate v3 stage hooks or the v4 UCF hook. No v3 or v4
browser or live stage result exists. The actual-source reducers cover only their
declared synthetic-service controls; see the scoped receipts below.

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

## Source behavior in the v4 candidate

| Pinned Slippi source and injection | Downstream patch site and source path | Behavior and dependency classification | Evidence limit |
| --- | --- | --- | --- |
| `Common/FastForward/DynamicsFix.asm`, `fcf47f10dc244152c2ebaa3a9dec142ea42243b7`, `0x8009E090` | [`patches/melee-profile-hooks.patch`](patches/melee-profile-hooks.patch); `.deps/melee/src/melee/ft/ftdynamics.c`, tail of `ftCo_8009DD94` after its dynamics and Flatzone work. `Fighter_8006D9AC` calls `ftCo_8009E0A8`, whose only call is `ftCo_8009DD94`. | Render-to-update dependency: fighter dynamics leave some JObj matrices dirty, and the shadow-render path normally resolves matrices. The profile walks root, child, then sibling; it calls `HSD_JObjSetupMatrixSub` only for `JOBJ_MTX_DIRTY` nodes without `JOBJ_USER_DEF_MTX`, using the upstream HSD flags. This runs in both present and correction fixtures. | The patch is placed at the source routine's exit corresponding to the pinned injection point. Downstream bone-position, collision, and hitbox reader closure remains incomplete; no claim is made that this closes every render-to-simulation dependency. |
| `Online/Core/BrawlOffscreenDamage.asm`, same pin, `0x8006A880` | [`patches/melee-profile-hooks.patch`](patches/melee-profile-hooks.patch); `.deps/melee/src/melee/ft/fighter.c`, existing `ifMagnify_802FC998` predicate in `Fighter_8006A360`'s offscreen-damage timer. | Render-to-update dependency: native `ifMagnify_802FBBDC` is a camera render callback that writes `is_offscreen`, then `Fighter_8006A360` reads it through `ifMagnify_802FC998`. The profile replaces that one predicate with strict camera-bound comparisons on fighter position, while excluding Home Run Contest in-game, the typed dead bit `fp->x221F_b1`, and action states 4 and 6. Equal-to-bound positions are inside; ordinary C comparisons leave NaN comparisons false. | Camera bounds are read through the existing stage getters. The first runtime fixture is Mario on Final Destination and does not validate all fighters, actions, or stages. |
| `Online/Core/FreezeDeadUpFallPhysics/`: `InitHitVelocity.asm` (`0x800D4C1C`), `UpdateFallVelocity.asm` (`0x800D4D68`), and `UpdateModelPos.asm` (`0x80080E80`) | [`patches/melee-profile-hooks.patch`](patches/melee-profile-hooks.patch); `.deps/melee/src/melee/ft/ft_0D31.c` cases in `ftCo_DeadUpFall_Anim` and `ftCo_DeadUpFall_Phys`, plus `ftdrawcommon.c`'s `ftDrawCommon_80080E18_inline2`. The C helpers are [`deadupfall_fix.c`](deadupfall_fix.c) and [`deadupfall_fix.h`](deadupfall_fix.h). | Case-2 initialization stores initial Y/Z in unused motion-union slots and zeros ordinary self velocity. Case-3 physics updates private Y with the original double subtraction, ordered strict terminal comparison, binary32 store/reload, and source-order vector accumulation, preserving the original later position update and early-break order. The draw helper transforms model position to a stack temporary for JObj translation and leaves `cur_pos` untouched; the original next-tick consumer and `fn_800D4DD4` path are included in the reduction. | The native fixture extracts actual case 2 and case 3 only. Its phase-1 no-dispatch check is a reduced-wrapper boundary control, not full phase-1 source coverage; full phase-1 behavior, interpolation, rotation, reader closure, and exceptional floating-point behavior remain unresolved. |
| `Online/Core/WhispyBlowDirFix/WhispyBlowDirFix.asm`, `39f9d2a4b70d0b9be72da3e7fed718d0fd4831e5fa10fcca48ec2294a744ede1`, `0x8008653C` | [`patches/melee-profile-hooks.patch`](patches/melee-profile-hooks.patch); `.deps/melee/src/melee/ft/ftlib.c`, inside `ftLib_800864A8`, after `ftLib_800866DC(cur, &vec)` and before adding `sgn(vec.x - v->x)`. The helper is [`whispy_direction_fix.c`](whispy_direction_fix.c). | Render-to-update dependency: for the signed authored condition `motion_id <= ftCo_MS_Sleep` (value 11), the queried fighter contributes zero to the direction sum. The bone query, entity walk, existing eligibility gates, accumulator order, and `HSD_Randi(2)` tie fallback remain in source order. | The pure helper controls cover signed boundary values. The actual-source reducer demonstrates original and omitted-hook direction divergence, profile agreement, and preserved query/RNG order using synthetic bone and RNG services. No live Whispy/KO route or stage behavior is established. |
| `Common/PSCameraIndependentMonitor/PSCameraIndependentMonitor.asm`, `a05d2964f9219e5b76d8ed667f9df319b0372ec87778c7a5949fce686d7d08ca`, `0x801D24FC` | [`patches/melee-profile-hooks.patch`](patches/melee-profile-hooks.patch); `.deps/melee/src/melee/gr/grpstadium.c`, state 8 in `grStadium_801D2344`, at the existing `grStadium_801D32D0` call. The helper is [`ps_camera_independent_monitor_fix.c`](ps_camera_independent_monitor_fix.c). | Camera/view-to-update dependency: the selected fighter is captured before the original camera/bone/projection helper runs. Its presentation-coordinate and wrapper side effects remain; afterward, the state decision uses the fighter's current position with authored sequential comparisons for the inclusive rectangle `-120 <= x <= 120`, `-20 <= y <= 80`. Ordered comparisons leave quiet NaN comparisons false, as in the source branch sequence. | Pure helper controls cover inclusive bounds, adjacent outside values and quiet-NaN fallthrough. The actual-source state-8 gate, projection helper and transition RNG loop pass paired-view and omission controls. Projector/RNG inputs and the downstream mode-setter observer are synthetic; there is no live Stadium runtime or display result. |
| `External/UCF 0.84/UCF/UCF DBOOC SquatRv Fix.asm`, `fcf47f10dc244152c2ebaa3a9dec142ea42243b7`, source SHA-256 `39a50bcab5c454d9fbf5865ca2ebdb947031959d0c4c8b188abf4d337da29c80`, `0x800D65EC` | [`patches/melee-profile-hooks.patch`](patches/melee-profile-hooks.patch); `.deps/melee/src/melee/ft/kinds/ftCommon/ftCo_SquatRv.c`, the positive threshold in `ftCo_SquatRv_CheckInput`. Own-C helper [`ucf_dbooc_fix.c`](ucf_dbooc_fix.c). | Input behavior: only the unsigned X-tilt timer byte at `Fighter+0x670` equal to zero admits the fused axis calculation. Checked conversion and modulo-32 arithmetic reproduce the signed squared-radius decision; only then select positive threshold bits `3f170a3d`. Preserve original stick-Y comparison, Enter and return order. | The independent finite rational oracle, native/Wasm scalar controls and source-transition reducer are recorded in the [DBOOC boundary](../../docs/status/2026-10-18-ucf-dbooc-threshold.md). Nearest-even/masked-exception scalar results do not establish FPSCR/trap/NaN-payload parity, matched patched-Slippi execution, live activation or complete UCF. |
| `External/UCF 0.84/UCF/UCF Shield Drop.asm`, `fcf47f10dc244152c2ebaa3a9dec142ea42243b7`, source SHA-256 `5b95d5ae91523453fa8b8cb71349e3d01b2cbb3d4265ebf008351cef9b77b8dd`, `0x800998A4` | [`patches/melee-profile-hooks.patch`](patches/melee-profile-hooks.patch); both original boolean callers `ftCo_80099794` and `ftCo_8009980C` in `ftCo_Escape.c`. Own-C helper [`ucf_shield_drop_fix.c`](ucf_shield_drop_fix.c). | Input behavior: when the ordered C-stick/main-stick comparisons, unsigned X-timer versus signed window, grounded platform and signed quantized-radius guards all pass, return false before the original neutral-escape transition. Preserve each caller's outer gates and the original Yoshi/default dispatch followed by cleanup when suppression does not apply. The radius calculation reuses the DBOOC scalar primitive. | The [shield-drop boundary](../../docs/status/2026-10-18-ucf-shield-drop.md) separates native/Wasm actual-source controls from matched patched-Slippi execution and live gameplay, which remain unrun. Nearest-even/masked-exception assumptions match the prior scalar contract. |

Both drawn-present and correction fixtures are configured to compile the same
profile source hooks. The draw policy accepts only this profile identity:
present ticks allow source draw, correction ticks skip source draw, and invalid
identities or tick kinds return an error. The profile-compiled fixture cannot
label itself vanilla. This policy is not connected to an in-game rollback
dispatcher. The actual-source reducers exercise their declared boundaries with
omitted-hook controls; prior helper/source results remain separately scoped.
They do not establish whole-stage execution, and the prior v2 browser capture
remains separate.

## Other selected hooks reviewed, not implemented by v5

These entries bound the initial source-dependency review; they are not a whole
roster or stage call graph.

| Pinned Slippi source and injection | Source producer or consumer found | Classification and remaining gap |
| --- | --- | --- |
| `Online/Core/Hacks/FD/DesyncProofBGTransformations.asm`, `0x8021AAE4` | `.deps/melee/src/melee/gr/grlast.c`: `grLast_8021AAB0` calls `grLast_8021B2E8` in the Final Destination stage update path. `grLast_8021B2E8` mutates stage-owned state; in case 15, when `xC4_b27` is set, it calls `HSD_Randi` to choose background color and duration. The pinned patch wraps that exact call with save/restore of the shared HSD RNG seed. | Update/RNG edge around display-owned background state, not a direct draw-to-simulation read. The ASM comments place the call after `PlayerThink_Interrupt` and before `PlayerThink_Physics`, and say most gameplay RNG uses have already occurred. Other background RNG consumers and downstream stage-state closure remain unverified. |
| `Common/NanaDeterminism/NanaDeterminism.asm`, `0x800AC5B8` | `.deps/melee/src/melee/ft/kinds/ftCommon/ftCo_0A01.c`: `ftCo_800AC5A0` computes CPU throw-DI sticks and its source comment confirms `stick_x`/`stick_y` can remain uninitialized when the knockback vector is nearly zero. | Input initialization/determinism behavior, not a render dependency. It is outside this profile. |
| `gm_80479D58.unk_4` presentation counter | The generated `gm_1A45.c` helper `melee_web_source_clock_present()` increments `unk_4`; the upstream render loop also increments it after `HSD_VICopyXFBAsync`. `gm_801A4B88(info)` only stores the active `GameSceneInfo*`; searches of pinned C found no read of `unk_4` outside its reset/increment sites. | Presentation-only counter in the inspected C, with non-decomp/ASM consumer closure unresolved. It is not advanced on correction ticks without source authority. |
| Display- and update-owned state across selected hooks | Confirmed examples include render callback state written by `ifMagnify_802FBBDC` and consumed by fighter offscreen-damage timing; fighter JObj matrices refreshed by the shadow-render path and read by later bone queries; camera/view-derived Stadium monitor placement; Final Destination background state with shared-RNG isolation; and the KO model callback that writes `cur_pos`. | These are distinct direct render-to-update, camera/view-to-update, and update/RNG edges. This bounded review does not establish that these are the only such edges or that any roster or stage family is complete. |

The retained v2 executable comparison used Mario/Final Destination. It is
scoped to that identity and its declared state, PCM, PAD, and draw observations.
The v3 Whispy and Stadium source hooks have not been exercised in a browser or
stage runtime. Even a passing actual-source reducer establishes only its
declared source boundary; it would not establish original gameplay accuracy,
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

### Historical captures and identity boundaries

The retained v1 browser receipt records a 300-sample prefix and one restored
sample-300 tick, with 1,280 bytes of declared simulation state, 4,272 PCM
bytes, and a 44-byte raw PAD record. Its restore applied only to Node's
128 MiB snapshot; the browser lane was never restored, and its first screenshot
showed the starfield. The reported `corruptedLowByte` value 108 was observed
after the step; the pre-step mutation was `195 ^ 1 = 194`. This evidence
predates the v2 DeadUpFall hooks and does not validate them. A later two-tick
browser result used the v2 identity; it remains v2 evidence and does not
validate the Whispy or Stadium hooks added under v3. See the
[scoped receipt](../../docs/status/2026-10-12-match-only-rollback-feasibility.md#first-no-draw-profile-comparison)
for the separate historical results and their limits. No browser capture under v3, v4 or v5 has run.

## Build and run the bounded diagnostic

For an authorized diagnostic build, follow `docs/LOCAL_RESOURCES.md` and verify
external storage first. Reuse an owned Mario/FD snapshot asset directory;
assets stay outside Git. The private build entry composes the isolated source
overlay and builds both lanes. Release build results are recorded in the
existing boundary receipt:

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

The headless browser retains audio processing with speaker output muted. The
retained v2 run used a 300-sample prefix and a restored two-tick interval
at input samples 300 and 301. The current diagnostic keeps that schedule; it is
not a performance measurement or full-profile test. Retain the first
failure, exact build/source/assets and owner cleanup receipts. After two failed
experiments, reduce the boundary or obtain bounded review before another run.
A successful run must not be repeated without a new hypothesis. The
[scoped receipt](../../docs/status/2026-10-12-match-only-rollback-feasibility.md#first-no-draw-profile-comparison)
keeps the historical v1 result separate from the v2 two-tick result. Neither
establishes complete profile or live KO-path coverage, and neither validates the v3, v4 or v5 additions.

The DeadUpFall spec repair supersedes earlier candidate wording about retaining
ordinary X accumulation: the retained hook resumes at `0x800D4D84`, after the
entire original `temp += self_vel` call. The profile preserves existing temp X,
adds `tempY + storedPrivateY` and `privateZ + tempZ` in that order, then retains
the original early-break, position update and clear. Focused native controls are
synthetic source reductions; they do not establish PPC NaN payload/FPSCR parity
or browser gameplay acceptance.

## Logical-frame clock v6 (explicit diagnostic opt-in)

`--slippi-logical-frame-clock` selects the separate private Release identity
`slippi-render-dependencies-logical-frame-v6`. Default v5 and ordinary source
behavior remain unchanged. This is own-C profile behavior, not a matched ASM or
complete Slippi identity. The GPL-3.0-or-later pure helper is
`logical_frame_clock.c`; the overlay stores one typed COLD/PENDING/COMPLETED phase.
A successful source post marks PENDING. A successful real draw return completes
it once; an accepted next source pre completes a skipped prior frame only after
all source gates pass and before controller evaluation/OnFrame. Repeated draws
do not double this logical housekeeping. No renderer resource or original clock
counter is assigned a synthetic target. Full clock and phase equality remain
mandatory, and the new phase is restored only by the existing typed CPU writer.
MENU, legacy flow-only restore, ordinary and default v5 paths are unchanged.
Reduced clock controls with modeled service leaves do not establish gameplay,
seven-tick, whole-profile or rollback milestone acceptance. No browser interval
is enabled by this option alone.

### Private fighter and shadow presentation views

V11 is a port presentation adaptation for no-draw experiments, not an upstream
Slippi patch equivalence claim. It preserves original projection arithmetic,
current-camera gates and late magnifier consumers inside the complete source
draw. Only the coordinate pair and the magnifier bit are temporarily supplied
from an actual Fighter-lifecycle-owned presentation view, then canonical source
values are restored. Prior coordinates remain available when original projection
returns without writing. Actual create receipts and current main-pool allocation
and Fighter identities are required; no capture-time owner adoption is allowed.
The ordinary profile remains unchanged. The fixed strict payload comparison is
retained, and focused view controls do not establish browser/visual correctness.

V12 extends this private port adaptation to the verified fighter shadow-camera
write boundary. The actual Fighter completion hook follows shadow construction;
child ownership ends before original shadow teardown. Camera/WObj values and the
direct shadow texture matrix stay visible through the complete original draw,
including late texture consumers, before canonical values are restored. Original
arithmetic, draw order, list/refcount operations and GPU output remain in place.
Ownership pointers are not value snapshots. Unsupported child, image and
inverse-cache transitions must refuse explicitly. Original list/active preparation
may evolve before a checkpoint; membership/active stability is required throughout
the existing armed CPU interval, including the final draw. This is not authority over
the main camera's independent inverse cache or complete Slippi compatibility.
The private build requires the existing CPU provenance configuration; V11
manifests/snapshots are refused by V12. Ordinary profiles remain unchanged.

V13 keeps GPU palette slot names local to each original TObj setup call. A
parallel eight-entry local name array follows the original palette-pointer list,
its duplicate comparison and its normal/big slot counters. GPU commands consume
the same selected names without writing them into the loaded source palette.
This avoids a draw-only payload difference during no-draw correction; it does
not change palette contents, selection order or the ordinary Vanilla path.
The canonical original loader copies descriptors into distinct runtime palettes;
this adaptation does not admit arbitrary external aliases between those palettes
and SObj descriptors. The private V13 identity refuses V12 snapshots/manifests.
It remains an incomplete diagnostic profile, not online-profile admission.


V14 targets the observed effect-model matrix dependency. The private profile
prepares dirty, non-user-defined matrices under the original effect GObj owners
after match process callbacks, while the source callback context is still active.
This includes effects spawned and attached during the current tick. It uses the
original matrix routines; it adds no source draw or GPU submission to correction.
The ordinary scheduler entry remains unchanged for menus and other scenes.
The scheduler lists are shared: non-effect objects retain topology checks but
are not treated as effect owners. Unsupported class-8 effect shapes fail before
any effect matrix preparation.
The full source payload comparison remains strict. V13 manifests and snapshots
are refused by this candidate, which remains an incomplete diagnostic profile
pending its browser result and later rollback gates.
