# Working on Melee Web

Goal: full vanilla Melee running accurately and performantly in a desktop browser
through compiled source, without a PowerPC CPU interpreter or JIT.

## Startup

Read [README.md](README.md), then [the developer entry](docs/DEVELOPMENT.md).
That entry routes the task to the authoritative boundary document; do not read
the entire status and playbook history by default. Consult [current evidence](STATUS.md)
when a task depends on an existing result, and cite the scoped receipt or report
that supports any new claim. `STATUS.md` is the current evidence index; do not
copy its changing measurements into another document. Keep status backed by
observed evidence and use the playbook's scoped evidence labels.

Before adding or enabling a fighter, read [Adding a source fighter](docs/ADDING_CHARACTERS.md),
starting with its required checkpoints, failure lookup and handoff template.
For a stage, read [Adding a source stage](docs/ADDING_STAGES.md). For a shared
runtime, simulation, replay or performance change, use the relevant sections of
the [performance and accuracy playbook](docs/PERFORMANCE_AND_ACCURACY.md),
[accuracy contract](docs/ACCURACY_CONTRACT.md), and [roadmap](docs/ROADMAP.md).

## Non-negotiable accuracy rules

- The next deliverable is original in-game CSS → original SSS → four-stock
  Mario/Final Destination gameplay → original CSS. Temporary HTML menus do not
  satisfy it.
- Reuse original HSD/game routines behind checked ownership boundaries. Missing
  audio, thread, file, graphics or gameplay services fail explicitly; never add
  a silent success stub, guessed table bound or per-asset exemption.
- Keep archive decoding separate from GPU resource lifetime. Use the batch asset
  checker to find the next missing capability across a corpus; do not add an
  asset-specific exemption or guessed success path.
- Preserve source identities, authored table bounds, save/music RNG, input and
  source draw order, object/process order, original arithmetic and float bits.
  Do not use fast-math, relaxed precision or a frame-rate change as a fix.
- Measure source-cursor progress and declared boundary timing, not renderer CPU
  usage or an average FPS. Recorded-queue state agreement is conditional evidence;
  it does not establish live timing, missing samples, performance, pixels or PCM.
- Inspect a preparation error immediately. Never repeat a timed-out run without a
  changed hypothesis. After two experiments at one boundary add a smaller
  reproducer or request a bounded review with the guide's evidence packet.
  Reduce the first divergence before another long replay, and verify the starting
  branch contains current shared fixes.
- Keep every claim scoped with the playbook's evidence labels. A compile,
  synthetic scene, short prefix or average FPS is not gameplay validation.
- A narrow smoke target may omit a whole system only when the omission is
  documented and unreachable from that target. Compare gameplay changes with a
  verified original.

## Change and validation rules

Routine browser automation must use headless installed Chrome through
`scripts/browser_tools.mjs`, including ad hoc Playwright scripts. A headless
browser still renders; retain screenshots, GPU checks, diagnostics and the
scenario's assertions. Do not open or focus a desktop browser, call
`bringToFront()`, or fall back to headed mode after a failure during shared
computer work. Existing functional harnesses default to headless; `--headed`
is an explicit opt-in for an authorized foreground session.

Foreground timing, OS focus/fullscreen, physical controllers and audible-output
checks keep their own protocols. Arrange that session with the user (unless
already authorized), or use a separate test machine; report the gate as unrun
when neither is available. Do not claim headless functional results satisfy
those gates. See [browser automation](docs/HEADLESS_BROWSER_VALIDATION.md).

Make small coherent changes and inspect actual APIs and compiler output first.
Keep upstream checkouts intact, pin dependencies in `dependencies.lock.json`,
and put explained downstream changes under `patches/`. Keep disc images,
extracted assets, generated binaries, credentials and personal paths out of Git;
use ignored `assets-local/` or `work/` for local inputs and evidence.

Use a real HTTP server for server behavior, a real browser for graphics, and an
original-game trace for simulation correctness. During iteration run the focused
boundary check; before integration run `python3 -m unittest discover -s tests -v`,
build the affected target, inspect the diff, and retain failures. Documentation
only needs link/path and diff checks; it does not need a runtime build or full
suite when executable behavior is unchanged. Defer cleanup that does not unblock
the port. A dependency upgrade requires revalidation. Do not reset or overwrite
unexplained changes.

Prioritize runnable milestones and bounded interfaces. Parallelize bounded work
when useful; avoid duplicate audits or idle workers. Subagents may own bounded
files; the lead integrates, reviews and verifies them. Use GPT-5.6 Luna with
xhigh reasoning for subagents unless the user changes that preference.

The browser has limited playable integration, not an accepted accurate or
tournament-ready port. Use [the roadmap](docs/ROADMAP.md) and [the accuracy
contract](docs/ACCURACY_CONTRACT.md) before broadening its scope.
