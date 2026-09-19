# Developer entry

Use this page to choose the smallest command set for the change. It is a map to
the detailed contracts, not a second status report. `STATUS.md` is the current
evidence index; follow its links for measurements, retained failures and exact
receipts instead of copying numbers into this page.

## First five minutes

```sh
python3 scripts/doctor.py --json
```

Run `python3 scripts/bootstrap.py` first when `.deps/` or `.venv/` is absent.
The doctor is read-only. Add `--disc /path/to/owned.gcm` to check a local disc
and `--build build/browser-public-release` to verify a public Release producer
receipt and its source/artifact hashes. Other build formats report freshness as
unknown; development captures use the frozen-build checks in
[Hitch capture](HITCH_CAPTURE.md). Keep local input paths untracked. A doctor
pass describes environment readiness, not gameplay or
performance acceptance. After the doctor, choose the smallest boundary check;
the [build, play and inspect guide](BUILD_AND_PLAY.md) has the local player,
asset and packaging commands. Run the full suite and affected build at handoff.

For a browser check, serve a built directory over loopback so cross-origin
isolation headers are present:

```sh
python3 scripts/serve.py --directory build/browser
```

Keep the tab visible for timing work. Browser graphics require a real browser;
the viewer's synthetic triangle and parser checks are useful smoke targets but
are not gameplay evidence.

For a short readiness and teardown probe, use the shared smoke harness. It can
optionally import an owned disc and reach CSS; it records failures and teardown
state but makes no timing or equivalence claim:

```sh
node scripts/browser_smoke.mjs \
  --url http://127.0.0.1:8787/runtime.html \
  --surface development --out work/browser-smoke \
  --disc /path/to/owned.gcm
```

Use `--surface public` with the public player URL. Pass `--playwright` when the
package is not available through the project Node resolution path. The output
directory must be new and belongs under ignored `work/`.

## Choose by change boundary

| Change | Start here | Focused command or procedure | Evidence boundary |
| --- | --- | --- | --- |
| Build, ABI or source integration | `scripts/build.py`, `docs/DEPENDENCIES.md` | `python3 scripts/build.py --target gameplay`; then the relevant `scripts/check_*.py` | Compile/source identified only until a runtime gate passes |
| HSD model, animation or asset parser | [Validation](TESTING.md) | `python3 scripts/check_assets.py assets-local/`; use an explicit stage entry | Parser/source behavior, separate from GPU output |
| Fighter | [Adding a source fighter](ADDING_CHARACTERS.md) | Pass owned-input discovery, real-asset construction, native lifecycle and the listed browser/reference gates | Source identity, native traced, retail compared, browser exercised, performance passed, then admitted |
| Stage | [Adding a source stage](ADDING_STAGES.md) | Derive map bounds from authored data; run stage construction and lifecycle checks | Stage-specific source, render, collision and teardown scope |
| Browser player or menu | [Accuracy contract](ACCURACY_CONTRACT.md) | Run `node scripts/browser_smoke.mjs ...` for readiness; use `runtime.html` through the original CSS/SSS route and the shared `scripts/browser_driver.mjs` helper in automation | Browser exercised; menu/transition and input claims need their own comparison |
| Browser hitch or first-use preparation | [Hitch capture](HITCH_CAPTURE.md), [browser performance](PERFORMANCE.md) | Run the frozen matrix; separate native deadlines, browser gaps, preparation and live callbacks | Cold/warm performance is named-machine evidence, never an average FPS claim |
| Native timing or retained capture | [Reference capture](REFERENCE_CAPTURE_APP.md) and [hitch capture](HITCH_CAPTURE.md) | Run `python3 scripts/native_capture.py preflight --output work/native-preflight`; validate its receipt, then use `record --pid` | A finalized trace is required; a failed or partial capture remains explicit evidence |
| Retail comparison or controller input | [Reference capture](REFERENCE_CAPTURE_APP.md), [original comparison](ORIGINAL_COMPARISON.md) | Use the private capture workflow and its hash-bound reports | Retail compared only for declared fields and source boundaries |
| Replay or recorded queue | [Recorded queue replay](RECORDED_QUEUE_REPLAY.md) | Validate the exact queue/draw/state inventory and first divergence | Conditional schedule evidence does not establish live timing or performance |
| Public player/package | [Public release review](PUBLIC_RELEASE_REVIEW.md) | Build `runtime-public` in Release, package, audit, then follow the deployment document | Packaging/deployment identity is separate from gameplay admission |

## Shared tools

`scripts/browser_driver.mjs` is the shared Playwright-facing orchestration layer.
New browser jobs should use its `createBrowserDriver(page, {surface,
timeoutMs, deadline})` and its readiness-aware methods (`waitForImport`,
`selectDisc`, `waitForStart`, `launch`, `waitForPhase`, `pressChord`, `unload`
and `diagnostics`). It serializes keyboard chords and waits for semantic
readiness; it does not auto-resume, retry, or recover a timed-out run. Treat a
timeout as a harness failure until the page state, captured diagnostics and
console evidence show otherwise. The helper does not turn a diagnostic route
into acceptance. Hitch profiles hash this helper; re-profile old manifests when
it changes.

`python3 scripts/native_capture.py` is the native capture entry point; see the
[native capture guide](NATIVE_CAPTURE.md) for scope and coverage details. Run
`preflight --output NEW_DIRECTORY` first, then validate its immutable receipt:

```sh
python3 scripts/native_capture.py preflight --output work/native-preflight
python3 scripts/native_capture.py validate --receipt work/native-preflight/preflight.json
python3 scripts/native_capture.py record --pid PID --output work/native-capture \
  --preflight-receipt work/native-preflight/preflight.json
```

The receipt states recorder attachment, bounded stop/finalization, XML export
and rolling-window retention evidence. It does not establish game-clock
alignment, gameplay correctness, GPU execution time or browser timing. Supply
the capture's actual coverage range when using `record`; keep raw captures and
generated receipts under ignored local paths. Do not rerun a failed capture
unchanged, and do not discard a useful negative receipt.

`python3 scripts/doctor.py --json` reports environment readiness. Optional
`--disc PATH` and `--build PATH` add local input/output checks. It never changes
dependencies, source trees, build artifacts or disc files. JSON is intended for
automation; human-readable output is the default when `--json` is omitted.

## Validation levels

The playbook's labels name independent evidence gates; one label does not imply
the next:

`Compiled`, `Source identified`, `Native traced`, `Retail compared`,
`Browser exercised`, `Performance passed`, and `Admitted`.

Name the scenario, fields, source boundary, machine, browser, build and run
inventory for each claim. Preserve the original input sampling boundary and
60 Hz simulation. Do not invent missed samples after a host stall, advance a
hidden match to warm resources, consume RNG, omit effects, widen tolerances,
remove compared fields or report an equal callback/tick total as cadence
equivalence. Preserve original arithmetic, float bits, save/music RNG and source
draw ordering.

## Before handoff

For code, run focused boundary checks, the full suite, the affected build and a
diff review. For documentation, validate local links and paths and inspect the
diff; a runtime build is unnecessary when executable behavior is unchanged.
Record observed failures and open gates in the authoritative evidence document.
Keep generated binaries, disc images, captures, credentials and personal paths
out of tracked files. Never claim a successful compile, synthetic scene or
average FPS as gameplay validation.
