# Melee Web

Melee Web is a source port of vanilla Super Smash Bros. Melee for a desktop
browser. Recovered game code is compiled to WebAssembly; Aurora supplies the
source-level GX-to-WebGPU path. The original GameCube build remains the
behavioral reference.

Start with [Developer entry](docs/DEVELOPMENT.md) for setup, commands and the
change-boundary map. [Build, play and inspect](docs/BUILD_AND_PLAY.md) keeps the
reproducible local player and asset commands. The entry pages are intentionally
short. [STATUS.md](STATUS.md) is the
single current index of observed evidence, open failures and measured results;
historical reports and receipts linked there remain authoritative for their
declared scope. This README does not repeat pass counts, timings or deployment
state that can go stale.

The product goal is full vanilla Melee without a PowerPC interpreter or JIT. The
next acceptance deliverable is original in-game CSS → original SSS → a four-stock
Mario-versus-Mario Final Destination match → original CSS. Accuracy, performance,
rendering, audio, input and lifecycle are separate gates. Read the [accuracy
contract](docs/ACCURACY_CONTRACT.md) before broadening the supported path.

Game content is supplied locally from an owned USA revision 1.02 GALE01 revision-2
disc. The browser reads required ranges in the tab; disc images and extracted
assets stay local and outside Git. RVZ is not supported. The public player and
private macOS [Reference Capture application](docs/REFERENCE_CAPTURE_APP.md) are
separate surfaces.

## Build the development target

```sh
python3 scripts/bootstrap.py
python3 scripts/build.py
python3 scripts/serve.py --directory build/browser
```

Open `http://127.0.0.1:8787` in a desktop browser with WebGPU. `runtime.html`
is the original CSS/SSS player; `viewer.html` is the separate asset inspector.

For the full validation matrix, focused gameplay targets, local asset extraction,
public packaging and reference capture, use [Developer entry](docs/DEVELOPMENT.md)
and the linked boundary documents.

## Evidence and scope

The [performance and accuracy playbook](docs/PERFORMANCE_AND_ACCURACY.md) defines
the evidence labels, numerical rules and admission workflow. The [bounded
hitch-capture guide](docs/HITCH_CAPTURE.md) separates native deadline misses from
browser callback gaps. [Original comparison](docs/ORIGINAL_COMPARISON.md),
[recorded-queue replay](docs/RECORDED_QUEUE_REPLAY.md), and the [reference capture
procedure](docs/REFERENCE_CAPTURE_APP.md) define their own evidence boundaries.

Adding a fighter or stage requires its dedicated gate first: [source fighter
checkpoints](docs/ADDING_CHARACTERS.md) and [source stage checkpoints](docs/ADDING_STAGES.md).
Do not treat a compile, synthetic scene, short trace or average FPS as gameplay
validation, and do not call a partial browser route tournament-ready.

## Public packaging

The public alpha is a separate Release build with its own audited source graph
and deliberately disabled audio. Follow [public release review](docs/PUBLIC_RELEASE_REVIEW.md)
and [public deployment](docs/PUBLIC_DEPLOYMENT.md) for packaging or deployment;
those documents do not widen gameplay acceptance.
The [development audio replacement record](docs/AUDIO_REPLACEMENT_EVIDENCE.md)
tracks implementation provenance, compatibility checks and remaining accuracy
limits; it does not enable public audio.

This project is independent of Nintendo, doldecomp and Aurora. Preserve upstream
source provenance and notices; see [third-party notices](THIRD_PARTY.md).
