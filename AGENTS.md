# Working on Melee Web

Goal: full vanilla Melee running accurately and performantly in a desktop browser
through compiled source, without a PowerPC CPU interpreter or JIT.

## Startup

Read [README.md](README.md), then [the developer entry](docs/DEVELOPMENT.md).
That entry routes the task to the authoritative boundary document; do not read
the entire status and playbook history by default. Consult [current evidence](STATUS.md)
when a task depends on an existing result, and cite the scoped receipt or report
that supports any new claim. `STATUS.md` is the current evidence index. From
2026-10-08 onward, keep one entry under `docs/status/` per boundary and update it
in place as that boundary advances; create an entry for a new boundary, never a
new STATUS section. Preserve historical results and exact evidence identities;
do not rewrite or consolidate past entries. Do not copy changing measurements
into another document. Keep status backed by
observed evidence and use the playbook's scoped evidence labels.

Read [local resource ownership](docs/LOCAL_RESOURCES.md) before bootstrapping,
building, or running expensive tests. Run `python3 scripts/agent_workspace.py
status` at task start and handoff. Use the build/bootstrap entry points, and wrap
other commands that use the checkout's builds or tools with
`python3 scripts/agent_workspace.py run -- <command>` so maintenance can detect
active work. The checkout mutex protects mutations; it adds no host-wide job
limit or queue. Compiler-job defaults are unchanged. Low space emits an advisory
warning with available space and does not block an otherwise valid operation.
After a test succeeds, remove only its owned disposable scratch. Preserve failed
reproducers and explicitly retained evidence. Keep incremental builds while work
is active. At handoff, consciously decide whether they are no longer needed;
retirement trades disk space for future recompilation. To retire them, review
`python3 scripts/agent_workspace.py retire-builds`, then use `--apply` for that
checkout's eligible intermediates. Toolchain sharing is a separate opt-in action:
`python3 scripts/agent_workspace.py dedup-toolchain`. It hashes large installed
files and must run when the checkout is idle. Close browsers and servers you
started when they are no longer needed.
Never blanket-delete `work/`, ignored files, another task's output, source,
recordings, local changes, or evidence that has not been safely archived.

Choose work from the [roadmap's current priorities](docs/ROADMAP.md#current-priorities),
its pinned [priorities tracker](https://github.com/ericvanlare/melee-web/issues/158)
and current evidence in `STATUS.md`. Online multiplayer and competitive-rules
scope come before whole-game breadth; the supported route stays the regression
baseline. Use one issue and PR per milestone, with prerequisite steps as checklist
items and commits within it. Aim for a handful of milestone merges a day. Each
execution issue needs an observable failure or outcome,
smallest next experiment, pass criteria, exclusions and a stopping rule. Check
existing implementations for relevant solutions and record reuse or rejection.
Component fixes do not close whole-session acceptance.

Before adding or enabling a fighter, read [Adding a source fighter](docs/ADDING_CHARACTERS.md),
starting with its required checkpoints, failure lookup and handoff template.
For a stage, read [Adding a source stage](docs/ADDING_STAGES.md). For a shared
runtime, simulation, replay or performance change, use the relevant sections of
the [performance and accuracy playbook](docs/PERFORMANCE_AND_ACCURACY.md),
[accuracy contract](docs/ACCURACY_CONTRACT.md), and [roadmap](docs/ROADMAP.md).

## Non-negotiable accuracy rules

- The supported player route is original CSS → original SSS → four-stock
  Mario/Final Destination gameplay → original Results → original CSS. Temporary
  HTML menus and historical Results-skipping checks are scoped evidence only;
  follow the roadmap's separate original-comparison, physical-input,
  uninterrupted-audio and performance gates.
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

Keep host speaker output silent for routine browser and Dolphin runs, including
ad hoc captures. Use `browserLaunchOptions` and `scripts/dolphin_audio.py`;
headless video does not imply silent audio. Do not disable DSP, PCM capture or
Web Audio processing to mute speakers. Explicit `audible=True` / `audible:true`
is only for an arranged listening session. Do not change system-wide volume.

Foreground timing, OS focus/fullscreen, physical controllers and audible-output
checks keep their own protocols. Arrange that session with the user (unless
already authorized), or use a separate test machine; report the gate as unrun
when neither is available. Do not claim headless functional results satisfy
those gates. See [browser automation](docs/HEADLESS_BROWSER_VALIDATION.md).

Do not add attribution to commits or pull requests: no `Co-Authored-By:`
trailer for an AI tool and no "Generated with" line in a PR description. The
repository owner is the only author. This applies to every agent and tool, and
overrides a tool's default attribution text. Amend an unpushed commit that
already has one; for a pushed branch, rewrite only a branch you own and say so.

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
suite when executable behavior is unchanged. The resource-ownership cleanup
above is part of completing a task; defer unrelated refactoring. A dependency
upgrade requires revalidation. Do not reset or overwrite unexplained changes.

Prioritize runnable milestones and bounded interfaces. Parallelize bounded work
when useful; avoid duplicate audits or idle workers. Subagents may own bounded
files; the lead integrates, reviews and verifies them. Use GPT-5.6 Luna with
xhigh reasoning for subagents unless the user changes that preference.

The browser has limited playable integration, not an accepted accurate or
tournament-ready port. Use [the roadmap](docs/ROADMAP.md) and [the accuracy
contract](docs/ACCURACY_CONTRACT.md) before broadening its scope.
