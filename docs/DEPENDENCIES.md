# Dependencies and portability boundaries

Audit date: 2026-09-07. `dependencies.lock.json` is authoritative; these are the
revisions inspected for the first probe.

| Dependency | Pinned source revision | Role |
| --- | --- | --- |
| [doldecomp/melee](https://github.com/doldecomp/melee) | `006b50d24cce1b6385d0bacea0af8f1e6d02b4f4` | Recovered game and HSD C; current target compiles only HSD `state.c` |
| [encounter/aurora](https://github.com/encounter/aurora) | `749d6ee7a22bdfab78c8ece9047bca5d79aa72ca` | Source-level GX renderer and platform services |
| [emscripten-core/emsdk](https://github.com/emscripten-core/emsdk) | `5eb0bde7585670252e8ba05e9d361627bffd08b5` | Installer for Emscripten **6.0.9**, including its compiler and browser WebGPU bindings |

Build tools: CMake **3.31.6** and Ninja **1.13.0**, installed in the local virtual
environment. Aurora browser adaptations belong in `patches/aurora-browser.patch`;
the upstream commit alone does not describe the browser implementation.

## Reproducibility and provenance

Bootstrap checks the three Git HEADs and the exact recorded Aurora patch, rejecting
unexpected source changes. Aurora's CMake files fetch further libraries; the root
`cmake/PinnedArchives.cmake` supplies the missing Abseil and SDL archive hashes.
The browser target forces source dependencies, disables native package discovery,
and checks that libpng receives the actual vendored zlib target. It uses SDL
**3.4.10** and Emscripten's **v20260423.175430** `emdawnwebgpu` port instead of
native Dawn. All active CMake archives now have content hashes; the SDK verifies
its WebGPU package with SHA-512.

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

The current seam is original `src/sysdolphin/baselib/state.c`, called by our
`src/hsd_probe.c`. Function/data sections allow unused HSD systems to be discarded
when linking the synthetic GX scene. The probe calls the original render-state
cache invalidators and setters; its triangle is authored test geometry.

`src/hsd_probe_compat.h` adapts observed header differences only for that HSD
translation unit: the `Vec3` name, missing `GXTevClampMode` declaration, `va_list`
include order, and Melee's conflicting local `ssize_t` typedef. The latter is
renamed only while including `Runtime/platform.h`; libc keeps its system type.
No SDK runtime calls are replaced with success stubs.

The simple `DrawRectangle` function in original HSD `hsd_3915.c` is another possible
geometry seam, but that whole file requires disc-derived `debug_font.inc` during
compilation. The initial probe does not fabricate that asset to make it compile.

## Portability work still required

All paths below refer to the pinned original Melee tree.

| Boundary | Evidence | Required treatment |
| --- | --- | --- |
| Asset layout | HSD `archive.c`, `archive.h`; `src/melee/lb/lbarchive.c` | Big-endian DAT headers and payload structures, plus in-place 32-bit pointer relocation; decode with explicit types and bounds |
| Memory regions | `src/melee/lb/lbfile.c`, `lbmemory.c`, `lbheap.c`; HSD `initialize.c` | Numeric addresses distinguish GameCube RAM/ARAM; browser pointers do not carry those meanings |
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

## Next asset gate

After the synthetic GX/HSD state probe, render an authored static descriptor
through the original HSD `jobj.c` → `dobj.c` → `pobj.c` path. Then load one static
model from a player-supplied DAT using a bounded, schema-aware decoder and a fixed
camera. Relocation metadata does not describe every field's type: swapping the
header or every 32-bit word is insufficient, and raw GX command bytes may need to
retain their original byte order. This gate exercises the real asset and polygon
boundaries before adding the scene scheduler, audio and match logic. See
[ROADMAP.md](ROADMAP.md) for acceptance criteria and [STATUS.md](STATUS.md) for
observed results.
