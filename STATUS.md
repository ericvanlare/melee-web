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
- Ordinary rigid joint trees, including original HSD Euler transforms and
  classical/nonclassical scale inheritance. Static view matrices are validated
  and prepared once when a scene is loaded.
- Typed texture/image/palette/LOD/material decoding and checked POS/NRM/TEX0–7
  arrays. Original MObj/TObj expression compilation, TEV, diffuse/specular lighting
  and reflection setup execute against owned static HSD objects. Original tiled
  bytes go to Aurora unchanged; SDK texture identities are stable across frames
  and material instances are shared by descriptor within each scene.
- Original inverse-transpose normal transforms and camera/light routines with
  an authored inspection camera and light rig. Joint and normal matrices are
  prepared once; fitting the model changes projection, not lighting space.
- A browser input boundary using SDL/Aurora PAD, explicit keyboard fallback,
  physical-port priority, immediate focus/visibility neutralization and separate
  raw versus PADClamp diagnostic samples.
- A batch CPU asset checker that reports supported roots and exact rejection
  reasons as JSON lines, so the same parser can check an expanding local corpus.
- Diagnostic page with errors and rolling CPU/frame-interval measurements.
- Loopback server with WebAssembly MIME type and cross-origin isolation headers.
- Setup, server, disc, DAT, model and original-HSD call-trace tests; a compile-only
  GitHub Actions workflow.

## Validation record

Verified locally on **2026-09-07**, from the standalone `~/Workspace/melee-web`
repository on Apple Silicon macOS:

- **94 tests passed**, covering setup, real loopback HTTP, synthetic disc/archive
  fixtures, joint/UV/material/texture validation, the input adapter contract, and
  Wasm tests of original HSD polygon, transform and material code. Material traces
  verify fractional blend constants, reflection texture assignment, palette/filter
  handling, stable SDK identities across frames and complete expression freeing.
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
  decoding and original HSD primitive dispatch; full game-scene loading remains
  to be integrated.
- Imported `TyHarise.dat` and visibly rendered the textured fan: **1 joint,
  2 meshes, 2 CMPR textures, 3 primitive packets and 147 submitted vertices**.
  The browser warning/error console was empty. Both fan and bullseye passed again
  after replacing the earlier material approximation with original HSD expressions.
- Imported `TyBacket.dat` and visibly rendered the reflective bucket and transformed
  handle: **2 joints, 8 meshes, 11 texture objects, 18 packets and 408 vertices**.
  More than 2,000 frames were observed with an empty captured warning/error console.
  This exercises real layered/specular material metadata and a nonidentity child
  joint through original HSD code. It uses authored inspection lighting, so it is
  not evidence of pixel equivalence to the game's camera/light setup.
- Joint tests exercise Euler rotation order, multigeneration scale inheritance,
  sibling parentage, shared geometry and inverse-transpose normal matrices.
- Browser keyboard presses reached the real SDL/Aurora PAD provider: D recorded
  raw stick `[127, 0]` and clamped `[72, 0]`; Q recorded trigger 255 and clamped
  150. Releasing keys returned the current sample to zero, and focusing a page
  control paused input. A retained last-activity diagnostic distinguishes these
  observed samples from current held state. Physical devices remain untested.
- Replacing the bucket with the fan, rejecting Mario's currently unsupported
  skeletal costume, and importing the bucket again all completed without browser
  warnings/errors. Tab moved focus from the canvas to the keyboard-control
  checkbox and paused input as intended.
- An independent review checked command execution, the C++/WGSL uniform layout,
  alignment, uploads and browser waits. Runtime inspection caught and resolved
  missing FIFO inline processing and missing `GXInit` in the harness.

Known build warnings are upstream Abseil's deprecated Emscripten version macros,
an unused HSD inline helper's integer `fabs` call, original TObj indexed-format
switch cases against Aurora's texture enum, and limited post-link
optimization while preserving DWARF. They are not suppressed globally.

This is a functional graphics smoke test, **not a performance benchmark**. GPU
identity is privacy-limited in this browser. No release-performance, background
tab recovery, physical-controller, audio, game-state fidelity, native-host or Linux
validation has been completed. CI has not run on GitHub because this repository
has not been published. Follow [docs/TESTING.md](docs/TESTING.md) for repeatable
checks as coverage expands.

## Next acceptance gate

Move from rigid inspection assets toward a real fighter/stage scene. Prioritize
typed joint/envelope and animation loading plus original HSD evaluation, using
batch rejection evidence to select representative assets and shared boundaries.
The selected next gate is Mario's complete neutral costume in its bind pose,
followed by an idle animation; see [the corpus findings and work boundaries](docs/NEXT_PHASE.md).

The current decoder handles ordinary joint SRT, opaque constant/diffuse/specular
materials and up to eight ordinary UV/reflection texture layers per material.
It rejects external links, special joint modes, custom material classes/TEV/PE,
transparency, nested GX commands, skinning and animation. Referenced-region bounds
are conservative, not inferred allocation sizes. CPU parser acceptance and GPU
render acceptance remain separate evidence.

Then integrate the scheduling, disc I/O, audio and complete scene services
needed for a complete versus loop. Track absent services as absent; a silent
stub must never turn an incomplete path into a passing milestone.

The full acceptance sequence is in [docs/ROADMAP.md](docs/ROADMAP.md), with
portability findings in [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md).
