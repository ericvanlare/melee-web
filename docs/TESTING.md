# Validation

Run `python3 -m unittest discover -s tests -v` for setup and real HTTP tests.
Run `python3 scripts/build.py` for the actual WebAssembly link.

## Browser smoke test

1. Start `python3 scripts/serve.py --directory build/browser` and open
   `http://127.0.0.1:8787` in a desktop browser with WebGPU enabled by default.
2. Check that the page renders a three-color triangle and increments frames.
3. Inspect the page log and browser console. Treat a WebGPU validation error,
   uncaught exception, or abort as failure even if the frame counter advances.
4. Reload once and check initialization again. Hide and restore the tab; samples
   should reset and rendering should resume without including the hidden interval.
5. Record OS, hardware/browser identification available from the runtime, build
   type, observed duration and any errors in STATUS.md. Do not convert this
   trivial draw's CPU submission time into a claim about full-game performance.

This currently has no automated GPU screenshot comparison. The CI workflow
checks tests and compilation, while the smoke test exercises the real browser.
Add a reference-image test when the first real asset scene is stable; future
gameplay tests must also compare simulation state, not only pictures.
