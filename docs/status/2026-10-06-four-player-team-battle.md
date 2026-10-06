# Four-player Team Battle admission

**Source identified, compiled, and exercised through bounded headless functional
routes.** The original CSS → SSS → four-player Mario CPU9 Team 2v2 with friendly
fire and four stocks on Final Destination → natural source outcome → team
Results → CSS route passed after a source-bound outcome-adapter fix. This is
functional evidence for the tested route, not full supported-session or
retail-accuracy acceptance. Tracker #158 item B3 remains open.

The [scoped receipt](../evidence/four-player-team-battle-admission-v1.json)
binds the current source, builds, test logs, browser reports, and external
evidence locators.

## Source admission boundary

The pinned source bounds the original VS CSS Team Battle route in
src/melee/mn/mncharsel.c:

- `cycleTeam` advances a door with `(team + 1) % 3`; authored team colours are
  0–2.
- `fn_80262F44` shows CSS Start when at least two doors are active and, in
  Teams, some pair has different teams; it imposes no other roster bound.
- The VS CSS owns four doors. `MatchEnd` has per-team standings and
  `OUTCOME_TEAM_ELIMINATION`.

The shared rule `melee_web_team_setup_supported` is used by the CSS/SSS/match
handoff, match-rules preparation, and patched `fn_8016DCC0` boundary. It admits
2–4 contiguous players, colours 0–2, with at least two different teams when
Teams is on. In-progress CSS admits source-reachable partial setups. It refuses
a single active player, all players on one team, colour above 2, invalid Teams
flag, non-contiguous doors, unsupported CPU/rule profiles, and unsupported
stock/timer/items with explicit errors.

## Outcome adapter correction

The first natural 2v2 run reached vanilla `OUTCOME_TEAM_ELIMINATION` (3), but
the ordinary VS source-frame adapter treated it as unsupported and stopped
before Results. The patch now accepts that outcome only when the source match
is a Teams match. It preserves the existing source callback, opening-demo
behavior, ordinary outcomes, and all preconditions. The extracted-adapter
native regression exercises Teams outcome 3, rejects outcome 3 outside Teams,
keeps outcomes 0/1/2/7, rejects other values, and checks ten source
preconditions.

The development CSS observer also now allocates the full native write shape:
14 int32 IDs and 8 float geometry values. Its focused test checks the actual
helper and cleanup cases. The lineup harness waits for the source Main owner
to be ready before releasing each CSS PAD edge; no runtime input guard or
clock threshold was weakened.

The route reuses the existing CPU9 lineup harness for door setup, natural-match
detection, and Results return; the existing two-player Teams harness provides
the CSS/team-colour input recipe, and browser_driver provides readiness helpers.
No new bespoke integration harness was added.

## Validation

- Canonical patch check passed at tree
  `df37a6ac1c1176fd224b9a0e7a843a254995b525`. Extracted adapter, shared Team
  Battle setup, and existing match-flow focused tests passed.
- Final suite passed: 1,903 tests, 141 skipped, in 342.218 seconds. Full stdout
  and exit footer are retained in the run evidence.
- Development and public Release builds for source `11ad970` exited
  successfully. Their incremental build stdout was emitted by the build tool
  but not retained as full logs. The complete Release logs in this run are from
  the earlier source `3ec18a8`; the receipt keeps those distinct from current
  served-artifact hashes. The standard RelWithDebInfo gameplay graph was
  configured for suite prerequisites.
- Natural four-player Team 2v2: four Mario CPU9, teams `[0,0,1,1]`, friendly
  fire on, four stocks, Final Destination; terminal source frame 12030, outcome
  3, complete, published source winner slot `[1]`. Results showed the red team
  with both red players in first place, then returned to CSS. This records one
  published winner slot, not an array containing both teammates.
- Ordinary non-Teams four-Mario CPU9 control: four stocks on Final Destination;
  natural outcome 2 at frame 14387, winner slot `[3]`, Results → CSS.
- Existing two-player Teams browser route passed its 17 assertions, including
  live two-player three-stock Teams configuration and original Results/menu
  flow. It uses the existing No Contest route to reach Results; it is not a
  natural-match win.
- Public preview package and local HTTP audit passed. The headless public smoke
  passed 19 checks for the packaged public player and keyboard UI/lifecycle,
  using a harness-induced No Contest path. That package uses the audio-disabled
  runtime profile, so this is not audio evidence.

Development captures used headless installed Chrome with host speakers muted;
WebAudio/PCM processing remained enabled in the development runtime. The
browser reports bind pre/post artifact identity and source cleanliness. The
public package also has equal pre/post package inventories and a retained
manifest.

## Retained failures

The pre-fix natural 2v2 failure and the first public smoke attempt are retained
with their reports. The public attempt used `scripts/serve.py`, which does not
apply the package’s CSP headers; the changed hypothesis used pinned Wrangler
Pages dev and passed the preview smoke. The local Wrangler HTTP report retains
its known `_headers` 502 behavior; hosted 404 behavior was not tested.

## Open gates and exclusions

Unrun: natural 3v1 and 2v1v1 browser matches; retail comparison; physical input;
foreground timing; uninterrupted audio and PCM fidelity; pixel equivalence;
performance; Sudden Death; public-player ports 3/4; sparse door layouts; and
wider supported-session acceptance. The public-player smoke is a package/UI No
Contest smoke only. No deployment or merge was performed.
