# Playable runtime notice source

Audit date: September 12, 2026. This is the source inventory for a WebMelee
player artifact. It is not a grant of rights in the recovered game or platform
code, and it is not a complete generated SBOM. Before publishing a frozen
artifact, compare this inventory with the linked binary's actual symbols and
the dependency lock, then expose the applicable full texts and corresponding
source under the same release.

## Artifact boundary

The Release `gameplay_public` target links the recovered Melee/HSD source tree,
the original platform source under its `extern/dolphin` directory, project
bridges, Aurora and its browser dependency graph. The public loader also ships
JavaScript modules for bounded local disc reads, audio, and input, plus the
Emscripten-generated Wasm JavaScript/Wasm/data files. No retail disc image or
extracted game archive is a release input.

The target preloads `web/initial_pipeline_cache.db.gz.b64` as
`/initial_pipeline_cache.db`. The current decoded seed is 2,113,536 bytes with
SHA-256
`cdf157ee0f1850884f07a71165fd2192177acb23c2c777313b719e70ea67546f` and has
one Aurora schema row, one shader row and 507 pipeline rows. The source was
captured from Aurora pipeline descriptors observed while rendering development
game routes (the seed update is commit
`9c90e52569ef66409d5a4ca7431548095bc14b6b`). Inspection found no raw texture,
model, audio or disc bytes and no filename fields in the SQLite schema, but
that observation does not settle whether generated descriptors are covered by
any upstream or game-related rights. Treat the seed as a distributed,
game-derived renderer artifact for the rights and notice review.

The public browser source does not load `runtime-cache.js`, mount IDBFS, or
write the native `/melee-render-cache` path to browser persistence. Aurora's
cache path is therefore page-local in this profile. A browser or graphics
driver may maintain its own cache outside the application's control.

The exact checked-in notice files have these SHA-256 values:

| File | SHA-256 |
| --- | --- |
| [`runtime-third-party.txt`](runtime-third-party.txt) | `efd8930e9fa18def4399ac66b27ed0091e4e80e152cc37b0a7624706041281cd` |
| [`dolphin-gpl-2.0-or-later.txt`](dolphin-gpl-2.0-or-later.txt) | `edaef632cbb643e4e7a221717a6c441a4c1a7c918e6e4d56debc3d8739b233f6` |
| [`aurora-mit.txt`](aurora-mit.txt) | `40c03cee04cda570d1029b85b6c2ee7ba78431ab1a754b9d912091d73b8ecb18` |
| [`emscripten-mit-ncsa.txt`](emscripten-mit-ncsa.txt) | `620a78084fc7ca97c0b5dea9abf891f3ffcadfdbf305276f099c9c4e12fc1d86` |
| [`emsdk-mit.txt`](emsdk-mit.txt) | `99d9a9616fbde3f5ee22a71d8645799a8522d48526130c5ba6dc27ad15ce01f1` |
| [`../../licenses/b0xx-ahk.txt`](../../licenses/b0xx-ahk.txt) | `fc4f057a1c694c97e11592c7214ab6d2a831128c7e0995ec8767d91244612470` |

[`runtime-third-party.txt`](runtime-third-party.txt) is the user-facing aggregate
notice file. It copies the applicable full texts from the actual pinned local
Aurora, Emscripten, emsdk and `emdawnwebgpu` dependency trees, plus the GPL and
MIT notices for the reviewed runtime adaptations. It is a conservative
configured-dependency inventory and deliberately is not an exact post-link or
post-dead-code-elimination SBOM. It does not provide corresponding source for
the GPL-covered adaptation or resolve the distribution basis for recovered
Melee/HSD and original platform code.

## Components and notice sources

| Component in the player build | Pinned provenance / role | Notice source and current issue |
| --- | --- | --- |
| Recovered `doldecomp/melee` code | `b43912cc78606f96c9569f5d6229bc9d7e265ea5`; game/HSD code reached through the runtime | No repository-wide license was identified. The aggregate intentionally does not assign a permissive license. Distribution basis remains unresolved. |
| Melee `extern/dolphin` source | Original platform AX/AXFX units compiled by `cmake/FighterRuntime.cmake`; this directory name does not identify Dolphin Emulator | No per-file license header was found in the audited units. Preserve provenance; rights remain unresolved. |
| Dolphin-derived audio code | `src/gameplay_audio_resample.c/.h` and the free DSP coefficient generator; adapted from Dolphin revision `a2efdf1197be8132674b90fe9cf4761df39752ed` | GPL-2.0-or-later text is included in [`runtime-third-party.txt`](runtime-third-party.txt) and [`dolphin-gpl-2.0-or-later.txt`](dolphin-gpl-2.0-or-later.txt). Source: [Dolphin AXVoice.h](https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/Source/Core/Core/HW/DSPHLE/UCodes/AXVoice.h). Corresponding-source and combined-program obligations remain unresolved. |
| Aurora | `encounter/aurora` commit `749d6ee7a22bdfab78c8ece9047bca5d79aa72ca`; renderer/platform layer | The actual `.deps/aurora/LICENSE` text is included in [`runtime-third-party.txt`](runtime-third-party.txt), with the reviewed copy in [`aurora-mit.txt`](aurora-mit.txt). Upstream [LICENSE](https://github.com/encounter/aurora/blob/749d6ee7a22bdfab78c8ece9047bca5d79aa72ca). |
| B0XX-derived keyboard mapping | `agirardeau/b0xx-ahk` commit `7c070f8e0f135c8108cfb0af9a37dc6809070b15`; coordinate/state mapping in `src/boxx_input.h` | The MIT text is included in [`runtime-third-party.txt`](runtime-third-party.txt) and [`../../licenses/b0xx-ahk.txt`](../../licenses/b0xx-ahk.txt). Retain attribution and source revision. |
| Emscripten runtime/toolchain | `emscripten-core/emsdk` commit `5eb0bde7585670252e8ba05e9d361627bffd08b5`; Emscripten 6.0.9, compiler revision `4e4223852a0835923411059a3929907d7df1232e` | Actual `.deps/emsdk/upstream/emscripten/LICENSE`, system runtime notices and emsdk notice are included in [`runtime-third-party.txt`](runtime-third-party.txt), with the reviewed copies in [`emscripten-mit-ncsa.txt`](emscripten-mit-ncsa.txt) and [`emsdk-mit.txt`](emsdk-mit.txt). |
| Dawn / `emdawnwebgpu` | Emscripten port `v20260423.175430`, Dawn revision `31e25af254ab572c77054edec4946d2244e184dd` | The actual package Emscripten-side and Dawn/Tint BSD-3-Clause notices are included in [`runtime-third-party.txt`](runtime-third-party.txt). Package source correspondence remains pending; no partial hash is treated as a package digest. |
| SDL 3.4.10 | Vendor archive `release-3.4.10`, SHA-256 `0dc11d980ba17250200718fa4e28011da293f27ed92f92203afffe396811f307`; Aurora patch hooks configured | Actual `build/browser-release/_deps/sdl-src/LICENSE.txt` is included in [`runtime-third-party.txt`](runtime-third-party.txt). Patched-source correspondence remains pending. |
| Abseil 20240722.0 | Archive SHA-256 `f50e5ac311a81382da7fa75b97310e4b9006474f9560ac46f54a9967f07d4ae3` | Actual `build/browser-release/_deps/abseil-cpp-src/LICENSE` is included in [`runtime-third-party.txt`](runtime-third-party.txt). |
| fmt 12.1.0 | Archive SHA-256 `ea7de4299689e12b6dddd392f9896f08fb0777ac7168897a244a6d6085043fea` | Actual `build/browser-release/_deps/fmt-src/LICENSE` is included in [`runtime-third-party.txt`](runtime-third-party.txt). |
| xxHash 0.8.3 | Archive SHA-256 `aae608dfe8213dfd05d909a57718ef82f30722c392344583d3f39050c7f29a80` | Actual `build/browser-release/_deps/xxhash-src/LICENSE` is included in [`runtime-third-party.txt`](runtime-third-party.txt). |
| zlib-ng 2.3.3 and libpng 1.6.58 | Pinned archives; zlib-ng is forced for libpng and patched by Aurora | Actual zlib-ng and libpng license files are included in [`runtime-third-party.txt`](runtime-third-party.txt). The zlib adaptation is recorded; source correspondence remains pending. |
| FreeType 2.14.3 and Dear ImGui 1.91.9b-docking | Pinned FreeType mirror archive and ImGui archive; ImGui source embeds ProggyClean.ttf | Actual FreeType `LICENSE.TXT`, FTL and GPLv2 notices, ImGui MIT text, and the embedded ProggyClean attribution are included in [`runtime-third-party.txt`](runtime-third-party.txt). Mirror and generated-backend provenance remain recorded. |
| SQLite 3.51.3 and Tracy | SQLite amalgamation SHA-256 `acb1e6f5d832484bf6d32b681e858c38add8b2acdfd42ac5df24b8afb46552b4`; Tracy commit `6789e7d6f9a65ec98926b602097a33a9676d2606` | The exact SQLite public-domain blessing from the linked amalgamation and Tracy license are included in [`runtime-third-party.txt`](runtime-third-party.txt). Tracy is included conservatively while `TRACY_ENABLE=OFF`. |

The aggregate is notice delivery for the reviewed dependency and adaptation
sources. It does not grant Nintendo or other rights in recovered Melee code,
original SDK code, disc contents, or the generated seed. It does not replace a
binary-level SBOM, corresponding-source delivery, or counsel review.

## Required release follow-up

1. Generate an artifact-level SBOM from the exact `gameplay_public.js`, Wasm and
   data files, including the selected Emscripten/Dawn port and all static
   libraries retained after dead-code elimination.
2. The checked-in [`runtime-third-party.txt`](licenses/runtime-third-party.txt)
   now delivers the reviewed full license and attribution texts for the GPL
   adaptation, Aurora, B0XX, Emscripten/emsdk, `emdawnwebgpu` and the configured
   Aurora dependency graph. Generate the artifact-level SBOM and complete
   corresponding-source delivery, including the GPL-covered adaptation and any
   patched dependencies, before treating this item as closed.
3. Resolve the distribution basis for the recovered Melee/HSD and original SDK
   sources and decide whether the generated pipeline seed can be distributed.
   Do not infer permission from a public repository, DOL hash, local disc
   processing, or a non-affiliation statement.
