# Melee Web

A source-port feasibility project for running vanilla Super Smash Bros. Melee
in a desktop browser using WebAssembly and WebGPU.

**This is not a playable game yet.** The browser renders Melee DAT models
through original HSD transforms, skinning, materials and polygon code, using
Aurora's GX renderer. Mario and Fox can play original idle and walking animations
from their action containers. The viewer also renders Final Destination's opaque
platform geometry, with omitted passes and unapplied stage services reported.
It does not establish complete scene fidelity or full-game performance.

## Build and inspect

Initial reference environment: Apple Silicon macOS, Python 3, Git, and desktop
Chromium with WebGPU. No global SDK installation or shell-profile edits required.
The pinned build and automated checks also pass on Ubuntu 24.04 in GitHub Actions;
real browser graphics are currently checked on macOS.

```sh
python3 scripts/bootstrap.py
python3 scripts/build.py
python3 -m unittest discover -s tests -v
python3 scripts/serve.py --directory build/browser
```

Open http://127.0.0.1:8787. Keep the tab visible for timing measurements. The
server binds to loopback and supplies cross-origin isolation headers. The probe
starts with a synthetic triangle and contains no extracted game assets.

To inspect the first validated model, extract it from your own GALE01 revision 2
ISO or CISO, then choose `assets-local/TyTarget.dat` in the page's file picker:

```sh
python3 scripts/extract_disc_file.py /path/to/game.ciso TyTarget.dat --output assets-local/TyTarget.dat
```

The extractor reads the disc without modifying it. Extracted data stays in ignored
`assets-local/`; the browser reads the selected file locally without uploading it.
Use the same command with `TyHarise.dat` to inspect the textured fan or
`TyBacket.dat` for a reflective bucket with a transformed child joint. Materials
run through Melee's original HSD expression compiler and setup code, with an
authored inspection camera and lights.

To inspect Mario, extract his neutral costume, fighter metadata, common data and
animation container:

```sh
python3 scripts/extract_disc_file.py /path/to/game.ciso PlMrNr.dat --output assets-local/PlMrNr.dat
python3 scripts/extract_disc_file.py /path/to/game.ciso PlMr.dat --output assets-local/PlMr.dat
python3 scripts/extract_disc_file.py /path/to/game.ciso PlCo.dat --output assets-local/PlCo.dat
python3 scripts/extract_disc_file.py /path/to/game.ciso PlMrAJ.dat --output assets-local/PlMrAJ.dat
```

Choose `PlMrNr.dat` as the model, `PlMr.dat` as fighter metadata, `PlCo.dat` as
common data, and `PlMrAJ.dat` as the animation container. Select action 2 (Wait1)
or 7 (WalkSlow), then press Play. Fox uses `PlFxNr.dat`, `PlFx.dat` and
`PlFxAJ.dat`; common data remains reusable in the tab. Identity, part mappings
and animation slices come from checked original source/data tables. Playback
uses original HSD evaluation at 60 Hz; gameplay commands and transitions are
not running.

For Final Destination, extract `GrNLa.dat`, load it as the model, select stage
entry 3 and **Opaque only**. The viewer renders all 13 opaque meshes from that
entry and reports 13 omitted translucent meshes. It uses an inspection camera
and lights; stage animation, effects, collision and callbacks remain unapplied.
The batch checker supports the same explicit selection:

```sh
python3 scripts/check_assets.py assets-local/GrNLa.dat --stage-entry 3 --opaque
```

Enable keyboard controls and click the canvas to inspect the SDL/Aurora PAD input
path. The page lists bindings and current raw/clamped samples; input does not
control the inspected model. Physical controller support uses Aurora's existing
provider and still needs hardware validation.

For a growing local asset corpus, run the actual CPU parser in a batch:

```sh
python3 scripts/check_assets.py assets-local/
```

This emits JSON lines with supported roots/counts or exact rejection reasons.
It measures parser coverage; browser rendering is verified separately.

Bootstrap downloads pinned Aurora, Melee, and Emscripten sources into `.deps/`
and CMake/Ninja into `.venv/`. The build downloads Aurora's transitive dependencies;
their versions are controlled by the pinned Aurora tree and our patch. See
[dependency notes](docs/DEPENDENCIES.md) for limits of reproducibility.

## Architecture and scope

- Melee's recovered C logic will compile directly to WebAssembly.
- Aurora supplies a source-level GX-to-WebGPU implementation.
- Browser-specific scheduling, audio and asset conversion are explicit port work.
- The original GameCube build remains the behavioral reference.
- User-supplied disc content stays local and outside Git.

See [current evidence and limitations](STATUS.md),
[milestones](docs/ROADMAP.md), and [architecture decisions](docs/ARCHITECTURE.md).

This project is independent of Nintendo, doldecomp and Aurora. Dependency licenses
remain attached to their upstream sources; see [third-party notices](THIRD_PARTY.md).
