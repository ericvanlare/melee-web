# Architecture decisions

## 001 — Compile recovered source

Use doldecomp/melee source with a separate platform layer and browser build target.
No PowerPC CPU emulator, JIT, or hidden interpreter fallback. The original binary
is an offline validation reference. Keep downstream patches reviewable and small.
The first playable target is an accurate local Mario-versus-Mario stock match on
Final Destination at 60 fps; full vanilla Melee remains the overall goal.

## 002 — Evaluate Aurora before writing a renderer

Aurora already translates GX state and draws to WebGPU. Its native implementation
is the starting point. The prototype's browser adaptations must execute all queued
render work; disabling worker creation without replacing its execution is invalid.

The Dusk browser fork is a reference for integration seams, not our dependency.
Its documented skipped game-worker threads do not constitute a viable scheduler.

## 003 — Browser first, native debugging companion

Build and validate the browser boundary immediately. Native builds can isolate
compiler/logic problems but do not substitute for a working browser target. Keep
web UI limited to local launch, errors and evidence until gameplay exists.

## 004 — Explicit binary data conversion

WASM32 matches the original pointer width but not GameCube byte order. Convert
typed asset fields and relocations at an audited boundary; do not bulk-swap unknown
payloads. Validate offsets, counts and buffer bounds before pointer relocation.
Start with one synthetic archive test and one real asset, then expand by type.

## 005 — Preserve numerical semantics

No fast-math. The native compatibility macros are not a correctness specification.
In particular Melee's host __frsqrte placeholder uses sqrt although original callers
expect a reciprocal-square-root estimate. Resolve these operations and establish
reference tests before relying on gameplay math.

## 006 — Stage performance evidence

The first probe reports CPU submission and requestAnimationFrame intervals only.
They are not GPU duration, physical display latency, or a full-game benchmark.
Keep shader validation enabled during bring-up. Measure release builds separately.

## 007 — Reuse HSD behavior behind typed ownership boundaries

Immutable CPU descriptors are decoded from checked DAT offsets. Prepared scenes
own hydrated HSD materials and keep their archive alive until those resources
are destroyed. Original MObj/TObj expression compilation and TEV setup implement
material behavior; do not grow a second collection of approximated shader rules.
The host piece allocator provides real aligned allocation and verifies size
buckets on free. Original assertions remain fatal, never successful no-ops.

Texture SDK base objects have stable identities across frames. The adapter copies
them when original HSD requests a temporary GX object, while original filtering,
binding and texture-matrix code still executes. Original texture-matrix output
is checked before acceptance; Aurora texture and palette identities are explicitly
destroyed with their owner. Joint and normal matrices are refreshed when the pose
changes. The inspection camera and lights are authored context, separate from the
future game scene loader and scheduler.

Direct RGBA8 vertex colors remain in the original GX display-list bytes and
follow HSD's vertex-color material path. Material shininess remains finite and
nonnegative but has no invented 128 cap: original `HSD_LObjSetup` uses
`shininess / 2` and `1 - shininess / 2`. Fox's value near 296 therefore reaches
the original lighting calculation unchanged.

## 008 — One input sample boundary

Poll Aurora PAD once after its event update, independently of whether a frame can
be presented. Preserve raw PADStatus values for future game code. A separately
clamped diagnostic copy uses the original provider's PADClamp; game code must not
receive an already-clamped value and apply its own clamp again. Keyboard fallback
is explicit, gives a physical port-0 controller priority, and requires canvas,
window and document activity. Losing activity immediately neutralizes input even
if the browser suspends rendering.

## 009 — Original envelope skinning, unchanged geometry

The typed model decoder preserves inverse bind matrices, ordered envelope weights
and direct GX matrix-index attributes. It validates primitive packets and every
referenced matrix slot before handing untouched big-endian display and vertex
bytes to the bridge. Current bounds are 256 joints, 10 palette slots per mesh and
31 influences per slot; shape animation and indexed matrix-load commands reject.

Owned HSD joints carry parent links, original flags, evaluated world matrices and
optional inverse binds. Original `SetupEnvelopeModelMtx` and
`_HSD_mkEnvelopeModelNodeMtx` calculate the palette, including the distinct
single-influence and weighted branches. Weights are not normalized by the port,
and vertices are not rewritten into a replacement skinning implementation.
Unexpected requests for dynamic joint setup fail explicitly: the ordinary SRT
bridge must evaluate a pose before the skin skeleton is updated.

Scoped GX adapters capture the source's position, normal and reflection matrix
loads and reject nonfinite output before upload. Identity-view palettes supply
world bounds for camera fitting, using each slot's referenced position bounds.
Rendering prepares palettes after material setup so original texture-context
queries see reflection requirements. Palettes are cached by pose revision;
unchanged skeleton size also reuses owned joint storage during animation.

## 010 — Fighter visibility indexes display objects

`DatFighterParts` decodes `ftData` to `FtPartsDesc`, costume fallback, representation
groups and variant lists into checked types. A generated original-source registry
binds the exact model symbol to fighter kind and costume index. This does not
replace fighter initialization or action visibility commands.

A DObj can own multiple PObjs. The model therefore records preorder DObj
occurrences separately from mesh indices, following `ftParts_80074194`. Normal
inspection hides the listed main-model alternates and enables the sole normal
variant in each group; unlisted DObjs keep their default visibility. Ambiguous
variant groups reject until an explicit selector exists. Metal indices refer to
a separate DObj list and must never be applied to the main model. Default Mario's
normal selection is 43 DObjs covering 52 meshes out of the 68 decoded meshes;
default Fox selects 45 of its 77 DObjs.

## 011 — Source animation evaluation before gameplay scheduling

The FigaTree decoder validates node counts, channels and packed stream operands,
then the animation bridge owns their bytes and hydrated HSD AObj/FObj state.
Original `aobj.c`, `fobj.c` and `spline.c` perform frame control and interpolation.
The pose callback supports ordinary Euler rotation, translation and scale;
untracked values retain their bind pose. Classical-scale flags follow the
original animation loader's policy for animated nodes. Visibility tracks,
gameplay events, transition blending and non-SRT channels remain unsupported.
FigaTrack byte 7 is unnamed C-struct alignment padding, ignored by original
`lbAnim_InitFrames`; it is not a channel flag. Nonzero padding in WalkSlow does
not bypass checks on named fields or packed operands.

`generate_fighter_registry.py` reads narrow, checked original C initializers for
fighter data, costume identities, motion counts and animation-container names.
The build checks the generated table against the pinned source; unexpected syntax
or ambiguous model identities reject. `DatCommonFighterLayout` reads the selected
kind's `PlCo.dat` part count and alternate-parts metadata. Ordinary initial parts
follow `ftParts_SetupParts` preorder and `ftAnim_8006F4C8` node consumption. Binding
requires that layout, the exact source costume identity, matching part/model/node
counts and exact action-table symbol and archive length. Nonnull alternate-parts
descriptors reject until original insertion and motion-mask behavior is supported.

`DatFighterActions` retains source motion IDs while omitting empty archive records.
The viewer retains the imported full AJ container, validates every declared range,
and slices the explicitly selected action by its source offset and length. Shared
archive aliases retain their separate motion IDs and flags. This enables Mario
and Fox Wait1 and WalkSlow without filename-derived animation binding or a growing
fighter whitelist; it does not execute action commands or transitions.

Inspection playback advances at fixed 60 Hz, independently of presentation rate,
with at most eight catch-up steps per tick. Larger stalls pause playback with a
message; hidden time is excluded and pause/resume resets the clock. Completed
clips can loop in the viewer. Pose changes rebuild original joint transforms and
invalidate palette caches while retaining the fitted inspection camera. This
clock is a viewer service, not Melee's eventual simulation scheduler.

The inspection projection uses the SDK's `C_MTXOrtho`. GX clip depth spans
`[-w, 0]`; Aurora applies its reversed-depth conversion and comparison mapping.
A trace checks that front geometry wins the resulting depth test. Camera fitting
stays fixed while playing animation or changing representation metadata.

## 012 — Explicit stage entry and render-pass inspection

`DatStage` decodes `map_head` and its counted 0x34-byte map-entry table. The viewer
enumerates entries and requires a selected entry; it does not assume every joint
tree is simultaneously visible. Camera, lights, fog, animation, collision and
other referenced stage services are reported as unapplied.

Complete-model decoding remains strict. The explicit opaque pass follows original
DObj material classification and reports omitted texture-edge and translucent
DObjs and meshes. It retains mesh owners, referenced envelope joints and all their
ancestors, remaps that transform closure, and validates the retained joint behavior.
Unsupported flags on a required ancestor are never cleared to make a draw pass.
The GrNLa entry-3 gate retains 13 opaque meshes and reports 13 omitted meshes.
That partial draw is stage inspection, not complete stage loading or gameplay.

## 013 — Original runtime with explicit ownership

The separate gameplay Wasm target executes original HSD GObj allocation, process
ordering, deferred mutation and userdata destruction. A wrapper around Aurora's
actual OS allocator claims one exclusive arena, rejects foreign initialization,
and clears allocator metadata before freeing owned memory. World generations
invalidate surviving handles and private pools across teardown/restart. The
current world excludes graphics-object kinds until their real lifecycle exists;
it does not substitute for full `Fighter_Create` or a match scheduler.

`gameplay_sources.py` prepares a checked downstream source tree under `build/`
and applies `patches/melee-gameplay.patch`; pristine `.deps/melee` is unchanged.
Compiler comparisons against PowerPC establish canonical fighter animation-flag
aliases on Wasm. Imported packed command unions still require explicit field
decoding; swapping a word and casting it to a native bitfield struct is invalid.

## 014 — Data readiness precedes native publication

`DatCommon` owns typed root0 scalars and an inventory of all23 common roots. Its
schema is generated from the pinned source and asserts every original Wasm field
offset/width. Unknown scalar words remain opaque integers, not invented pointers.
Other roots remain unresolved graphs. The focused Fighter probe allocates real
source storage, executes original input reset and walk-threshold logic, and
restores the previous common context afterward. This scoped consumer is not
`Fighter_LoadCommonData` and must not publish a half-ready root table.

`DatCollision` preserves source line categories, adjacency, material flags and
counted joints. The runtime bridge copies mutable native arrays, then invokes
original `mpLibLoad`, pruning, island initialization and static queries. Stage
kind and source `grGroundParam.y` scale are explicit inputs. GObj userdata owns
teardown; stage joint bindings, callbacks, dynamic lines and fighter ECB/physics
remain pending. Empty archive binding tables do not eliminate source C bindings:
Final Destination maps collision joint0 to stage entry3/render joint0.

The next integration joins named common part maps, original HSD constructors,
Mario ftData/costume/material animation and source player/stage services. Preserve
the selected source motion record independently of shared figatree aliases.
Original `ftData_80085E50` distinguishes RAM/ARAM by numeric address; ordinary Wasm
pointers fail that test. Port storage identity and completion explicitly, reusing
checked decoded clips without pretending raw BE archive relocation is valid.
Acceptance is real creation, neutral source ticks, unload and restart before
movement/combat and the local stock-match gate.
