# Build, play and inspect

This page keeps the reproducible local commands out of the short developer
entry. They operate on local inputs only. A successful build or inspection is
not gameplay, retail-equivalence or performance evidence.

## Bootstrap and build

The pinned environment is Python 3, Git, CMake/Ninja from the project virtual
environment, and desktop Chromium with WebGPU. Bootstrap downloads the exact
Aurora, Melee and Emscripten revisions into `.deps/` and installs build tools in
`.venv/`; it does not modify an upstream checkout that has unexpected changes.

```sh
python3 scripts/bootstrap.py
python3 scripts/build.py
python3 scripts/build.py --target gameplay
python3 scripts/build.py --target graphics
python3 scripts/check_gameplay.py
python3 scripts/check_gameplay.py --common assets-local/PlCo.dat --stage assets-local/GrNLa.dat --stage-kind 37
python3 -m unittest discover -s tests -v
```

`--stage-kind 37` is Final Destination's original `GrKind`; it is distinct from
viewer map entry 3. The gameplay runner reports source-consumer readiness, not a
fully initialized fighter or stage. Keep the extracted data under ignored
`assets-local/` and send generated reports to ignored `work/` or `build/`.

## Run the local player

```sh
python3 scripts/serve.py --directory build/browser
```

Open `http://127.0.0.1:8787/runtime.html` and choose an owned, unmodified USA
revision 1.02 Melee ISO, GCM or CISO. **Open character select** enters the
original in-game CSS, which is the canonical player path. `native-menu.html`
redirects to it; `viewer.html` remains the separate asset inspector. Click the
canvas for keyboard input. Physical controller support remains a separate gate.

The browser reads required disc ranges locally and does not upload or persist
game content. RVZ is unsupported. **Unload** releases the source world and saves
only the optional renderer cache. Diagnostic input controls do not establish
original timing or gameplay equivalence.

## Inspect owned assets

The extractor reads the disc without modifying it and refuses unsafe output
paths. Use a new output path under ignored `assets-local/`:

```sh
python3 scripts/extract_disc_file.py /path/to/game.ciso TyTarget.dat --output assets-local/TyTarget.dat
python3 scripts/extract_disc_file.py /path/to/game.ciso TyHarise.dat --output assets-local/TyHarise.dat
python3 scripts/extract_disc_file.py /path/to/game.ciso TyBacket.dat --output assets-local/TyBacket.dat
```

Choose `TyTarget.dat`, `TyHarise.dat` or `TyBacket.dat` in `viewer.html` for the
bullseye, fan or reflective bucket fixtures. The inspector uses authored camera
and lights and original HSD evaluation; it does not establish the gameplay
camera, effects, collision or pixel equivalence.

For a fighter animation fixture, extract the model, metadata, common data and
animation container:

```sh
python3 scripts/extract_disc_file.py /path/to/game.ciso PlMrNr.dat --output assets-local/PlMrNr.dat
python3 scripts/extract_disc_file.py /path/to/game.ciso PlMr.dat --output assets-local/PlMr.dat
python3 scripts/extract_disc_file.py /path/to/game.ciso PlCo.dat --output assets-local/PlCo.dat
python3 scripts/extract_disc_file.py /path/to/game.ciso PlMrAJ.dat --output assets-local/PlMrAJ.dat
```

Select `PlMrNr.dat` as the model, `PlMr.dat` as fighter metadata, `PlCo.dat` as
common data and `PlMrAJ.dat` as the animation container. Select an action
explicitly, such as Wait1 (2) or WalkSlow (7), then press Play. Fox uses
`PlFxNr.dat`, `PlFx.dat` and `PlFxAJ.dat`; common data is reusable in the tab.
Animation playback is a 60 Hz inspection path, not gameplay.

For Final Destination, extract `GrNLa.dat`, load it as the model, select stage
entry 3 and **Opaque only**. The batch checker uses the same explicit entry:

```sh
python3 scripts/extract_disc_file.py /path/to/game.ciso GrNLa.dat --output assets-local/GrNLa.dat
python3 scripts/check_assets.py assets-local/GrNLa.dat --stage-entry 3 --opaque
python3 scripts/check_assets.py assets-local/
```

The batch checker measures parser coverage and reports exact rejection reasons;
it does not verify browser rendering or stage callbacks. The stage entry and
opaque pass must remain explicit so unsupported geometry is not silently treated
as supported.

## Public package

The public alpha is a separate Release target with audio deliberately disabled:

```sh
python3 scripts/build.py --target runtime-public --configuration Release
python3 scripts/build_public.py --profile player --runtime-dir build/browser-public-release \
  --output /path/to/public-output --manifest /path/to/public-output.manifest.json \
  --environment preview
python3 scripts/audit_public.py --profile player --mode preview \
  --output /path/to/public-output --manifest /path/to/public-output.manifest.json
```

Use explicit output and environment arguments for packaging. Read [public release
review](PUBLIC_RELEASE_REVIEW.md) and [public deployment](PUBLIC_DEPLOYMENT.md)
before staging or publishing. Packaging identity and HTTP checks do not widen
gameplay acceptance.
