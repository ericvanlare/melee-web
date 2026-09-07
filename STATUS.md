# Current status

This is an early source-port feasibility repository. It does **not** boot Melee,
load a game scene, or run a match. It contains no emulator and no game assets.

## Implemented

- Pinned Melee, Aurora, and Emscripten revisions with a project-local toolchain.
- Explicit Aurora browser patch: WebGPU canvas surface, browser event-loop
  integration, inline execution of renderer work, and uniform-buffer replacement
  for desktop-only draw immediates.
- A synthetic GX triangle using the original Melee HSD render-state functions.
- Local ISO/CISO extraction and a bounded, typed big-endian DAT reader.
- A real rigid DAT model through original HSD `PObjDispSimplePrimitive`, with
  explicit indexed-array lengths and unchanged big-endian geometry bytes.
- File-picker import with visible rejection of unsupported model features.
- Diagnostic page with errors and rolling CPU/frame-interval measurements.
- Loopback server with WebAssembly MIME type and cross-origin isolation headers.
- Setup, server, disc, DAT, model and original-HSD call-trace tests; a compile-only
  GitHub Actions workflow.

## Validation record

Verified locally on **2026-09-07**, from the standalone `~/Workspace/melee-web`
repository on Apple Silicon macOS:

- **65 tests passed**, covering setup, real loopback HTTP, synthetic disc/archive
  fixtures, model validation and a Wasm trace of original HSD polygon calls.
- Updated Melee to `b43912cc78606f96c9569f5d6229bc9d7e265ea5`, including the final
  snapshot-function match/link change. The retained HSD code is unchanged by this update.
- Project-local bootstrap succeeded; the Emscripten **6.0.9** / CMake **3.31.6**
  `RelWithDebInfo` build compiled and linked the complete graphics probe.
- The Codex in-app Chromium browser visibly rendered the expected red/green/blue
  gradient triangle through HSD → GX → WebGPU. The runtime reported a 1280×960
  framebuffer at scale 2; more than 1,300 frames were observed on a fresh page.
- Reload and a separate fresh tab both initialized successfully. The fresh
  tab's captured warning/error console was empty. Its page log contained no
  WebGPU validation failures or aborts. The message about an absent bundled
  initial pipeline cache is expected; no cache is bundled with this probe.
- Imported the locally extracted `TyTarget.dat` and visibly rendered its red/gray
  bullseye: **2 meshes, 11 triangle-strip packets, 676 submitted vertices**. The
  original bytes remain outside Git. The import uses schema-aware descriptor
  decoding and original HSD primitive dispatch; full HSD joint loading and
  material lighting are not integrated.
- An independent review checked command execution, the C++/WGSL uniform layout,
  alignment, uploads and browser waits. Runtime inspection caught and resolved
  missing FIFO inline processing and missing `GXInit` in the harness.

Known build warnings are upstream Abseil's deprecated Emscripten version macros,
an unused HSD inline helper's integer `fabs` call, and limited post-link
optimization while preserving DWARF. They are not suppressed globally.

This is a functional graphics smoke test, **not a performance benchmark**. GPU
identity is privacy-limited in this browser. No release-performance, background
tab recovery, input, audio, game-state fidelity, native-host or Linux
validation has been completed. CI has not run on GitHub because this repository
has not been published. Follow [docs/TESTING.md](docs/TESTING.md) for repeatable
checks as coverage expands.

## Next acceptance gate

Extend the demonstrated rigid polygon path to joint hierarchies and textured
materials, then render a representative static scene. The current decoder
supports only an identity joint, opaque diffuse materials and indexed POS/NRM
surface primitives. It rejects external archive links, nested GX commands,
skinning and animation. Referenced-region limits bound array/display-list reads;
they are conservative checks, not inferred allocation sizes.

After that, integrate the scheduling, disc I/O, input, audio and scene services
needed for a complete versus loop. Track absent services as absent; a silent
stub must never turn an incomplete path into a passing milestone.

The full acceptance sequence is in [docs/ROADMAP.md](docs/ROADMAP.md), with
portability findings in [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md).
