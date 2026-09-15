# Melee Web

A source-port project for running vanilla Super Smash Bros. Melee
in a desktop browser using WebAssembly and WebGPU.

The first playable goal is an accurate local **Mario-versus-Mario stock match on
Final Destination at 60 fps**, using original gameplay code. Full vanilla Melee
remains the goal. The active first deliverable includes the original in-game
CSS → original SSS → four-stock Mario/FD match → original CSS. See the
[accuracy contract](docs/ACCURACY_CONTRACT.md).
The combined [performance and accuracy playbook](docs/PERFORMANCE_AND_ACCURACY.md)
defines the evidence levels and admission process used for every new fighter,
stage and shared runtime change.
The [bounded hitch-capture loop](docs/HITCH_CAPTURE.md) records every failure
with separate native deadline and browser-gap metrics, a frozen repetition
matrix and optional correlated traces.

**The complete playable-match milestone is still open.** The browser currently
runs two original Mario instances on Final Destination, with source controller
processing, camera, authored stage markers, stage/item rendering, stock/respawn
logic, match outcome and audio transport. Ground/air fireballs survive impact,
restart and unload. An optional IDBFS renderer cache saves compiled pipelines
after native unload and restores them on the next page startup without
persisting game assets. Cold first-use stalls, broader
rendered combat, audible output, physical controllers and full original-game
equivalence still need acceptance checks.

CSS, SSS and match transitions now expose an explicit resource-preparation
phase. It waits for the audio worklet's disabled-state acknowledgement, keeps
source simulation and drawing out of owner construction, and reports
preparation separately from live callbacks. The scoped
[scene-entry profile](work/scene-entry-profile.md) records the measured phases;
it is not cold-cache or full-match performance acceptance.

Typed source-content rows and runtime manifests now include Falco, Fox, Marth,
Battlefield, Yoshi's Story and Dream Land, with focused development traces for
their source identity, costumes, effects and stage data. Their integrated source
match lifecycles cover every admitted costume and stage, representative special
and common actions, dynamic stage objects, and repeated teardown. Release browser
runs selected Fox on Yoshi's Story and Marth on Dream Land through the original
CSS/SSS and rendered the source matches; uninterrupted audio inspection remains
open.
A raw-PAD browser run selected P1 Falco and P2
Mario on Battlefield through the original CSS/SSS and visibly rendered both
fighters, the stage and their distinct four-stock icons. The native loop also
completed this route on both Battlefield and Final Destination. Full ordinary
input, uninterrupted audio, complete browser-loop and retail-reference checks
remain open, so this does not widen the accepted first-deliverable claim.

Dr. Mario and Roy are now enabled as **development candidates** through the
same original CSS/SSS path. Their own source attributes, five costumes each,
articles, effects and voices use the existing Mario/Marth family adapters.
Native mixed matches run in both orientations; retail capture comparison and
cold rendering preparation remain open. See the
[clone port evidence and capture plan](docs/ROY_DR_MARIO_PORT_NOTES.md).

A scoped 102-frame original-game jump comparison passes; this does not establish
full-match equivalence. See [current evidence](STATUS.md),
[the runtime gate](docs/FIGHTER_RUNTIME.md) and
[original-game comparison](docs/ORIGINAL_COMPARISON.md). A separate [retail replay calibration](docs/RETAIL_REPLAY_CAPTURE.md) now matches
240 neutral Mario/Mario Final Destination ticks across two independent retail
captures and the port's declared state fields, RNG and input recipe. The first
Fox/Falco Battlefield Slippi input donor also matches all 686 ticks against two
independent vanilla captures. The same donor also passes visible Release state comparison and cleared-origin/warm
performance checks. A second derived donor now matches all 3,122 ticks of an
original elimination match, including the final drawn state and teardown. See
[the complete-game calibration](docs/COMPLETE_REPLAY_CALIBRATION.md) for its
reference cross-check, timing status and coverage limits. Broad gold-corpus and
content admission remain open. The reusable
[CSS/SSS and match-entry transition gate](docs/TRANSITION_EQUIVALENCE.md) is
implemented. Its pinned Mario/Mario Final Destination replay matches retail at
all nine lifecycle, audio, match-data and RNG boundaries.

## Public player packaging

The public entry in `web/player/` mounts `web/melee-runtime.mjs` directly and
keeps the prototype's black canvas and small toolbar. The development entry
attaches its tools separately to that same owner. `runtime-public` is a
Release-only native target with an audited lifecycle/input export surface. The
initial public alpha deliberately has no audio: its native graph excludes the
GPL-derived resampler and its browser graph excludes the DSP coefficient
generator and Web Audio transport. Development retains its existing audio and
accuracy tests. Full public audio fidelity is on the roadmap.

Build it with `python3 scripts/build.py --target runtime-public --configuration Release`.
Then run `scripts/build_public.py --profile player --runtime-dir build/browser-public-release`
with an explicit output and environment. The packager checks the native build
identity and copies a fixed source graph into a content-hashed runtime directory;
`scripts/audit_public.py` independently regenerates and verifies its manifest.
The optional `maintenance` profile has no runtime or disc selector.
See [release review](docs/PUBLIC_RELEASE_REVIEW.md) and
[Cloudflare deployment](docs/PUBLIC_DEPLOYMENT.md) for complete commands,
publication requirements and rollback. This work does not widen gameplay acceptance.

## Play the current match

Open `runtime.html` on the local build server and choose your own unmodified
USA revision 1.02 Melee ISO, GCM, or CISO. **Open character select** enters the
original in-game CSS. The accepted first-deliverable slice supports two Marios,
four stocks and Final Destination through original CSS/SSS transitions, fighter
entrances, Ready/Go, gameplay HUD, pause/resume and GAME! ending. It returns
directly to original CSS after the source exit request; Results is deliberately
skipped. The runtime now also admits Falco, Fox, Marth, Battlefield, Yoshi's
Story and Dream Land,
with the narrower browser/native-loop evidence described above. All roster unlocks
are enabled separately from availability.

Click the canvas for keyboard input; the page lists P1/P2 bindings. Ordinary
keyboard Start and SSS cancellation have been checked in the browser, while a
complete ordinary-input Falco/Battlefield loop remains open. Physical controllers
use the original PAD processing path and still need acceptance checks.
The browser reads only required disc ranges; game data stays in the tab and is
neither uploaded nor persisted. RVZ is not supported. **Unload** releases the
source world and saves the optional renderer cache. `native-menu.html` redirects
to this player. The former HTML fighter/stage selectors are no longer in the
player flow. Asset inspection remains at `viewer.html`.

The current framebuffer is 640×480 at 1×. Cold-cache timing, physical input,
audio/reference comparison and full original-game equivalence remain open;
the diagnostic input buttons do not establish those forms of acceptance.

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

To build and run the separate gameplay foundation checks with the project-local
Node runtime:

```sh
python3 scripts/build.py --target gameplay
python3 scripts/check_gameplay.py
python3 scripts/check_gameplay.py --common assets-local/PlCo.dat --stage assets-local/GrNLa.dat --stage-kind 37
```

The data arguments are optional and stay local. `--stage-kind 37` is the original
Final Destination `GrKind`, distinct from viewer map entry 3. The runner reports
common-root readiness and source-consumer results; successful checks do not mean
a fighter or stage is fully initialized. `--target graphics` builds only the
browser viewer; the default build also includes the original-game player.

Open http://127.0.0.1:8787. Keep the tab visible for timing measurements. The
server binds to loopback and supplies cross-origin isolation headers. The `viewer.html` probe
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
entry 3 and **Opaque only**. The static viewer renders all 13 opaque meshes from
that entry and reports 13 omitted translucent meshes. It uses an inspection
camera and lights; gameplay stage animation, effects, collision and callbacks
belong to the separate runtime path.
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

- Melee's recovered C compiles directly to WebAssembly; source runtime integration
  proceeds through checked ownership and data boundaries.
- Aurora supplies a source-level GX-to-WebGPU implementation.
- Browser-specific scheduling, audio and asset conversion are explicit port work.
- The original GameCube build remains the behavioral reference.
- User-supplied disc content stays local and outside Git.

See [current evidence and limitations](STATUS.md),
[milestones](docs/ROADMAP.md), [browser performance work](docs/PERFORMANCE.md),
[the performance and accuracy playbook](docs/PERFORMANCE_AND_ACCURACY.md), and
[architecture decisions](docs/ARCHITECTURE.md).

This project is independent of Nintendo, doldecomp and Aurora. Dependency licenses
remain attached to their upstream sources; see [third-party notices](THIRD_PARTY.md).
