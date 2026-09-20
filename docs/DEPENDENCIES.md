# Dependencies and portability boundaries

Audit date: 2026-09-07. `dependencies.lock.json` is authoritative; these are the
revisions inspected for the current probe.

| Dependency | Pinned source revision | Role |
| --- | --- | --- |
| [doldecomp/melee](https://github.com/doldecomp/melee) | `b43912cc78606f96c9569f5d6229bc9d7e265ea5` | Recovered game/HSD C; retained rendering, animation, object scheduling and focused fighter/collision consumers |
| [encounter/aurora](https://github.com/encounter/aurora) | `749d6ee7a22bdfab78c8ece9047bca5d79aa72ca` | Source-level GX renderer and platform services |
| [emscripten-core/emsdk](https://github.com/emscripten-core/emsdk) | `5eb0bde7585670252e8ba05e9d361627bffd08b5` | Installer for Emscripten **6.0.9**, including its compiler and browser WebGPU bindings |

Build tools: CMake **3.31.6** and Ninja **1.13.0**, installed in the local virtual
environment. Aurora browser adaptations belong in `patches/aurora-browser.patch`;
the upstream commit alone does not describe the browser implementation.
Gameplay ABI corrections belong in `patches/melee-gameplay.patch`, applied only
to the checked generated `build/gameplay-source` tree. The current patch preserves
canonical fighter animation-flag aliases on little-endian Wasm and corrects the
Final Destination callback declaration to match its original definition.

## Reproducibility and provenance

Bootstrap checks the three Git HEADs and the exact recorded Aurora patch, rejecting
unexpected source changes. Aurora's CMake files fetch further libraries; the root
`cmake/PinnedArchives.cmake` supplies the missing Abseil and SDL archive hashes.
The browser target forces source dependencies, disables native package discovery,
and checks that libpng receives the actual vendored zlib target. It uses SDL
**3.4.10** and Emscripten's **v20260423.175430** `emdawnwebgpu` port instead of
native Dawn. All active CMake archives now have content hashes; the SDK verifies
its WebGPU package with SHA-512.

The Aurora patch generates the compatible ImGui WebGPU backend only when its
contents change. Reconfiguration preserves the generated file's timestamp when
unchanged, avoiding needless backend recompilation and dependent target relinks.
Existing checkouts transition from the exact previous reviewed patch by rerunning
bootstrap, as described in [Bootstrap and build](BUILD_AND_PLAY.md#bootstrap-and-build).

Python package versions are pinned without wheel hashes. This is a pinned
integration baseline, not a hermetic or bit-for-bit reproducible release build.
Tool downloads, host tooling and release artifact reproducibility remain future
hardening work.

The browser patch disables position-independent code for this static Wasm build.
The original configuration triggered an invalid LLVM relocation in zlib's
`adler32_stub`, also reported in [Emscripten issue 27525](https://github.com/emscripten-core/emscripten/issues/27525).
An isolated compile verified that removing PIC produces the correct function
relocation. Compression dispatch remains intact.

Aurora supplies an MIT license. No repository-wide license was found in the
inspected Melee root; licenses under its tools do not establish one for the game
source. Preserve source origin and notices, and do not relabel upstream code as
this project's original work. See [THIRD_PARTY.md](../THIRD_PARTY.md). Disc images,
extracted assets and generated game data remain outside tracked source.

## Existing native work in Melee

The original Melee tree already contains `.nix/melee-gcc-native.nix` and
`.nix/CMakeLists.txt`, exercised by `.github/workflows/build.yml`. That target uses
`TARGET_PC` and Aurora headers to create a **static library on i686 Linux**.
Its `.nix/overlay.nix` pins a different interface baseline:
`r-burns/aurora` at `e6a6f02ace4146e8a2f648d5c274dbb7dd89665c`.

The source list covers 806/908 game C files and 42/76 HSD C files. It excludes
major runtime paths such as `pobj.c`, `displayfunc.c`, `video.c`, `synth.c`,
`sobjlib.c` and `gobjinit.c` under `src/sysdolphin/baselib/`. Static archive creation
does not require all symbols to resolve. This work is useful compiler/interface
coverage; it does not demonstrate a linked executable, rendering or gameplay.

## Narrow HSD integration

The current seams are original `src/sysdolphin/baselib/state.c`, called by our
`src/hsd_probe.c`, and `PObjDispSimplePrimitive` from original `pobj.c`, compiled
inside `src/hsd_pobj_bridge.c`. Function/data sections discard unused HSD systems.
The bridge adapts `GXSetArray` to Aurora's explicit byte length and byte-order
arguments, and clears HSD descriptor caches when temporary descriptors are reused.
The default triangle is authored test geometry; local DAT import uses original
display-list and vertex bytes after validating their typed descriptors and indices.

`src/hsd_transform_bridge.c` calls original `HSD_MtxSRT` from `mtx.c` and Aurora's
SDK matrix concatenation. It preserves `HSD_JObjMakeMatrix`'s accumulated-scale
policy explicitly. The CPU decoder stores the joint graph and immutable raw
asset spans; `PreparedScene` owns transform state and GPU-facing resources.
This separates archive interpretation from device lifetime and keeps decoding
available to both browser import and the native batch checker.

`dat_texture.cpp` checks original tiled images, mip chains and palettes before
upload. Original HSD MObj/TObj expression compilation, TEV setup, diffuse/specular
lighting and reflection execute through `hsd_material_bridge.c` and the scoped
GX adapter. The earlier single-texture preview approximation has been replaced.
Owned material instances, stable texture identities and original geometry feed
Aurora; the inspection camera/light rig is still authored context, not a loaded
game scene.

`src/hsd_probe_compat.h` adapts observed header differences only for that HSD
translation units: the `Vec3` name, missing `GXTevClampMode` declaration, `va_list`
include order, and Melee's conflicting local `ssize_t` typedef. The latter is
renamed only while including `Runtime/platform.h`; libc keeps its system type.
No SDK runtime calls are replaced with success stubs.

The simple `DrawRectangle` function in original HSD `hsd_3915.c` is another possible
geometry seam, but that whole file requires disc-derived `debug_font.inc` during
compilation. The initial probe does not fabricate that asset to make it compile.

## Gameplay execution boundary

The particle renderer’s direct GameCube write-gather stores are mapped to the
equivalent GX position, texture-coordinate, and indexed-coordinate calls on PC.
This preserves component order and expressions while routing every particle
vertex through Aurora; the original PowerPC stores remain unchanged.

`scripts/build.py --target gameplay` builds separate Wasm ABI, scheduler, collision
and local-data probe executables. `scripts/check_gameplay.py` runs them with the
pinned Node runtime; optional `--common`, `--stage` and `--stage-kind` inputs remain
local. Final Destination is original `GrKind`37, distinct from viewer map entry3.

The scheduler retains original GObj/proc/ObjAlloc/memory routines. Its C++ heap
wrapper includes Aurora's unchanged OS allocator and adds exclusive ownership
and metadata release; it replaces that allocator translation unit in this target,
so both providers must not be linked together. Scoped source includes retain
original collision initialization/pruning/query behavior without successful
stubs for unreachable stage services.

`gameplay_compat.h` supplies observed vector/header differences and original math
constants. Generated common-field layout assertions and PowerPC/Wasm compiler
comparisons validate the reached ABI. They do not validate every Fighter union,
packed command stream or gameplay numerical path. The source census records
compile/link blockers independently from executable acceptance.
In particular, `melee/gr/types.h`'s `StageCallbacks` overlays a numeric `flags`
word with byte bitfields. Its PowerPC/Wasm correspondence remains to be corrected
and verified before executing stage callbacks; the fighter flag patch covers a
different structure.

## Portability work still required

All paths below refer to the pinned original Melee tree.

| Boundary | Evidence | Required treatment |
| --- | --- | --- |
| Asset layout | HSD `archive.c`, `archive.h`; `src/melee/lb/lbarchive.c` | Big-endian DAT headers and payload structures, plus in-place 32-bit pointer relocation; decode with explicit types and bounds |
| Memory regions | `src/melee/lb/lbfile.c`, `lbmemory.c`, `lbheap.c`; HSD `initialize.c` | Numeric addresses distinguish GameCube RAM/ARAM; browser pointers do not carry those meanings |
| Fighter animation storage | `src/melee/ft/ftdata.c`, `ftData_80085E50` | Values below `0x80000000` select ARAM, including ordinary Wasm pointers; replace address classification and raw archive relocation with explicit checked storage/decoded-clip ownership |
| Scheduling | `src/melee/gm/gmmain.c`, `gm_1A3F.c`, `gm_1A45.c`; HSD `video.c` | Infinite scene loops, controller-sample queues and retrace waits require an explicit browser lifecycle |
| File completion | `src/melee/lb/lbfile.c`; HSD `devcom.c` | Busy waits depend on asynchronous DVD/ARAM callbacks; preserve completion ordering |
| Audio | `src/melee/lb/lbaudio_ax.c`; HSD `synth.c`, `axdriver.c` | AX voices, ADPCM, looping, effects and ARAM sample storage need a software backend; an audio output device alone is insufficient |
| Binary assumptions | `src/Runtime/platform.h`, HSD `bytecode.c`, `src/melee/ft/types.h` | Pointer width, bitfields, packed structures, pointer/int/float overlays and callback signatures need separate verification |

**Concrete numerical hazard:** `src/placeholder.h` maps non-MWERKS
`__frsqrte(x)` to `sqrt(x)`. Callers such as
`src/melee/ft/kinds/ftMasterHand/ftmasterhandfingergun.c` expect a reciprocal-square-
root estimate, then apply Newton refinements. That mapping is not a correct host
implementation. It is outside the retained probe path, but must be fixed and
validated before gameplay. Successful host compilation cannot establish numeric
equivalence; avoid fast-math and validate floating-point behavior against original
game traces.

## Current runtime gate

The first playable target remains original Mario versus Mario on Final
Destination. Named common roots, Mario and Falco fighter data/costumes/actions,
Final Destination and Battlefield stage owners, source constructors/destructors,
player services, audio transport and match outcomes are integrated for the scopes
recorded in [STATUS.md](../STATUS.md). Common root20 remains loaded
unconditionally by the original fighter initializer; model inspection alone does
not satisfy that lifetime. Complete ordinary input, physical controllers,
audible/reference comparison and full-match timing remain open under
[ROADMAP.md](ROADMAP.md) and [the accuracy contract](ACCURACY_CONTRACT.md).

The executable stage animation lookup uses a volatile local pointer across
`setjmp`/`longjmp` and a pointer-typed traversal callback. Without volatility,
the Wasm compiler returns the initial null pointer even after the callback
finds an animation, stopping Final Destination at background state2. The
regression runs the original lookup at optimization level O2 and verifies
that removing volatility fails. Original PPC code remains unchanged.
