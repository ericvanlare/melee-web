# Full-game port inventory and integration workflow

`docs/full-game-inventory.json` is the versioned planning boundary for the
offline vanilla GALE01 revision-2 port. It is deliberately a source-derived
inventory, not a completion claim. The source checkout is pinned by
`dependencies.lock.json` to Melee commit
`b43912cc78606f96c9569f5d6229bc9d7e265ea5`; `source_refs` are relative to
`.deps/melee/src`, and evidence paths are repository-relative.

The inventory covers the whole offline product boundary: fighters and moves,
costumes and alternate parts, transformations, partners and bosses, stages,
items and CPU, local multiplayer and VS rules, menus and results, unlocks,
trophies, single-player modes, minigames, events, movies, save and memory-card
behavior, audio, graphics, input and browser lifecycle. Netplay, online
services, custom content and non-vanilla revisions are later scope. The
inventory keeps those exclusions explicit so a missing offline feature cannot
be mistaken for an online or product-extras task.

The current local-versus priority and milestone are tracked in the
[roadmap](ROADMAP.md); this inventory does not duplicate its changing acceptance
details.

The fighter census has one row for each source selectable identity through
`CKIND_PLAYABLE_COUNT` (including separate Zelda/Sheik identities and the
single Ice Climbers selection that constructs Popo and Nana). The VS-stage census has one row for each named `StKind` from
`St_Kind_Izumi` through `St_Kind_Last`, retaining the source `GrKind` mapping,
including shared or unavailable source slots. Each child row carries optional
`source_identity` metadata with the exact `ckind`/`ftkind` or `stkind`/`grkind`
names. Aggregate fighter and stage rows remain dependency anchors; child rows
depend on those anchors, while the current eight-fighter and four-stage bounded
rows depend on their relevant children. This direction avoids dependency cycles.

## Read-only inventory report

Generate a fresh local report in an unused directory under ignored `work/`:

```sh
python3 scripts/full_game_report.py \
  --inventory docs/full-game-inventory.json \
  --out work/full-game/report-001
```

The command checks the dependency lock against the clean pinned Melee checkout,
validates schema, feature IDs, dependency edges and source/evidence paths, and
retains the original symbol/split inventory. It writes `report.json` and a
searchable `index.html`; it does not compile, launch a browser, infer linker
reachability, or convert source symbols into accepted gameplay. The report keeps
browser-linkage, reference-tested and full-game acceptance fractions `null`
until those separate evidence producers are ingested. Use a new output
directory for each run so an old report cannot be mistaken for current source
or port state.

The report is intentionally independent of the feature declarations when it
validates the source pin and paths. Lead-owned validators and acceptance
reports remain the authority for runtime claims. `STATUS.md` is the changing
evidence index; this document and the JSON point to it and to retained receipts
without copying its measurements.

## Long-term integration order

Follow the active roadmap before choosing a full-game expansion. Work from
current main in a dedicated branch with small, reviewable slices. A slice may
own a bounded set of inventory rows and its implementation/evidence, while the
lead owns central build integration, cross-row dependency decisions and final
validation.

1. Close the shared source boundary first: archive and file completion, memory
   and heap ownership, object/process scheduling, source clocks, input sampling,
   graphics resources, audio ownership and browser preparation/teardown. A
   missing service must fail at its owning boundary; a successful stub is not a
   feature.
2. Decompose and harden the starting eight-fighter/four-stage cohort. Preserve
   each CharacterKind/FighterKind and StKind/GrKind identity, authored table
   bounds, costumes, actions, articles, effects, music, map services and
   teardown. That cohort's eight fighter rows and four stage rows are
   explicitly `partial` implementation and `partial` scoped acceptance; they
   remain below full-game
   acceptance until its ordinary-input, reference, pixel, PCM and performance
   gates are complete.
3. Expand VS breadth through the same source-owned interfaces: the remaining
   roster and stages, all item/article families, CPU levels and decisions, four
   local players, teams and every VS rule. Add one source-identity row per
   fighter, stage, item family and rule family before changing a category row to
   an aggregate claim.
4. Add the original menu graph and persistence boundaries: title/main menu,
   CSS/SSS, rules/name screens, match ending, Results, unlocks, trophies,
   gallery and save/memory-card recovery. Merged Results PR #44 and merged audio
   PR #42 are inventory dependencies; Results is tracked by issue #34. Their
   merged status does not convert inventory rows into accepted gameplay.
5. Port the source-owned single-player graph: Classic, Adventure, All-Star,
   Event Matches, Training, How-To, Multi-Man, Target Test, Home-Run and related
   Stadium/tournament flows. Then add opening/staff/movie playback and the
   collection/gallery routes, keeping mode-only fighters, stages, items and
   unlock conditions distinct from normal VS rows.
6. For every admitted slice, run source lifecycle checks before a long browser
   run, reduce the first divergence before another timed replay, and retain
   independent original comparison, pixel, PCM, physical-input, lifecycle and
   cold/warm performance evidence. A recorded controller queue is conditional
   evidence and does not establish live timing or input equivalence.

## Asset residency boundary

The owned USA revision-2 FST audit finds 110,925,567 bytes in 224 unique paths
for the full playable fighter set: 27 source fighter kinds, including Nana and
the separate Zelda/Sheik forms, rather than 27 CSS slots. The 29 concrete VS
ground archives occupy another 28,856,907 bytes. Deduplicating these with the
Jigglypuff checkpoint's existing menu/shared/music imports yields 287 paths and
192,035,176 bytes. This is a lower bound: remaining stage music and other game
modes are still outside that union.

That lower bound exceeds the browser's 128 MiB requested-file budget by
57,817,448 bytes. Full-roster integration therefore needs explicit scene asset
plans and teardown ownership. Keep executable identity, exact FST paths,
per-file bounds and complete-request preflight; derive each menu or match plan
from the source identities and selected stage's authored music candidates.
Release retained bytes only after the corresponding native owners close.
The cap must not be raised implicitly or missing assets made optional to admit
the roster. The ignored audit is retained at
`work/full-game/full-roster-import-boundary-v1.md`; its totals describe the
Jigglypuff checkpoint, not browser memory use or gameplay readiness.

## Status and acceptance rules

Each feature has a stable kebab-case ID, a category, source references,
dependencies, concrete pass criteria, evidence paths, a next step and an issue
owner when one exists. `implementation` means how much of the port boundary is
wired (`not_integrated`, `partial` or `integrated`). `acceptance` is a separate
evidence gate (`not_evaluated`, `partial` or `passed`). An integrated source
adapter is not automatically accepted gameplay, and a source file or parser is
not a browser or retail comparison.

Only mark a row `passed` when its stated criteria have a retained, hash-bound
or otherwise scoped receipt. Keep failed, skipped and untested cases visible in
the row's next step or evidence. Do not use aggregate percentages, an average
FPS, a compile, a synthetic scene, a short prefix or equal callback counts as a
full-game result. Preserve original arithmetic and float bits, RNG and music
selection, input and draw order, process ownership and source table bounds.

The fighter and VS-stage rows are now decomposed from the pinned source enums,
but the broad item, mode, single-player, movie and service rows remain
category-level until their source tables and callers receive the same treatment.
Ganondorf is a `partial` development candidate with scoped native lifecycle
and browser-entry evidence in [its port notes](GANONDORF_PORT_NOTES.md). Keep unused, route-only, event-only and boss entities visible rather
than silently dropping them.

Current development and deployed inventories, fighter-specific limitations and
release identities belong in [STATUS](../STATUS.md), not a copied roster count
here. Integrated content is not automatically admitted. The player uses
[scene-specific asset scopes](SCENE_ASSET_LOADING.md) with explicit source
teardown and asynchronous reads while its clock is stopped. The
[production audio release path](AUDIO_PRODUCTION.md) defines the audio-player
profile and silent rollback separately. Refer to their scoped receipts before
making residency, audio, mode or broader gameplay claims.

## Validation before handoff

For inventory-only updates, validate JSON syntax and run the report command
above. Changes to the report generator also require its focused tests. Before integrating an implementation slice, follow the focused
boundary check in `docs/DEVELOPMENT.md`, the fighter/stage checkpoint documents,
and the performance/accuracy playbook. Inspect the diff and preserve any
preparation error or failed acceptance gate. The inventory is complete for its
version only when every declared row has valid source/evidence paths and every
remaining limitation is stated; it is not complete gameplay by itself.
