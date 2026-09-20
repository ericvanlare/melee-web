# Developer entry

Use this page to choose the smallest command set for the change. It is a map to
the detailed contracts, not a second status report. `STATUS.md` is the current
evidence index; follow its links for measurements, retained failures and exact
receipts instead of copying numbers into this page.

## Start with the task you are doing

Use the [build, play and inspect guide](BUILD_AND_PLAY.md) for setup and player
commands, then choose the smallest relevant boundary check below.
`scripts/build.py` checks pinned source and toolchain prerequisites before
compilation. Build freshness remains the responsibility of the producer and
frozen-build checks in [Hitch capture](HITCH_CAPTURE.md).

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

Use `--surface public` with the public player URL. The smoke command and
public-player browser test share configuration: `--playwright PACKAGE_DIR`
overrides `MELEE_PLAYWRIGHT_DIR`, followed by normal Node package resolution.
They use installed Google Chrome unless `MELEE_BROWSER_PATH` names another
installed Chromium executable. Invalid explicit paths fail without falling
back. The output directory must be new and belongs under ignored `work/`;
missing parent directories are created automatically.

For the remaining offline game scope and its searchable source inventory, use
[the full-game integration workflow](FULL_GAME_PORT.md). It keeps implementation
status separate from original-comparison and browser acceptance.

## Choose by change boundary

| Change | Start here | Focused command or procedure | Evidence boundary |
| --- | --- | --- | --- |
| Build, ABI or source integration | `scripts/build.py`, `docs/DEPENDENCIES.md` | `python3 scripts/build.py --target gameplay`; then the relevant `scripts/check_*.py` | Compile/source identified only until a runtime gate passes |
| HSD model, animation or asset parser | [Validation](TESTING.md) | `python3 scripts/check_assets.py assets-local/`; use an explicit stage entry | Parser/source behavior, separate from GPU output |
| Fighter | [Adding a source fighter](ADDING_CHARACTERS.md) | Pass owned-input discovery, real-asset construction, native lifecycle and the listed browser/reference gates | Source identity, native traced, retail compared, browser exercised, performance passed, then admitted |
| Stage | [Adding a source stage](ADDING_STAGES.md) | Derive map bounds from authored data; run stage construction and lifecycle checks | Stage-specific source, render, collision and teardown scope |
| Browser player or menu | [Accuracy contract](ACCURACY_CONTRACT.md) | Run `node scripts/browser_smoke.mjs ...` for readiness; use `runtime.html` through the original CSS/SSS route and the shared `scripts/browser_driver.mjs` helper in automation | Browser exercised; menu/transition and input claims need their own comparison |
| Browser hitch or first-use preparation | [Hitch capture](HITCH_CAPTURE.md), [browser performance](PERFORMANCE.md) | Run the frozen matrix; separate native deadlines, browser gaps, preparation and live callbacks | Cold/warm performance is named-machine evidence, never an average FPS claim |
| Native timing or retained capture | [Reference capture](REFERENCE_CAPTURE_APP.md) and [hitch capture](HITCH_CAPTURE.md) | Follow the owned-process capture procedure and verify the actual exported interval | A finalized trace is required; a failed or partial capture remains explicit evidence |
| Retail comparison or controller input | [Reference capture](REFERENCE_CAPTURE_APP.md), [original comparison](ORIGINAL_COMPARISON.md) | Use the private capture workflow and its hash-bound reports | Retail compared only for declared fields and source boundaries |
| Replay or recorded queue | [Recorded queue replay](RECORDED_QUEUE_REPLAY.md) | Validate the exact queue/draw/state inventory and first divergence | Conditional schedule evidence does not establish live timing or performance |
| Public player/package | [Public release review](PUBLIC_RELEASE_REVIEW.md) | Build `runtime-public` in Release, package, audit, then follow the deployment document | Packaging/deployment identity is separate from gameplay admission |

## Shared tools

For early fighter/stage iteration, [content checks](CONTENT_CHECKS.md) batch
explicit asset roots and freshly built source lifecycle traces. The command
retains the first failing boundary and treats missing local inputs as failures;
it does not replace browser, original-comparison or admission checks.

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

Development-page failures use the explicit `#status[data-runtime-error]`
channel; public-page failures use the error dialog. The driver reports the
actual error immediately. `selectDisc` and `unload` can begin recovery when
their controls are enabled; readiness and completion still require success.
It never resumes a timing failure.

After changing this harness, run its focused checks and real HTTP/browser
contract (including smoke-command failure paths):

```sh
python3 -m unittest discover -s tests -p test_browser_driver.py -v
node tests/browser_driver_browser_test.mjs
```

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
