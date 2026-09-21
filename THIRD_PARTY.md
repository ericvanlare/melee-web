# Third-party provenance

Dependencies are fetched separately and pinned in dependencies.lock.json.

The public player has separate silent and audio-enabled package identities;
the optional maintenance shell has no runtime. See the exact artifact boundaries
in [the release review](docs/PUBLIC_RELEASE_REVIEW.md) and
[production audio](docs/AUDIO_PRODUCTION.md). The separate Dolphin reference
application is outside either browser package.

Repository publication also exposes tracked source, patches, generated material
and history. The [first-pass source/license inventory](docs/SOURCE_LICENSE_INVENTORY.md)
maps those boundaries and proposes a license for confirmed project-authored
portions. It does not grant that license or resolve the remaining rights questions.

| Source | Role | License/notice |
| --- | --- | --- |
| https://github.com/doldecomp/melee | Recovered game, HSD and original platform/SDK source compiled by the development runtime | No repository-wide license located in audited root; retain all source notices, do not relabel as our original code |
| https://github.com/encounter/aurora | GX renderer and platform compatibility | MIT; upstream LICENSE retained in .deps/aurora |
| https://github.com/emscripten-core/emsdk | Local Wasm SDK installation | Retain upstream notices and component licenses |
| https://github.com/project-slippi/slippi-js | Replay-format authority and conformance oracle; not linked into gameplay source | LGPL-3.0-or-later; pin version and retain upstream license when distributed |
| https://github.com/sh1ftmaker/dusk-wasm | Browser integration research/reference | Not a linked dependency; any future distribution must identify adapted code and its actual license separately |

Aurora fetches further dependencies (SDL, Dawn bindings, Abseil, fmt, ImGui,
texture/image libraries, SQLite and Tracy). Their source notices remain with each
download. License attribution for a distributable bundle must be assembled from the
actual dependency graph before a playable public release. The maintenance shell includes none of those runtime components.

No Nintendo disc images, extracted assets, audio, textures or game executables
are tracked. This repository does not grant rights to upstream code or game data.

## Audio implementation and separate Dolphin reference tools

The current `src/gameplay_audio_resample.c/.h` and `web/dsp-coefficients.mjs`
are replacement implementations. Their specifications, numerical compatibility
evidence and limitations are recorded in
[the audio replacement record](docs/AUDIO_REPLACEMENT_EVIDENCE.md).
The coefficient generator preserves the existing approximation byte for byte,
including numerical compatibility values from Dolphin's replacement DROM; this
does not make the table original hardware data or close its provenance review.
No generated coefficient binary is tracked and no project-wide license is chosen.

Earlier versions of those source files adapted Dolphin revision
`a2efdf1197be8132674b90fe9cf4761df39752ed`, under GPL-2.0-or-later.
Their history and previously built artifacts retain that provenance and any
applicable obligations. The [GPL text](docs/licenses/dolphin-gpl-2.0-or-later.txt)
and historical release inventory remain available.

The reference observer remains in `reference-capture/dolphin/`, with its
[license inventory](reference-capture/dolphin/LICENSES.md), source notices and
Dolphin patches intact. It builds a separate reference application; it is not
linked into the browser player. Replacing player audio does not relicense it.

## B0XX-style keyboard mapping

`src/boxx_input.h` adapts the coordinate tables and directional state rules from
[agirardeau/b0xx-ahk](https://github.com/agirardeau/b0xx-ahk/tree/7c070f8e0f135c8108cfb0af9a37dc6809070b15)
at commit `7c070f8e0f135c8108cfb0af9a37dc6809070b15`.
The MIT notice is retained in [licenses/b0xx-ahk.txt](licenses/b0xx-ahk.txt).
The default key positions come from the same project's `hotkeys.ini`.
See [keyboard behavior and scope](docs/KEYBOARD_LAYOUTS.md).

## Additional tracked-source origins

`src/gameplay_fres.h` attributes its reciprocal-estimate table and integer steps
to Andrew Church's `calc_fres` hardware test at
[ppc750cl.s](https://achurch.org/cpu-tests/ppc750cl.s), described by the source
header as public domain. The September 20 first-pass inventory could not retrieve
that URL (HTTP 403); a pinned source digest and the actual dedication still need
to be retained. This origin is separate from Aurora, Dolphin and Melee.

The player audio replacement covers the resampler and coefficient generator.
Other native files retain original-source translations: for example,
`src/gameplay_audio_reverb.c` translates the original AXFX `HandleReverb`, and
`src/gameplay_ps_math.c` preserves original paired-single instruction arithmetic.
Original implementation includes, generated schemas and source-derived tables
are listed in the [source/license inventory](docs/SOURCE_LICENSE_INVENTORY.md).
Do not infer that all native code or audio is newly independent because those
two audio implementations were replaced.

The standalone `reference-capture/controller-probe/` target imports SDL3 and
libusb static archives from the pinned reference build. The pinned Dolphin
submodules identify SDL `5848e584a1b606de26e3dbd1c7e4ecbc34f807a6` (permissive
SDL notice) and libusb `15a7ebb4d426c5ce196684347d2b7cafad862626`
(LGPL-2.1-or-later). Their exact notice sources and remaining source/manifest
correspondence work are recorded in the [inventory](docs/SOURCE_LICENSE_INVENTORY.md).
The browser runtime inventory does not establish that native probe binary's
distribution requirements.

## Additional development-runtime release findings

The September 2026 launch audit identified the audio adaptations described
above. The following upstream findings still apply:

- `cmake/FighterRuntime.cmake` also compiles recovered original platform code
  from Melee's `extern/dolphin`: `AXAlloc.c`, `AXVPB.c`, `AXCL.c`, `AXAux.c`,
  `axfx.c`, `delay.c` and `reverb_std.c`. That directory name refers to the
  original platform SDK; it must not be confused with the independently authored
  Dolphin Emulator project. No per-file license header was identified in these
  files. The Melee patch changes several of them. Their rights remain unresolved.
- The pinned Melee tree has tool-specific licenses (including AGPL/GPL/MIT
  notices), which do not supply a license for the recovered game as a whole.

The audited Aurora external build declarations name these dependencies. Exact
linked inclusion and full notice bundling must be regenerated for a frozen
playable target; this table is a source inventory, not an executable SBOM.

| Component/version | Upstream license reviewed |
| --- | --- |
| Aurora `749d6ee7a22bdfab78c8ece9047bca5d79aa72ca` | [MIT](https://github.com/encounter/aurora/blob/749d6ee7a22bdfab78c8ece9047bca5d79aa72ca/LICENSE) |
| Abseil 20240722.0 | [Apache-2.0](https://github.com/abseil/abseil-cpp/blob/20240722.0/LICENSE) |
| SDL 3.4.10 | [zlib-style](https://github.com/libsdl-org/SDL/blob/release-3.4.10/LICENSE.txt) |
| xxHash 0.8.3 | [BSD-2-Clause](https://github.com/Cyan4973/xxHash/blob/v0.8.3/LICENSE) |
| fmt 12.1.0 | [MIT](https://github.com/fmtlib/fmt/blob/12.1.0/LICENSE) |
| zlib-ng 2.3.3 | [zlib](https://github.com/zlib-ng/zlib-ng/blob/2.3.3/LICENSE.md) |
| libpng 1.6.58 | [libpng License 2](https://github.com/pnggroup/libpng/blob/v1.6.58/LICENSE) |
| FreeType 2.14.3 | [FTL or GPLv2](https://github.com/freetype/freetype/tree/0a0221a1347e2f1e07c395263540026e9a0aa7c7/docs); build fetches a hash-pinned third-party mirror, whose provenance must be retained |
| ImGui 1.91.9b-docking | [MIT](https://github.com/ocornut/imgui/blob/v1.91.9b-docking/LICENSE.txt) |
| SQLite 3.51.3 amalgamation | [Public-domain policy](https://sqlite.org/copyright.html), subject to jurisdiction review |
| Tracy `6789e7d6f9a65ec98926b602097a33a9676d2606` | [BSD-3-Clause](https://github.com/wolfpld/tracy/blob/6789e7d6f9a65ec98926b602097a33a9676d2606/LICENSE) |

Emsdk's pinned root is MIT. Emscripten 6.0.9 carries MIT/NCSA and component
notices. Its `emdawnwebgpu` port pins package `v20260423.175430` and a SHA-512 in
`tools/ports/emdawnwebgpu.py`; its declared notices include BSD-3-Clause and
Emscripten MIT/NCSA. Preserve that port's actual notices in any future distribution.
CMake/Ninja and Slippi JS are build/reference tools, not public-shell assets.
No root license is chosen here and no recovered or upstream code is relicensed.
