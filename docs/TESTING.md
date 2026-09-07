# Validation

Run `python3 -m unittest discover -s tests -v` for setup, real HTTP, synthetic disc,
DAT, texture and model tests. Native parser tests require a C++20 compiler. The
original HSD polygon/transform tests use the pinned project SDK and skip if it
is not installed. CI runs them again after installing the SDK.
Run `python3 scripts/build.py` for the actual WebAssembly link.

## Browser smoke test

1. Start `python3 scripts/serve.py --directory build/browser` and open
   `http://127.0.0.1:8787` in a desktop browser with WebGPU enabled by default.
2. Check that the page renders a three-color triangle and increments frames.
3. Inspect the page log and browser console. Treat a WebGPU validation error,
   uncaught exception, or abort as failure even if the frame counter advances.
4. Extract `TyTarget.dat` as described in README.md and choose it in the page.
   Expect a red/gray bullseye, 2 meshes, 11 primitive packets and 676 submitted
   vertices. It is an unlit diffuse preview. No game bytes are included in tests.
   Repeat with `TyHarise.dat`: expect the cream fan with red handle, 2 textures,
   3 primitive packets and 147 vertices. Loading an unsupported asset should
   replace the old model with a clear rejection and allow another import.
5. Reload once and check initialization again. Hide and restore the tab; samples
   should reset and rendering should resume without including the hidden interval.
6. Record OS, hardware/browser identification available from the runtime, build
   type, observed duration and any errors in STATUS.md. Do not convert this
   trivial draw's CPU submission time into a claim about full-game performance.

This currently has no automated GPU screenshot comparison. The CI workflow
checks tests and compilation, while the smoke test exercises the real browser.
Add a reference-image test when the first real asset scene is stable; future
gameplay tests must also compare simulation state, not only pictures.

## Batch asset coverage

`python3 scripts/check_assets.py assets-local/` builds the actual CPU parser once
and emits JSON lines for each public root. Supported roots report joint, mesh,
texture and primitive counts; other roots report their exact rejection. Exit
status is nonzero if any input/root is rejected. Redirect reports into ignored
`work/` or `build/` when comparing a local corpus across changes. A parser pass
does not validate source HSD transforms, GPU output or game-scene behavior.
