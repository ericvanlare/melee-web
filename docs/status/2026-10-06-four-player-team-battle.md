# Four-player Team Battle admission

**Source identified / Compiled** for the widened Team Battle admission rule, with
validator-level native unit coverage. **Browser exercised is not claimed:** the
four-player 2v2 headless browser run has not been executed (see
[Open gates](#open-gates)). This is tracker #158 item B3; it does not close it.

The [scoped receipt](../evidence/four-player-team-battle-admission-v1.json)
binds the source routines, the admitted and refused setup table, the test
files and the build identity.

## What the source allows

The pinned decomp (`doldecomp/melee` at the lockfile commit) bounds Team Battle
in the original VS CSS (`src/melee/mn/mncharsel.c`) and nowhere else:

- `cycleTeam` advances a door's team with `(team + 1) % 3`, so a door holds one
  of three authored team colours (0..2). The team-dependent costume selectors
  are `gm_80169264` (colour 0), `gm_801692BC` (1) and `gm_80169290` (2).
- `fn_80262F44` shows CSS Start when at least two doors are active and, with
  `is_teams == 1`, some pair of active doors has different teams. It places no
  other bound on the roster.
- The VS CSS owns four doors (`mnCharSel_804D6CF5`), and `MatchEnd` carries
  per-team standings (`GM_MAX_TEAMS` is 5) with `OUTCOME_TEAM_ELIMINATION`.

So the source starts 1v1, 2v1, 1v1v1, 2v2, 3v1 and 2v1v1. It does not start
four players on one team, a single active player, or a door with a fourth team
colour (the CSS cannot produce one).

## What was admitted

The two-player limit lived in three independent places. All three now call one
shared rule, `melee_web_team_setup_supported` in
`src/gameplay_player_selection.h`:

| Boundary | Location | Before | Now |
| --- | --- | --- | --- |
| CSS exit, SSS and match handoff (and in-progress CSS) | `team_selection_valid`, `src/gameplay_menu.c` | exactly two players | 2–4 contiguous players, colours 0..2, two different teams (in-progress CSS waives only the opposing-team clause) |
| Match rules prepare/init | `supported_team_setup`, `src/gameplay_match_rules.c` | exactly two players | the shared rule |
| Original `fn_8016DCC0` boundary | `melee_web_match_validate_source_start`, `patches/melee-gameplay.patch` | exactly two players | the shared rule |

The in-progress CSS check used to reject a third door joining while Teams was
on; it now accepts any door count that the source CSS can show, while CSS exit
and match admission still require opposing teams.

Still refused with an explicit error: four players on one team, one player, a
team colour above 2, `is_teams` other than 0/1, non-contiguous doors, any other
CPU kind, and any rule profile outside the existing stock/timer/item-None
admission. The match-rules owner reports a Team Battle refusal separately from
an unsupported stock/stage rule:
`Team Battle payload is outside the supported setups: two to four contiguous
players, team colours 0-2, and at least two players on different teams`.

## Evidence in this entry

- `tests/test_gameplay_team_setup.py` compiles
  `tests/gameplay_team_setup_trace.c` with the exact
  `melee_web_match_validate_source_start` text extracted from the canonical
  patch. It checks 2v2, interleaved 2v2, 3v1, 2v1v1, 1v1v1, 2v1, 1v1, free-for-all
  and every refusal above, then compares the shared rule and the patched
  validator against an independent transcription of `fn_80262F44` for every
  flag, roster size 0–5 and 4^4 colour assignment. Reintroducing the old
  two-player bound into the extracted validator fails the 2v2 case.
- `tests/gameplay_match_item_mask_trace.c` runs `melee_web_match_rules_prepare_from_menu`
  and `init_from_menu` for the same setups and checks the exact refusal text.
- `tests/gameplay_menu_trace.c` (in both the development and public builds)
  covers the CSS, SSS and leave-CSS gates, a third and fourth door joining while
  Teams is on, and the refusal of a fourth colour.
- `tests/fighter_cpu9_lineup_browser_test.mjs` gained `--lineup M` (four Mario),
  `--teams T0,T1,T2,T3` and `--friendly-fire`. They drive the original CSS Teams
  control, each door's team box and Rules Plus, and assert the live source
  `StartMeleeData` teams, the source `OUTCOME_TEAM_ELIMINATION` terminal outcome
  and the CSS team state after Results. Argument validation and syntax were
  checked; the scenario itself has not run.
- Reused: the CPU9 lineup harness (CPU door setup, natural-match detection,
  Results return) and its `scripts/browser_driver.mjs` readiness helpers, the
  two-player Teams control and team-box input recipe from
  `tests/vs_rules_item_menu_browser_test.mjs`, and the CPU conventions in
  `docs/CPU_OPPONENTS.md`. Rejected: a new bespoke harness. The two-player
  Teams route harness keeps working against the preserved `player_teams`
  observation field but was not re-run here.

## Open gates

1. **Four-player 2v2 browser run (not run).** The development runtime build
   passes, but the headless run could not start: `MELEE_PLAYWRIGHT_DIR` is unset,
   no installed Playwright package exists on the machine, and installing one
   needs a network download that was not authorized for this task. The first
   attempt failed with `Playwright is unavailable` before launching a browser
   (retained under ignored `work/four-player-teams/`). Run, with an owned disc:

   ```sh
   python3 scripts/serve.py --directory build/browser-release
   node tests/fighter_cpu9_lineup_browser_test.mjs \
     --url http://127.0.0.1:8787/runtime.html --disc OWNED_CISO \
     --out work/four-player-teams/2v2 --lineup M --teams 0,0,1,1 \
     --playwright PACKAGE_DIR --build-dir build/browser-release
   ```

   Pass criteria: Teams on with four CPU level 9 doors (two red, two blue) on the
   original CSS, Final Destination on the original SSS, a natural four-stock
   match, source `OUTCOME_TEAM_ELIMINATION`, original team Results, and CSS
   return with the team setup retained; screenshots at CSS, SSS, in-match,
   Results and returned CSS. Then re-run the two-player Teams route and the
   Mario/Final Destination route.
2. Friendly fire: `--friendly-fire` reaches Rules Plus row 1 through the original
   menus (`GameRules.friendly_fire`); unrun.
3. 3v1 and 2v1v1 browser runs (`--teams 2,2,2,0`, `--teams 1,0,1,2`).
4. Retail comparison of the 2v2 (reference capture of the original route).

## Exclusions

Public-player ports 3/4 (`web/player/*`), physical controllers, Sudden Death,
non-contiguous doors, retail comparison, and any timing, pixel, audio or
performance claim. Admission here is an implementation limit, not accuracy
acceptance.
