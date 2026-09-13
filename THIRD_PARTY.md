# Third-party provenance

Dependencies are fetched separately and pinned in dependencies.lock.json.

The public shell built by `scripts/build_public.py` ships none of the runtime
components below. It contains only original HTML/CSS and a fullscreen script,
uses device-provided system fonts and has no shipped package dependencies.
Its public notices are in `web/public/notices.html`. This exclusion is not
clearance for distributing a future playable build; see
[the release review](docs/PUBLIC_RELEASE_REVIEW.md).

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
actual dependency graph before a playable public release. None is included in
the independent public shell.

No Nintendo disc images, extracted assets, audio, textures or game executables
are tracked. This repository does not grant rights to upstream code or game data.

## Free DSP coefficient generator

`web/dsp-coefficients.mjs` adapts Dolphin’s free replacement coefficient
generator at revision `a2efdf1197be8132674b90fe9cf4761df39752ed`:
https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/docs/DSP/free_dsp_rom/generate_coefs.py

Retain GPL-2.0-or-later attribution and upstream notices. The generated 4096-byte
replacement is checked against SHA-256
`d7741279c2e8ec5c5fb318f8fbdd6de6bf583520d288e836a5383233a4238179`.
It is not a Nintendo hardware ROM dump and is only approximately equivalent.
The source generator is included; no generated coefficient binary is tracked.

## B0XX-style keyboard mapping

`src/boxx_input.h` adapts the coordinate tables and directional state rules from
[agirardeau/b0xx-ahk](https://github.com/agirardeau/b0xx-ahk/tree/7c070f8e0f135c8108cfb0af9a37dc6809070b15)
at commit `7c070f8e0f135c8108cfb0af9a37dc6809070b15`.
The MIT notice is retained in [licenses/b0xx-ahk.txt](licenses/b0xx-ahk.txt).
The default key positions come from the same project's `hotkeys.ini`.
See [keyboard behavior and scope](docs/KEYBOARD_LAYOUTS.md).

## Additional development-runtime release findings

The September 2026 launch audit inspected the pinned source trees and build
configuration. The following are **not shipped in the public shell**:

- `src/gameplay_audio_resample.c/.h` adapts Dolphin Emulator audio code and
  retains GPL-2.0-or-later notices; see `src/gameplay_audio_provenance.md` and
  [Dolphin's pinned source](https://github.com/dolphin-emu/dolphin/tree/a2efdf1197be8132674b90fe9cf4761df39752ed).
  The combined runtime's corresponding-source and license obligations remain
  a release review item, separate from Nintendo rights.
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
