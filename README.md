# Melee Web

A source-port feasibility project for running vanilla Super Smash Bros. Melee
in a desktop browser using WebAssembly and WebGPU.

**This is not a playable game yet.** The initial target is a synthetic graphics
probe using original Melee HSD render-state functions and Aurora's GX renderer.
It verifies an integration boundary; it does not establish game-scene fidelity,
audio, physics, controller latency, or full-game performance.

## Build and inspect

Initial reference environment: Apple Silicon macOS, Python 3, Git, and desktop
Chromium with WebGPU. No global SDK installation or shell-profile edits required.
Other build hosts have not yet been validated.

```sh
python3 scripts/bootstrap.py
python3 scripts/build.py
python3 -m unittest discover -s tests -v
python3 scripts/serve.py --directory build/browser
```

Open http://127.0.0.1:8787. Keep the tab visible for timing measurements. The
server binds to loopback and supplies cross-origin isolation headers. The probe
requires no game image and contains no extracted game assets.

Bootstrap downloads pinned Aurora, Melee, and Emscripten sources into `.deps/`
and CMake/Ninja into `.venv/`. The build downloads Aurora's transitive dependencies;
their versions are controlled by the pinned Aurora tree and our patch. See
[dependency notes](docs/DEPENDENCIES.md) for limits of reproducibility.

## Architecture and scope

- Melee's recovered C logic will compile directly to WebAssembly.
- Aurora supplies a source-level GX-to-WebGPU implementation.
- Browser-specific scheduling, audio and asset conversion are explicit port work.
- The original GameCube build remains the behavioral reference.
- User-supplied disc content will stay local; asset import is not implemented yet.

See [current evidence and limitations](STATUS.md),
[milestones](docs/ROADMAP.md), and [architecture decisions](docs/ARCHITECTURE.md).

This project is independent of Nintendo, doldecomp and Aurora. Dependency licenses
remain attached to their upstream sources; see [third-party notices](THIRD_PARTY.md).
