# Developer entry

Use this page to choose the smallest command set for the change. It is a map to
the detailed contracts, not a second status report. `STATUS.md` is the current
evidence index; follow its links for measurements, retained failures and exact
receipts instead of copying numbers into this page.

The [documentation map](README.md) lists every document by purpose and says
where new documents belong.

For the relationship between the vanilla port, accuracy tooling and later
Slippi/online projects, read [project direction](PROJECT_DIRECTION.md). It records
strategy; the roadmap and boundary documents still govern implementation order
and acceptance.

Choose work from the [roadmap's current priorities](ROADMAP.md#current-priorities),
its pinned [priorities tracker](https://github.com/ericvanlare/melee-web/issues/158)
and current evidence in `STATUS.md`. GitHub issues own bounded tasks and completion criteria; this entry
owns command routing. Do not infer an accuracy pass from a closed implementation
issue, a short replay or a successful build.

## Start with the task you are doing

Use the [build, play and inspect guide](BUILD_AND_PLAY.md) for setup and player
commands, then choose the smallest relevant boundary check below.
`scripts/build.py` checks pinned source and toolchain prerequisites before
compilation. Build freshness remains the responsibility of the producer and
frozen-build checks in [Hitch capture](HITCH_CAPTURE.md).

Follow [local resource ownership](LOCAL_RESOURCES.md) for advisory disk reporting,
mutation safety, test scratch, and explicit retirement of completed builds.

For a browser check, serve a built directory over loopback so cross-origin
isolation headers are present:

```sh
python3 scripts/serve.py --directory build/browser
```

Routine checks use headless installed Chrome so agents can render, inspect,
take screenshots and drive input without opening a desktop window. Use the
shared browser tools for ad hoc scripts too. Do not retry failures in a visible
browser automatically. `--headed` explicitly opts into a foreground session;
arrange it with the user when required. Keep timing work on its visible-browser
protocol. The [browser automation guide](HEADLESS_BROWSER_VALIDATION.md)
records the tested scope and remaining foreground gates. The viewer's synthetic
triangle and parser checks are useful smoke targets but are not gameplay
evidence.

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

For the bounded local room UI check, serve the ordinary Release runtime on port
8787 and run the following under the workspace wrapper. It starts and disposes
the pinned local Worker, uses two independent headless profiles and actual
keyboard events, and retains evidence in a new output directory. The pinned
relay tools must already be available under `work/a3-room-relay-tools/node_modules`;
use Node 22 or newer. Keep the server under the same workspace owner (or use
`scripts/capture_owner_deadline.py --server-spec` to supervise both together).

```sh
python3 scripts/agent_workspace.py run -- node scripts/a3_local_room_keyboard_browser.mjs \
  --url http://127.0.0.1:8787/runtime.html --disc /path/to/owned.gcm \
  --out work/local-room-keyboard
```

This covers Host/Join, original CSS keyboard input, matching checksums and Leave.
See the [scoped readiness result](status/2026-10-11-local-room-ui-readiness.md);
it does not exercise the deployed staging Worker or a complete match.

The optional `--full-route` driver extends the same keyboard controls toward
original SSS, a four-stock Mario/Final Destination match, Results and CSS return.
Its scoped local functional result passes once; the same receipt retains the
preceding failed attempts and separate broader acceptance gates.
Use the supervised five-minute owner budget with cleanup reserve described in
that receipt. Do not interpret a driver timeout as a desync or extend its budget
without reducing the observed boundary first.

For owner-authorized same-machine network experiments, add
`--network-profile moderate` to that same command. The harness starts an owned
TCP proxy between both browser WebSockets and the local Worker. Named profiles
are `control`, `moderate`, `asymmetric` and `constrained`; their exact directional
settings and hashes are recorded in the report. This option cannot be combined
with public-relay flags. HTTP calibration, actual browser endpoint observations,
byte-integrity checks and final socket/listener cleanup are required. The
configured delay is per client/Worker leg; measured HTTP round trips are not
peer RTT or input latency. These experiments establish simulated-network
functional evidence only, leaving public staging and two-machine gates open.
Keep the same supervised owner budget and shared capture-lane policy.

The serial suite packages that owner and its cleanup checks:

```sh
python3 scripts/agent_workspace.py run -- python3 scripts/run_network_simulation.py \
  --disc "$MELEE_DISC" --playwright "$PLAYWRIGHT_DIR" --out "$NEW_EVIDENCE_DIR" \
  --scenarios css-moderate full-moderate full-asymmetric css-constrained
```

Choose a new evidence directory on the verified external volume. The suite
requires an existing Release runtime, records every served file's identity,
and stops on the first failed scenario. It retains failed evidence and never
retries automatically. Use `--plan-only` to inspect commands without launching
anything, or select a single scenario for a reviewed changed hypothesis.
Scenario availability is tooling, not a claim that its browser gate has passed.

`full-moderate` and `full-asymmetric` use the complete-match baseline, including
P2's short actual gameplay axis pulse. The separate, non-default
`full-moderate-action-probe` adds the strict jab/jump observation window. Its
underlying `--gameplay-action-probe` flag requires `--full-route`; reports and
suite validation bind the requested mode. A passing baseline cannot satisfy a
failed action probe. See the [M2 receipt](status/2026-10-11-simulated-network-full-route-m2.md)
for the current coverage and retained failures.

The separate `full-moderate-combat-probe` scenario adds a bounded actual-keyboard
P2 approach and jab, requiring paired source observations of the attack and
P1 damage reaction before continuing the original route. Its
`--gameplay-combat-probe` flag is exclusive with the action, stall, cut and
repeat probes. The approach permits at most 16 observed-release taps and 480
source cursors; its positive-distance target is an unproved reach hypothesis.
The existing action and owner deadlines remain fixed. Scenario availability
does not establish a hit, complete combat coverage or original equivalence.

The separately selected `full-moderate-combat-exchange` scenario keeps that
prefix, then adds one P1 jab and one P1 jab against P2's actual held shield.
It requires paired Guard entry before the attack, paired GuardSetOff, unchanged
defender damage through recovery, and exact A/R input and release windows.
The R hold is bounded to 30 source input ticks and the existing 500 ms observed
keyboard callback; each attack retains its 60-source-tick observation limit.
Use `--gameplay-combat-exchange` together with `--gameplay-combat-probe` only in
the moderate full-route scenario. This mode does not attribute a stock loss to
combat; its scoped result belongs in the combat evidence entry.

The reliability scenarios use separate result policies. Select one command below
with a new evidence directory for each invocation:

```sh
python3 scripts/agent_workspace.py run -- python3 scripts/run_network_simulation.py \
  --disc "$MELEE_DISC" --playwright "$PLAYWRIGHT_DIR" --out "$NEW_EVIDENCE_DIR" \
  --scenarios css-constrained-stall
```

```sh
python3 scripts/agent_workspace.py run -- python3 scripts/run_network_simulation.py \
  --disc "$MELEE_DISC" --playwright "$PLAYWRIGHT_DIR" --out "$NEW_EVIDENCE_DIR" \
  --scenarios css-moderate-cut css-moderate-fresh
```

```sh
python3 scripts/agent_workspace.py run -- python3 scripts/run_network_simulation.py \
  --disc "$MELEE_DISC" --playwright "$PLAYWRIGHT_DIR" --out "$NEW_EVIDENCE_DIR" \
  --scenarios full-moderate-repeat
```

The controlled-cut browser route remains a failure. A separate expected-fault
auditor checks the declared cut and normal resource retirement before the fresh
CSS scenario may start with a distinct room and process owner. The repeat
scenario runs two complete original routes in one continuously owned room,
retaining separate match artifacts and requiring the final raw checksum prefix
to contain the unchanged first-return prefix. It keeps a fixed admission rule,
second-route cap and cleanup reserve; an incomplete route or insufficient budget
fails. Full snapshots remain at validation boundaries; input-only reads use the
existing compact health suffix. Mode combinations that confuse these policies
are rejected. See the [scoped resilience receipt](status/2026-10-12-simulated-network-resilience.md)
for observed coverage and retained failures. Scenario availability does not
establish a passing gate; do not repeat a failed capture without a changed,
reviewed hypothesis.

## Choose by change boundary

| Change | Start here | Focused command or procedure | Evidence boundary |
| --- | --- | --- | --- |
| Build, ABI or source integration | `scripts/build.py`, `docs/DEPENDENCIES.md` | `python3 scripts/build.py --target gameplay`; then the relevant `scripts/check_*.py` | Compile/source identified only until a runtime gate passes |
| HSD model, animation or asset parser | [Validation](TESTING.md) | `python3 scripts/check_assets.py assets-local/`; use an explicit stage entry | Parser/source behavior, separate from GPU output |
| Fighter | [Adding a source fighter](ADDING_CHARACTERS.md) | Pass owned-input discovery, real-asset construction, native lifecycle and the listed browser/reference gates | Source identity, native traced, retail compared, browser exercised, performance passed, then admitted |
| Stage | [Adding a source stage](ADDING_STAGES.md) | Derive map bounds from authored data; run stage construction and lifecycle checks | Stage-specific source, render, collision and teardown scope |
| Browser player or menu | [Accuracy contract](ACCURACY_CONTRACT.md), [save profiles](SAVE_PROFILES.md) | Run `node scripts/browser_smoke.mjs ...` for readiness; use `runtime.html` through the original CSS/SSS route and the shared `scripts/browser_driver.mjs` helper in automation. Summarize an existing failed replay with [Browser failure triage](BROWSER_FAILURE_TRIAGE.md). | Browser exercised; menu/transition and input claims need their own comparison |
| Original cold-boot menus | [Menu-route capture](ORIGINAL_MENU_ROUTE_CAPTURE.md), [reference capture](REFERENCE_CAPTURE_APP.md) | Capture the fresh-DOL original route before porting new source scenes | Source identified or route diagnostic only until the retail and browser lifecycle gates pass |
| Browser hitch or first-use preparation | [Hitch capture](HITCH_CAPTURE.md), [browser performance](PERFORMANCE.md) | Run the frozen matrix; separate native deadlines, browser gaps, preparation and live callbacks | Cold/warm performance is named-machine evidence, never an average FPS claim |
| Native timing or retained capture | [Reference capture](REFERENCE_CAPTURE_APP.md) and [hitch capture](HITCH_CAPTURE.md) | Follow the owned-process capture procedure and verify the actual exported interval | A finalized trace is required; a failed or partial capture remains explicit evidence |
| Retail comparison or controller input | [Reference capture](REFERENCE_CAPTURE_APP.md), [original comparison](ORIGINAL_COMPARISON.md) | Use the private capture workflow and its hash-bound reports | Retail compared only for declared fields and source boundaries |
| Replay or recorded queue | [Recorded queue replay](RECORDED_QUEUE_REPLAY.md) | Validate the exact queue/draw/state inventory and first divergence | Conditional schedule evidence does not establish live timing or performance |
| Recorded whole session | [Recorded-session state comparison](RECORDED_SESSION_STATE.md) | Replay the bound MWRC v8 input, then run the strict whole-session comparator | Exact declared match state plus complete input/scene order; separate from draw cadence and menu scalar state |
| Allocation history comparison | [Allocation trace comparison](ALLOCATION_TRACE_COMPARISON.md) | `python3 scripts/compare_allocation_traces.py --original-trace ... --browser-trace ... --out work/...` | Strict selected-prefix comparison; incomplete or unmapped fields remain explicit |
| Repository publication | [Public repository checklist](PUBLIC_REPOSITORY_CHECKLIST.md), [content guard](REPOSITORY_CONTENT_CHECK.md) | Stage intended content and run `python3 scripts/check_repository_content.py`; resolve provenance and audit the publication checkpoint and GitHub controls | The guard checks the index or a named commit; history and publication rights need their separate reviews |
| Production audio player/package | [Production audio](AUDIO_PRODUCTION.md) | Build `runtime-audio-preview` in Release; run `scripts/release_audio_player.py prepare`, `audit` and `verify`; promote the same audited bytes | Audio package/hosted evidence is separate from gameplay admission and the silent rollback |
| Public player/package | [Public release review](PUBLIC_RELEASE_REVIEW.md) | Build the silent `runtime-public` target in Release, package, audit, then follow the deployment document | Packaging/deployment identity is separate from gameplay admission |

## Shared tools

For early fighter/stage iteration, [content checks](CONTENT_CHECKS.md) batch
explicit asset roots and freshly built source lifecycle traces. The command
retains the first failing boundary and treats missing local inputs as failures;
it does not replace browser, original-comparison or admission checks.

`scripts/browser_driver.mjs` is the shared Playwright-facing orchestration layer.
New browser jobs should use its `createBrowserDriver(page, {surface,
timeoutMs, deadline})` and its readiness-aware methods (`waitForImport`,
`selectDisc`, `waitForPhase`, `pressChord`, `unload` and `diagnostics`). The
development surface retains `waitForStart` and `launch` for its manual launch
contract. The public surface auto-launches after a successful import; use
`waitForPublicCss`, then call `recoverAudioActivation` only when that method
reports the player's specific suspended-audio recovery state. The helper never
clicks Play on a successful public load, retries an import/preparation failure,
or recovers a timed-out run. Treat a timeout as a harness failure until the
page state, captured diagnostics and console evidence show otherwise. The
helper does not turn a diagnostic route into acceptance. Hitch profiles hash
this helper; re-profile old manifests when it changes.

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

## Execution contract and handoff

Before an execution task, record its observable outcome/failure, smallest next
experiment, exact pass criteria, exclusions, existing solutions checked and
stopping rule in the issue. After two experiments at one boundary, reduce the
reproducer or request a bounded review before another long run. Keep component
steps as commits within the milestone PR, and keep the broader acceptance issue
open until all its gates pass. Keep one `docs/status/` entry per boundary, updating it in place as the
boundary advances and preserving historical results and exact evidence
identities. Create an entry for a new boundary; do not rewrite or consolidate
past entries. Keep evidence in these entries and scoped reports rather
than copying it into the roadmap or another status document.

To pause unfinished work, push a WIP branch with one handoff at
`docs/handoffs/YYYY-MM-DD-<slug>.md`. It states the branch, base and source
commit; what was tested and what was not; open failures; and the exact next
step. Keep machine-local paths and hashes in a private manifest outside Git, and
name only how to find it. Preserved helper code goes under
`tools/checkpoints/<slug>/` on the same branch. Do not create other handoff
locations. Link the handoff from the tracking issue and from the
[priorities tracker](https://github.com/ericvanlare/melee-web/issues/158).

For code, run focused boundary checks, the full suite, the affected build and a
diff review. For documentation, validate local links and paths and inspect the
diff; a runtime build is unnecessary when executable behavior is unchanged.
Record observed failures and open gates in the authoritative evidence document.
Keep generated binaries, disc images, captures, credentials and personal paths
out of tracked files. Never claim a successful compile, synthetic scene or
average FPS as gameplay validation.
