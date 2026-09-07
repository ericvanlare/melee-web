# Third-party provenance

Dependencies are fetched separately and pinned in dependencies.lock.json.

| Source | Role | License/notice |
| --- | --- | --- |
| https://github.com/doldecomp/melee | Recovered C; initial probe compiles HSD state.c | No repository-wide license located in audited root; retain all source notices, do not relabel as our original code |
| https://github.com/encounter/aurora | GX renderer and platform compatibility | MIT; upstream LICENSE retained in .deps/aurora |
| https://github.com/emscripten-core/emsdk | Local Wasm SDK installation | Retain upstream notices and component licenses |
| https://github.com/sh1ftmaker/dusk-wasm | Browser integration research/reference | Not a linked dependency; identify any adapted code in patches/README.md |

Aurora fetches further dependencies (SDL, Dawn bindings, Abseil, fmt, ImGui,
texture/image libraries, SQLite and Tracy). Their source notices remain with each
download. License attribution for a distributable bundle must be assembled from the
actual dependency graph before a public release. No public release is made here.

No Nintendo disc images, extracted assets, audio, textures or game executables
are tracked. This repository does not grant rights to upstream code or game data.
