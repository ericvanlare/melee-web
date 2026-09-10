# Third-party provenance

Dependencies are fetched separately and pinned in dependencies.lock.json.

| Source | Role | License/notice |
| --- | --- | --- |
| https://github.com/doldecomp/melee | Recovered C; initial probe compiles HSD state.c | No repository-wide license located in audited root; retain all source notices, do not relabel as our original code |
| https://github.com/encounter/aurora | GX renderer and platform compatibility | MIT; upstream LICENSE retained in .deps/aurora |
| https://github.com/emscripten-core/emsdk | Local Wasm SDK installation | Retain upstream notices and component licenses |
| https://github.com/project-slippi/slippi-js | Replay-format authority and conformance oracle; not linked into gameplay source | LGPL-3.0-or-later; pin version and retain upstream license when distributed |
| https://github.com/sh1ftmaker/dusk-wasm | Browser integration research/reference | Not a linked dependency; identify any adapted code in patches/README.md |

Aurora fetches further dependencies (SDL, Dawn bindings, Abseil, fmt, ImGui,
texture/image libraries, SQLite and Tracy). Their source notices remain with each
download. License attribution for a distributable bundle must be assembled from the
actual dependency graph before a public release. No public release is made here.

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
