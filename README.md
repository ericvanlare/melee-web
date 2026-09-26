# Melee Web

**[Play the public alpha at webmelee.gg](https://webmelee.gg)**

Melee Web is a source port of vanilla Super Smash Bros. Melee for a desktop
browser. Recovered game code is compiled to WebAssembly; Aurora supplies the
source-level GX-to-WebGPU path. The goal is the complete original game without
a PowerPC CPU interpreter or JIT.

## Play online

1. Open [webmelee.gg](https://webmelee.gg) in a desktop browser with WebGPU support.
2. Choose **Disc** and select your own unmodified USA revision 1.02
   (GALE01 revision-2) Melee disc image in ISO, GCM or CISO format. RVZ is not
   supported.
3. Choose **Play**. Open **Controls** to see keyboard bindings and input options.

Game content is supplied by your local disc image. The browser reads required
ranges on your device; the disc and extracted game data are not uploaded.
Disc images and extracted game archives are not included in this repository.

This is a work-in-progress alpha with limited playable integration. Features,
accuracy and performance are still being developed; errors can interrupt play.
The live release and the latest repository code may differ. See
[current status](STATUS.md) for tested scenarios, known failures and the
supporting evidence, and the [roadmap](docs/ROADMAP.md) for planned work.

## Develop locally

Start with [Contributing](CONTRIBUTING.md) and the
[developer entry](docs/DEVELOPMENT.md) for prerequisites and the checks relevant
to your change. Bootstrap installs the pinned source and build dependencies.

```sh
python3 scripts/bootstrap.py
python3 scripts/build.py
python3 scripts/serve.py --directory build/browser
```

Open `http://127.0.0.1:8787/runtime.html` in a desktop browser with WebGPU.
Choose your local disc and **Open character select** to enter the original
in-game character select screen. `viewer.html` is the separate asset inspector.

The [build, play and inspect guide](docs/BUILD_AND_PLAY.md) covers local assets
and inspection commands. Keep disc images, extracted assets and generated
captures outside Git; use ignored `assets-local/` and `work/` directories for
local inputs and evidence. The private macOS
[Reference Capture application](docs/REFERENCE_CAPTURE_APP.md) supports
original-game comparisons separately from the browser player.

## Accuracy and contribution priorities

The original GameCube build is the behavioral reference. The next acceptance
deliverable is original character select → original stage select → a four-stock
Mario-versus-Mario Final Destination match → original character select.
Accuracy, rendering, audio, input, lifecycle and performance have separate gates;
the public alpha is not an accepted accurate or tournament-ready port.

Use the [accuracy contract](docs/ACCURACY_CONTRACT.md) and
[performance and accuracy playbook](docs/PERFORMANCE_AND_ACCURACY.md) when
changing runtime behavior. Adding a fighter or stage starts with the
[source fighter checkpoints](docs/ADDING_CHARACTERS.md) or
[source stage checkpoints](docs/ADDING_STAGES.md).

[Original comparison](docs/ORIGINAL_COMPARISON.md),
[recorded-queue replay](docs/RECORDED_QUEUE_REPLAY.md) and
[hitch capture](docs/HITCH_CAPTURE.md) describe the validation procedures.
A successful build or short trace does not establish full gameplay equivalence.
[STATUS.md](STATUS.md) is the evidence index for measured results and open gates.

## Release documentation

The public release has two separate audited Release profiles: `audio-player`
for the production-audio path and `player` as the silent rollback identity.
See [production audio](docs/AUDIO_PRODUCTION.md),
[public release review](docs/PUBLIC_RELEASE_REVIEW.md) and
[public deployment](docs/PUBLIC_DEPLOYMENT.md) for packaging, validation and
release procedures. A successful deployment does not establish gameplay
accuracy or performance.

## Credits and licensing

This project is independent of Nintendo, doldecomp and Aurora. Preserve upstream
source provenance and notices; see [third-party notices](THIRD_PARTY.md).
The [root MIT grant](LICENSE) covers only the explicit
[project-file scope](LICENSE_SCOPE.md). It does not cover recovered game/SDK
material, current audio/data, or the combined player. See the
[publication assessment](docs/PUBLICATION_PROVENANCE_ASSESSMENT.md) for those
boundaries and the owner's accepted publication risk.
