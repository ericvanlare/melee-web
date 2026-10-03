# Documentation map

Start with the [developer entry](DEVELOPMENT.md). It routes a change to the
smallest set of contracts below. Do not read this whole tree by default.
Observed results live in [STATUS](../STATUS.md) and its dated
[entries](status/), with receipts in [evidence](evidence/).

## Start here

| Document | Use it for |
| --- | --- |
| [Developer entry](DEVELOPMENT.md) | Command routing by change boundary |
| [Roadmap](ROADMAP.md) | Work order and acceptance boundaries |
| [Accuracy contract](ACCURACY_CONTRACT.md) | What counts as correct, and how it is proven |
| [Performance and accuracy playbook](PERFORMANCE_AND_ACCURACY.md) | Evidence labels, timing and runtime change rules |
| [Architecture decisions](ARCHITECTURE.md) | Durable design decisions |
| [Project direction](PROJECT_DIRECTION.md) | Vanilla port → Slippi compatibility → online strategy |
| [Build, play and inspect](BUILD_AND_PLAY.md) | Local setup and the player |
| [Validation](TESTING.md) | Test suites and focused checks |
| [Local resources](LOCAL_RESOURCES.md) | Disk, scratch and build ownership |
| [Dependencies](DEPENDENCIES.md) | Pins and patch boundaries |
| [Player experience metrics](PLAYER_EXPERIENCE_METRICS.md) | Release-to-release browser wait and memory observations |
| [Architects' coordination log](ARCHITECTS.md) | Cross-cutting structural decisions and coordination |

## Adding content

[Adding a fighter](ADDING_CHARACTERS.md), [adding a stage](ADDING_STAGES.md),
[content checks](CONTENT_CHECKS.md), [full-game inventory](FULL_GAME_PORT.md),
[fighter runtime gate](FIGHTER_RUNTIME.md) and [CPU opponents](CPU_OPPONENTS.md).
Per-fighter and per-stage development notes are in [`content/`](content/);
[Mewtwo's notes](MEWTWO_PORT_NOTES.md) stay at the top level because retained
receipts cite that path.

## Comparison, replay and diagnostics procedures

- Original game: [reference capture app](REFERENCE_CAPTURE_APP.md),
  [original comparison](ORIGINAL_COMPARISON.md),
  [menu route capture](ORIGINAL_MENU_ROUTE_CAPTURE.md),
  [transition equivalence](TRANSITION_EQUIVALENCE.md),
  [retail replay capture](RETAIL_REPLAY_CAPTURE.md),
  [retail draw clock](RETAIL_DRAW_CLOCK.md).
- Replay and session state: [recorded queue replay](RECORDED_QUEUE_REPLAY.md),
  [recorded session state](RECORDED_SESSION_STATE.md),
  [replay corpus](REPLAY_CORPUS.md),
  [Slippi replay validation](SLIPPI_REPLAY_VALIDATION.md),
  [CPU match corpus](CPU_MATCH_CORPUS.md).
- CPU and allocation context: [source address context](SOURCE_ADDRESS_CONTEXT.md),
  [allocation trace comparison](ALLOCATION_TRACE_COMPARISON.md),
  [CPU register diagnostics](CPU_REGISTER_DIAGNOSTICS.md),
  [CPU register compatibility](CPU_REGISTER_COMPATIBILITY.md),
  [CPU zero-knockback ABI](CPU_ZERO_KNOCKBACK_ABI.md),
  [owned save prerequisite](OWNED_SAVE_PREREQUISITE.md).
- Browser: [headless browser validation](HEADLESS_BROWSER_VALIDATION.md),
  [browser failure triage](BROWSER_FAILURE_TRIAGE.md),
  [runtime diagnostics](RUNTIME_DIAGNOSTICS.md),
  [hitch capture](HITCH_CAPTURE.md), [browser performance](PERFORMANCE.md).

## Player runtime services

[Prototype boundary](PROTOTYPE.md), [scene asset loading](SCENE_ASSET_LOADING.md),
[controllers](CONTROLLERS.md), [keyboard layouts](KEYBOARD_LAYOUTS.md),
[save profiles](SAVE_PROFILES.md), [public resize](PUBLIC_RESIZE.md), and the
renderer pipeline cache: [preparation](PIPELINE_PREPARATION.md),
[provenance](PIPELINE_PROVENANCE.md),
[requirements schema](PIPELINE_REQUIREMENTS_SCHEMA.md).

## Audio

[Production audio](AUDIO_PRODUCTION.md), [listening preview](AUDIO_PREVIEW.md),
[replacement evidence](AUDIO_REPLACEMENT_EVIDENCE.md),
[resampler contract](AUDIO_RESAMPLER_CONTRACT.md),
[filter design](AUDIO_FILTER_DESIGN.md) and
[coefficient review](AUDIO_COEFFICIENT_REVIEW.md).

## Release and publication

- Release: [public release review](PUBLIC_RELEASE_REVIEW.md),
  [public deployment](PUBLIC_DEPLOYMENT.md), [public staging](PUBLIC_STAGING.md).
- Repository publication: [checklist](PUBLIC_REPOSITORY_CHECKLIST.md),
  [content check](REPOSITORY_CONTENT_CHECK.md),
  [history audit](REPOSITORY_HISTORY_AUDIT.md),
  [GitHub audit](GITHUB_PUBLICATION_AUDIT.md),
  [compiler-cache review](COMPILER_CACHE_REVIEW.md),
  [review brief](PUBLICATION_REVIEW_BRIEF.md),
  [pre-freeze](PUBLICATION_PRE_FREEZE.md),
  [checkpoint](PUBLICATION_CHECKPOINT.md), [cutover](PUBLICATION_CUTOVER.md),
  [provenance assessment](PUBLICATION_PROVENANCE_ASSESSMENT.md),
  [source license inventory](SOURCE_LICENSE_INVENTORY.md).

These paths are cited by the license scope, provenance inventories, retained
receipts or code. Do not move them without updating every citation.

## Subdirectories

| Directory | Contents |
| --- | --- |
| [`status/`](status/) | Dated evidence entries indexed by [STATUS](../STATUS.md) |
| [`evidence/`](evidence/) | Hash-bound receipts; immutable once cited |
| [`content/`](content/) | Fighter and stage development notes |
| [`investigations/`](investigations/) | Bounded boundary investigations and research records still cited by current work |
| [`history/`](history/) | Superseded checkpoints, dated campaigns and resolved incidents, kept for provenance |

New documents go in the narrowest matching directory. A one-off investigation
belongs in `investigations/`, not at the top level. When its conclusion lands
in a contract or a status entry and nothing current depends on it, move it to
`history/`.
