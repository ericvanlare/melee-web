# Contributing to Melee Web

Melee Web is an experimental source port of vanilla Super Smash Bros. Melee
for a desktop browser. Contributions should keep the original game's behavior
and provenance boundaries visible. Read the [developer entry](docs/DEVELOPMENT.md)
and [build, play and inspect guide](docs/BUILD_AND_PLAY.md) before changing a
runtime boundary.

## Set up a checkout

Use a normal local checkout outside a cloud-synced folder. The supported
development baseline is Python 3, Git, CMake and Ninja from the project virtual
environment, plus a desktop Chromium browser with WebGPU. Bootstrap downloads
the pinned source/toolchain inputs into ignored directories and stops when the
dependency checkouts have unexpected changes.

```sh
python3 scripts/bootstrap.py
python3 scripts/build.py
```

Keep disc images, extracted game files, generated binaries, captures,
credentials and personal paths outside Git. Owned game inputs belong under the
ignored `assets-local/` directory when a documented local check needs them;
reports and temporary evidence belong under ignored `work/` or `build/` paths.
Do not upload game assets or secrets in an issue, pull request, fixture, log or
artifact. A renamed extension does not make a payload safe to publish.

## Validate a change

Choose the smallest boundary check from the [developer entry](docs/DEVELOPMENT.md)
and record the command, build/configuration, inputs and observed limitations in
the pull request. A normal baseline is:

```sh
python3 -m unittest discover -s tests -v
python3 scripts/build.py --target gameplay
```

For browser behavior, serve the built directory over loopback and use the real
browser path:

```sh
python3 scripts/serve.py --directory build/browser
node scripts/browser_smoke.mjs \
  --url http://127.0.0.1:8787/runtime.html \
  --surface development --out work/browser-smoke
```

The smoke harness is a readiness and teardown probe. A compile, synthetic
scene, short trace or average FPS does not establish gameplay accuracy or
performance. Use the evidence labels and comparison procedures in the
[accuracy contract](docs/ACCURACY_CONTRACT.md) and [performance and accuracy
playbook](docs/PERFORMANCE_AND_ACCURACY.md) for stronger claims. Inspect a
preparation error immediately and retain failed evidence; do not retry a timed
run without a changed hypothesis.

Before handoff, inspect the diff and validate local links and paths. Code
changes should also run their focused checks, the affected build and the full
test suite. Documentation-only changes do not need a runtime build when
executable behavior is unchanged.

## Source, generated files and patches

Keep upstream checkouts intact and keep dependency revisions pinned in
`dependencies.lock.json`. Browser adaptations belong in
`patches/aurora-browser.patch`; gameplay ABI corrections belong in
`patches/melee-gameplay.patch`. Build preparation materializes generated source
under ignored `build/gameplay-source/`; do not edit that checkout as the source
of record. Regenerate a tracked generated declaration only through its existing
generator, review the generated diff, and run its `--check` mode when the
generator provides one. The [testing guide](docs/TESTING.md) documents the
fighter-registry example and the [dependency guide](docs/DEPENDENCIES.md)
documents the patch boundaries.

Changes to shared runtime ownership, generated declarations, source patches or
accuracy-sensitive behavior need a clear description of the source routine or
observed behavior that motivates them. Preserve source identities, authored
table bounds, save/music RNG, input sampling, source draw order, object/process
order, original arithmetic and float bits. Do not add silent success stubs,
guessed table bounds, per-asset exemptions, fast-math, relaxed precision or a
frame-rate change as a fix. Missing services should fail explicitly. Keep
archive decoding separate from GPU resource lifetime.

Before proposing repository content for publication, stage the intended changes
and run the repository-content guard against the Git index/staged snapshot:

```sh
python3 scripts/check_repository_content.py
```

To inspect a specific commit, pass its revision:

```sh
python3 scripts/check_repository_content.py --ref REV
```

Read the [guard's scope and exception procedure](docs/REPOSITORY_CONTENT_CHECK.md).
The [public repository checklist](docs/PUBLIC_REPOSITORY_CHECKLIST.md), provenance
inventory and third-party notices cover the remaining publication decisions.
Keep all unresolved licensing and generated-data limits explicit in the pull
request.

## Pull requests

Explain the user-visible or boundary-level behavior change, the source or
provenance basis, the focused validation you ran, and any evidence limits. Say
which original-game inputs were used without attaching them. Keep the change
small enough that source identity, generated output and lifecycle ownership can
be reviewed together. Do not claim a gameplay, retail-equivalence or
performance gate from a narrower check.

The project remains independent of Nintendo, doldecomp and Aurora. Preserve
upstream notices and consult [third-party provenance](THIRD_PARTY.md), the
[source/license inventory](docs/SOURCE_LICENSE_INVENTORY.md) and the
[audio coefficient review](docs/AUDIO_COEFFICIENT_REVIEW.md) before changing
attributed or unresolved material.
