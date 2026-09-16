# Adding a source fighter

Character expansion starts from the pinned GALE01 revision 2 source and disc.
The registry is generated from the original `ftdata.c` and `forward.h`; hand
written rows are not a substitute for source identity. Keep extracted assets in
the ignored `assets-local/next-gate` directory and raw captures in ignored `work/`.
Keep upstream checkouts intact and put downstream source changes in `patches/`.

## Before editing

This is required reading for adding or enabling a fighter. Start with the current
[status](../STATUS.md), [accuracy contract](ACCURACY_CONTRACT.md) and
[performance/accuracy playbook](PERFORMANCE_AND_ACCURACY.md). Record the branch,
HEAD and worktree status; preserve unrelated changes and other owners' worktrees.

Verify that the chosen base includes the latest shared fixes. The Roy/Doc replay
baseline is commit `29d2dc71f5f4104935fbc09fb3789f8cf2580a16`, with its
[recorded-queue contract](RECORDED_QUEUE_REPLAY.md) and
[evidence ledger](evidence/recorded-queue-replay-v1.json). A later integration may
carry equivalent changes under different commits; check the implementation and
evidence instead of blindly cherry-picking. Character notes retain chronological
failures and superseded experiments. Their earlier passing runs are not the
current acceptance decision.

## Roy/Doc lessons that must survive the next port

- **Shared routines do not imply identical data layouts.** Keep character-select
  kinds and internal fighter kinds distinct. Derive each fighter's attributes,
  actions, costumes, articles, effects, audio and relocated table extents from its
  own source and DAT. Roy's authored blend selectors require six dynamics modes;
  copying Marth's five-mode extent crashed during combat. Reuse family adapters,
  but validate every referenced row and reject missing dependencies explicitly.
- **Check initialization before blaming a move.** The first Roy/Doc RNG mismatch
  came from the original stage-music selector and captured save unlock masks.
  Music selection consumes gameplay RNG even in silent builds. Restore the
  original routine, context and lifetime; never adjust a seed or insert an
  unexplained RNG call to align a trace. Keep unlock state separate from runtime
  availability, and preserve each capture's actual save/setup context.
- **A one-bit float mismatch can expose a shared bug.** Roy uncovered original
  fused-operation boundaries in joint transforms, vector rotation and the throw
  hip adjustment. Compare original operand bits and producer/consumer boundaries
  before changing arithmetic. Preserve fused and separately rounded operations
  exactly where the original uses them; no blanket fusion, fast-math, tolerance
  widening or character-specific offsets. Keep observed scalar cases and the old
  failing implementation as a negative control.
- **Source drawing can affect simulation.** Camera/magnifier callbacks can affect
  later damage and RNG. Headless prefixes help locate defects but cannot establish
  drawn gameplay equivalence. For recorded-queue comparisons, use validated MWRC
  v6 fixtures and compare exact source draw boundaries as well as state. Browser
  callback counts and equal final draw totals are insufficient. Do not feed
  expected fighter/RNG/camera state into runtime or fit a draw-skip list.
- **Keep live timing separate.** The startup-phase-only model was disproven on
  the second capture by an original audio interrupt delaying a queue check.
  Recorded-queue replay is conditional gameplay evidence and leaves the live
  controller path unverified. [Issue #28](https://github.com/ericvanlare/melee-web/issues/28)
  tracks that deferred work; character expansion does not require reopening it.
  Preserve any new live symptoms and the historical GPU-stall failures.
- **New effects need their own preparation evidence.** An existing descriptor
  catalog does not automatically cover a new fighter. Discover missing pipelines
  in visible gameplay, preserve the failures and update only the intended catalog
  with reviewed compact descriptors and binding hashes. Keep raw per-draw data
  local. Preparation must not run hidden gameplay, draw source callbacks, consume
  RNG or omit effects; report preparation time separately and retain cold/warm gates.
- **Repair the owner of the first divergence.** A new fighter often exposes a
  shared decoder, lifecycle or math defect. Fix that boundary and rerun affected
  existing-character regressions. If the cause is not established, retain a small
  reproducer and report the unknown; do not add guessed success paths. Both human
  holdouts remain unopened during implementation.

## Fast development iteration

The [Dr. Mario/Roy pass](ROY_DR_MARIO_PORT_NOTES.md) separates a playable
development candidate from content admission. Use this bounded sequence before
asking for a new original-game capture:

1. Identify the source initializer, family ABI and every owned dependency.
   Reuse a decoded family schema while retaining the new fighter's own DAT,
   article kinds, effect bank and audio. Being a clone is not a data substitute.
2. Extract and hash the dependencies once. Compile the portable parser once
   and the model checker once, then check every costume with those binaries.
3. Run the native pair in both player orientations, with all costumes, special
   families, article creation/destruction, damage and repeated teardown.
4. Build Release after the native boundary is stable. First run the cheap
   browser/native upload-manifest parity and generated-roster tests. Finish the
   build and freeze the served JS/Wasm/data together before testing; rebuild any
   trace executable affected by the change. Then enter
   through visible source CSS/SSS and exercise a small named action subset.
   Use source-position feedback for test positioning, rather than assuming
   fixed dash durations work for every fighter.
5. Keep cold discovery and warm reload results separate. Preserve new-pipeline
   misses and timing failures; an action reaching its expected source state is
   not a performance pass. Add a retail donor for the new pair before widening
   the matrix or certifying renderer coverage.

Run the repository suite once per coherent implementation, then rerun the
failed or changed boundary during iteration. Avoid recompiling an asset checker
per costume or rerunning every route after a manifest-only correction. Complete
the admission gates below before promotion; the bounded loop does not replace
them.

Once a repeatable original capture is available:

1. Check input/setup hashes and fixture validity before expensive execution.
   Reuse immutable verified reference traces unless the reference or capture
   boundary changes. Preserve all failed attempts.
2. Find the first differing source phase and field. Check construction context
   and operands, then reduce the failure to a scalar test or short native prefix.
   Headless/drawn differences require a drawn reproducer. Fix and rerun the small
   test before spending another complete-match run.
3. Run the complete visible comparison after the narrow checks pass. Require all
   declared state fields, original draw boundaries and actual source match ending;
   input-stream completion alone is insufficient. After shared changes, rerun the
   existing comparisons that exercise that boundary, including the Roy/Doc pair
   when changing their shared math or queue replay behavior.
4. At the integration checkpoint, build affected targets, run
   `python3 -m unittest discover -s tests -v` and inspect the diff. Report skips
   explicitly; a missing asset or stale executable is not successful validation.
   Broaden or repeat expensive checks only for new changes, failures or unresolved
   coverage. A passing state comparison does not remove cold/warm performance gates.

Useful existing examples are the
[clone dependency/lifecycle notes](ROY_DR_MARIO_PORT_NOTES.md),
[real-asset test](../tests/test_clone_fighters_real_assets.py), and the compact
[joint-transform](../tests/test_gameplay_srt.py),
[vector-rotation](../tests/test_gameplay_vector_rotation.py) and
[throw-rounding](../tests/test_gameplay_throw_smoothing.py) regressions. Extend the
relevant boundary rather than building another general validation framework.

At handoff, record the exact head/build and fixture identities, implemented
dependencies and shared fixes, commands and scoped results, retained failures,
and next blocking gate. Use the playbook's evidence labels. Keep development
candidacy, retail state comparison and performance admission distinct; passing
two matches does not certify every move, costume or stage.

## Repeatable workflow

Set the paths to the owned disc and source checkout, then extract every source
file used by the character. For Falco, the complete first pass is:

```sh
DISC='/path/to/owned.ciso'
OUT='assets-local/next-gate'
mkdir -p "$OUT"
for file in PlFc.dat PlFcNr.dat PlFcRe.dat PlFcBu.dat PlFcGr.dat PlFcAJ.dat EfFxData.dat; do
  python3 scripts/extract_disc_file.py "$DISC" "$file" --output "$OUT/$file"
done
# The source sound archive is named by the original ssm_files table.
python3 scripts/extract_disc_file.py "$DISC" audio/us/falco.ssm --output "$OUT/falco.ssm"
python3 scripts/generate_fighter_registry.py --melee-root .deps/melee --check
```

Fox follows the same extraction shape with `PlFx.dat`, `PlFxAJ.dat`,
`PlFxNr/Or/La/Gr.dat`, `EfFxData.dat` and `audio/us/fox.ssm`. Its exact source
contract and hashes are recorded in [FOX_PORT_NOTES.md](FOX_PORT_NOTES.md).

Confirm each costume against its exact source model symbol. Falco's four model
symbols are `PlyFalco5K_Share_joint`, `PlyFalco5KRe_Share_joint`,
`PlyFalco5KBu_Share_joint` and `PlyFalco5KGr_Share_joint`; the costume DATs are
separate material/texture owners. The action container is `PlFcAJ.dat`, not an
ordinary DAT root. A model parser check can be run for every extracted costume:

```sh
python3 scripts/check_assets.py "$OUT/PlFcNr.dat" --symbol PlyFalco5K_Share_joint
python3 scripts/check_assets.py "$OUT/PlFcRe.dat" --symbol PlyFalco5KRe_Share_joint
python3 scripts/check_assets.py "$OUT/PlFcBu.dat" --symbol PlyFalco5KBu_Share_joint
python3 scripts/check_assets.py "$OUT/PlFcGr.dat" --symbol PlyFalco5KGr_Share_joint
```

The focused source checks compile a bounded native runtime and skip only when
the local assets are absent:

```sh
python3 -m unittest tests.test_dat_fighter_runtime tests.test_gameplay_fighter_data \
  tests.test_gameplay_fighter_attributes tests.test_gameplay_fighter_assets
python3 -m unittest tests.test_falco_real_assets
```

The centralized Wasm build must then run the character match trace. It binds
the original `Fighter_Create` rows, selects a common action, executes the
source action lanes covered by the trace (including laser paths), and tears
down the owned descriptors before a second initialization. The real-asset
trace also selects and decodes recovery rows; that selection check does not
claim recovery execution. A successful DAT parse by itself does not make a
character playable.

## Dependency audit

Use the source initializer and the disc bytes together. For each new fighter,
record the numeric `FighterKind`, action count, costume rows, model/material
symbols, animation container, article table slots, effects table, and sound
archive. Follow pointers from the exact public root and reject a missing or
ambiguous relocation; do not infer a dependency from a filename alone.

Falco's revision 2 dependencies are:

| Source role | Exact identity | Why it is required |
| --- | --- | --- |
| Fighter data | `PlFc.dat`, `ftDataFalco`, `FTKIND_FALCO` (22), 327 actions | Original base/extended attributes, action rows and article roots |
| Costume/model | `PlFcNr.dat`, `PlFcRe.dat`, `PlFcBu.dat`, `PlFcGr.dat`; `PlyFalco5K_Share_joint` | Four material owners for the source costume table |
| Animation | `PlFcAJ.dat` | Source FigaTree streams for all nonempty motion rows |
| Articles | Slots 0, 1 and 3; item kinds at `ftFox_DatAttrs+0x1c` and `+0x20` | Falco laser, blaster and Phantasm article constructors |
| Effects | `EfFxData.dat`, `effFoxDataTable`, bank 3, six entries | Falco uses the original Fox effect table (`ftData_UnkBytePerCharacter[22] == 3`) |
| Audio | `falco.ssm` | Falco voice and fighter SFX bank from `ssm_files` slot 10 |

Falco and Fox share the `ftFox_DatAttrs` 0xd4-byte extension and the original
`ftFx_Init_OnLoadForFalco` dependency. The extension is decoded by numeric
fighter kind, while the article kinds remain the source values in the DAT.
The shared effect table is loaded once per effect bank and is not duplicated
under a Falco-specific symbol.

Fox exposed two runtime requirements that a metadata-only check cannot find.
Its nonempty `ftDynamics` needs the original dynamics pool initialized inside
each scoped SDK heap before `Fighter_Create`, and its Illusion Article uses item
script opcodes 12 and 14. Decode the authored chain parameter rows into their
original `DynamicsDesc` view and test the live constructor; a scalar dump alone
does not prove the per-Fighter linked chain can be built.

## Shared integration boundary

Character work owns the fighter data, relevant family attributes, action and item
decoders, costume/model owners, player-kind mapping, and the focused real-asset
checks. The shared world and match session own one asset scope per requested
`FighterKind`, effect-bank and audio loading, source lifecycle ordering, and
the final CSS/SSS admission. Menu and browser changes must keep source menu
navigation as the authority for selection; a registry row alone does not
admit a fighter.

Before integrating a new character, send the lead the numeric kind, article
slots and kinds, effect symbol/bank/count, audio path, borrowed family data,
and constructor requirements. Keep extraction paths configurable and leave
the pinned source and generated build wiring to the shared integration owner.

## Action command boundary

Fighter command words are big-endian source words. Branch operands are accepted
only when the DAT relocation points at another checked instruction; numeric
operands remain values. The source `ftAction_803C0870` length table is the
schema for the action decoder:

```text
10..58 lengths:
5,5,1,1,1,1,1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
3,1,1,1,7,4,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,3,2,1,4
```

Falco's special scripts use opcode 12 (hitbox damage), 30 (rapid-jab state),
37 (fighter visibility), 38 (seven-word pseudo-random SFX), 41 (part
animation), 42 (`unk9`) and 49 (`unk16`). Opcode 55 is a three-word
effect/voice form. On the little-endian gameplay target its checked source
adapter stores the first word in the shared `sound_effect_0` representation:
the patched consumer reads the flag and graphics ID from that representation
and then passes the original word to the sound path. This is why opcode 55
must not be decoded as the raw little-endian `unk_fx_0` bitfield even though
the retail command word uses that source opcode. Each native allocation
retains the canonical source word for trace diagnostics.

Only checked command rows are installed in the native action table. A row that
has not passed the source graph and operand checks keeps the failing sentinel,
so an unavailable article/effect/audio service cannot masquerade as a playable
action.

## Review checklist

- Verify registry output with `--check` after changing the pinned source.
- Keep every extracted file local and record its source path and hash in the
  test log.
- Add one real-asset test for metadata, one for model/material ownership, and
  one action trace that selects a normal action and each new special family.
- Exercise construction, one source action tick, unbind, close, and a second
  construction. Check that input archive bytes are unchanged.
- Before browser admission, add a versioned fighter inventory to
  `web/action-sweep.mjs` with expected source motion IDs for every common action,
  defense option, normal, aerial, throw, recovery and special variant. Run it
  through the in-page source CSS/SSS drivers on every admitted stage, from both a
  cleared origin cache and a second application load. Preserve the emitted JSON
  reports. A hand-played or “representative” subset, a single action trace, or a
  successful scene entry is not browser performance coverage.
- Extend the same Release matrix with articles/effects, taking and shielding a
  hit, ledges, KO/respawn, pause, match exit and a second match when those paths
  are not yet encoded by the inventory runner.
- Require zero automatic timing pauses, zero active callback intervals over
  33.3 ms, zero browser `longtask` entries, zero audio underruns, and no live
  pipeline creation in that matrix. Add newly observed pipeline
  descriptors to the reviewed seed only after the source action is exercised
  in visible gameplay; never advance a hidden match to warm it.
- State the exact remaining gap when a source service is not hydrated; do not
  enable the character only because its row appears in the registry.
