# Ganondorf development integration

This is work on `codex/full-game-integration`, based on the eight-fighter,
four-stage runtime. Ganondorf is a development candidate; the public release
and the full-game acceptance boundary are unchanged.

## Original source and data boundary

Ganondorf is `CKIND_GANON` / `FTKIND_GANON` (25). The original fighter uses
Captain-family callbacks with its own 318-row action/animation table,
`ftCaptain_DatAttrs` (0x8c bytes), model trees, effects and sound bank. The source
costume table has five entries: neutral, red, blue, green and lavender.
**All five authored material-animation symbols are null.** The adapter now
preserves that source distinction; it still requires a real material-animation
root for costumes whose source table names one. Unknown fighter/costume pairs
remain errors.

The owned `PlGn.dat` / `PlGnAJ.dat` decode confirms three dynamic bones,
no dynamic collision spheres, two authored dynamics modes and no fighter
Article table. Mode counts follow the authored action blend selectors.
`EfGnData.dat` owns `effGanonDataTable`: bank 19, six source table entries.
`ganon.ssm` is the source sound bank. Owned bytes and generated binaries stay
outside Git.

The runtime carries the complete Captain attribute layout across its C/C++
boundary, admits the bounded Ganon self-motion command graph and preserves
original command consumers. Opcodes 14, 15, 21 and 50 were already decoded but
missing from readiness. They now reach the original hitbox flag/disable,
throw-flag and dynamics callbacks. Hitbox indices reject values outside the
source four-capsule array. The audited admitted roots include call/jump targets;
item-only opcodes outside those roots remain explicitly unsupported.

The source CSS has 25 icon positions but 26 playable CharacterKind values:
Zelda and Sheik share an icon. Its raw-input diagnostic observation now uses
`CKIND_PLAYABLE_COUNT`, allowing Ganondorf's final identity without changing
the source icon count or selection behavior.

## Observed scope

Fresh Release `gameplay_content_match_trace` runs pass both Ganondorf/Mario
orientations on Final Destination, reconstructing all five selected costumes.
They exercise the original intro, grounded and aerial neutral special,
opponent damage, source pause/No Contest and repeated world teardown. The
Ganondorf-first run also enters Raptor Boost, Dark Dive and Wizard's Foot and
returns to grounded common actions. Warlock Punch produces 30 damage in this
authored diagnostic. These runs use fixture setup and raw PAD; they are native
lifecycle evidence, not an original-state comparison or complete move coverage.

A headed Chrome development run imports an owned disc, selects Ganondorf
through original CSS raw PAD, selects Final Destination through original SSS,
reaches Ready/Go and advances 30 observed gameplay frames with Ganondorf and
Mario rendered. It retains first-use pipeline creation and timing pauses;
explicit diagnostic resumes allow functional exploration but cannot pass a
performance gate. No physical-controller, pixel or PCM equivalence is claimed.

The complete 30-case `ganondorf-visible-actions-v1` development sweep reaches
all expected ground/air entries over 5,200 source frames. Two discovery runs
retain timing, audio-underrun and live-pipeline failures; one runs during the
regression suite. They establish action reachability only. The cleared-origin
export contains 37 new portable pipeline descriptors. Review preserves all 628
prior rows (one shader and 627 pipelines), verifies no existing payload changes,
and adds only the 37 descriptors. The resulting seed contains 664 pipelines
and one shader. A fresh cleared-origin follow-up on Chrome 153.0.8010.50
passes the same 30 cases and 5,200 source frames with no timing gaps, long
tasks, native target misses, audio underruns, live pipelines, preparation
pauses, automatic resumes, focus losses or heap growth. Native/browser maxima
are 9.965/27.605 ms. A following frozen cold/warm pair passes both 30-case,
5,200-frame runs with the same zero-failure counters. Native/browser maxima
are 9.595/27.170 ms cold and 9.555/27.655 ms warm. Both runs record a 640×480
backing canvas, 900×675 CSS size, DPR 1 and a 1280×960 viewport on Apple M4,
macOS 26.6.2, Chrome 153.0.8010.50. Warm means a complete unload, awaited cache
save and full application reload in the same browser context. This establishes
that authored action pair; original comparison, the full costume/interaction
matrix and broader fighter gates remain separate.

The narrow rigid-model asset inspector rejects the costume materials under its
restricted preview policy. That failure is retained separately from the full
native HSD ownership path, which constructs the costumes. It is not an exemption
or a successful rendering comparison.

An independently repeated original-source diagnostic now retains two exact
603-frame Ganondorf/Mario/Final Destination captures with matching declared
entry, input, PAD, RNG, clock and fighter fields. The legacy version-3 native
transport is retained with its first `match_enter_complete.rng` mismatch; a
separately save-bound version-4 recipe, using the hash-verified original GCI
profile and the identical input payload, reaches a declared-state match across
all 603 frames. The scoped receipt is
[ganondorf-original-reference-v1.json](evidence/ganondorf-original-reference-v1.json).
The accompanying headless CPU sidecar still diverges at tick 1 on
`camera.far_bits`; drawing rows are excluded from that comparison. This evidence
does not claim drawn equivalence, a complete match ending, performance, pixels,
PCM, physical-input fidelity, a human holdout, gold admission or public
acceptance.

After the Ground light/OnLoad lifecycle rebuild, one fresh run of the unchanged
save-bound v4 recipe retained the same declared-state match across all 603
frames. A second refresh after the final Luigi/Fountain Article validation
changes also matches all 603 frames and produces the same trace hash. Both
build identities are recorded in the scoped receipt; these refreshes do not
broaden the field or acceptance scope.

Both development and public Release builds succeed. The complete regression
suite passes 1,048 tests with 43 optional skips after correcting two stale
prototype roster expectations. GitHub Verify passes checkpoint `ca692dd` in
6m 32s. Fresh native lifecycle checks also pass all
eight existing fighters, and the retained 240-frame Mario original pair matches
the fresh port for its declared entry/input/fighter/RNG/clock fields. This small
regression excludes drawing, global PAD history, pixels, PCM and performance.
The [hash-bound development receipt](evidence/ganondorf-development-v1.json)
retains the measurements and failed discovery attempts.

## Retained failures and next gates

Early construction diagnostics exposed the missing Ganon command-family gate,
dynamics schema, and an eye-animation telemetry assumption that required two
material TObjs for every fighter. The latter now follows the original authored
null material and zero-TObj contract. The next runs exposed command 21 in
Warlock Punch and command 15 in Dark Dive; focused original-consumer tests and
a reachable-command audit cover the corrections above.

Browser attempts retained a rejected CharacterKind bound, one invalid run made
from stale output after a failed build, an early Start sample during the
original ten-tick CSS cooldown, cold rendering pauses, and a harness click on
a collapsed diagnostic panel. Those attempts do not count as acceptance.

Remaining gates include the complete visible action and interaction corpus,
independent original comparison, all costume/opponent render states, reviewed
pipeline coverage, cold/warm performance, complete match endings and repeated
menu loops, pixels, PCM and physical input. Catch/throw follow-ups need actual
opponent interactions; entering a special does not establish those branches.
The [whole-game inventory](FULL_GAME_PORT.md) keeps this candidate separate
from full fighter admission.
