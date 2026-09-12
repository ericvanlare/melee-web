# CPU opponents

Ordinary VS CPU opponents belong to the compiled game shared by `runtime.html`
and `prototype.html`. The original CSS owns slot type, character selection and
the level 1–9 slider. There is no browser AI, automatic key sequence, second
simulation loop or prototype-only CPU setting.

## Source boundary

`gameplay_player_selection.h` declares the admitted player types: human or
ordinary VS CPU (`cpu_kind == 4`, difficulty 1–9). Both the menu gates and the
patched original match-entry boundary use it. The two-slot, four-stock, supported
fighter/stage restrictions remain. Event and training AI modes are not admitted.
CPU rumble stays disabled by the original `gm_LoadRumbleEnabled` routine.
The prepared-match gate rejects CPU rumble; CSS selection does not inspect a
rumble setting that original VS entry has not resolved yet.

The original `fn_8016D8AC` copies `slot_type`, `cpu_kind` and `cpu_level` from
`StartMeleeData` into the original player records before fighter creation.
`Fighter_Create` registers the original fighter processes; `Fighter_8006ABA0`
runs the original CPU decision path and `Fighter_Spaghetti_8006AD10` consumes
its input. This is compiled source execution, not emulation or replacement AI.

The shared retail setup decoder also accepts those explicit CPU records, so
reference recipes execute through the same `GameplayMatchSession` as both
browser pages. A disconnected P2 device supplies no human input; the original
fighter processes generate CPU input.

The older standalone `gameplay_browser` harness and Slippi-driven
`GameplayReplaySession` still construct human players. They are separate test
entry points, not the runtime/prototype CSS path. CPU reference playback uses
the typed retail setup and `GameplayMatchSession` instead.

## Local CPU data and lifetime

The CPU decision graph is root 22 of the locally imported `PlCo.dat`, published
to the original `Fighter_804D64FC` global. It contains command scripts, per-fighter
attack-choice tables, distances and reach values. Loading only the source AI
functions is insufficient: their data must be decoded and relocated too.

`DatCommon` owns the decoded graph. The scoped common context retains it while
fighters can use it, requires its readiness before fighter initialization, and
restores the prior source global before releasing ownership. Command bytes keep
their original meaning; numerical words are decoded into the source layout.
Malformed pointers, table bounds and command streams reject during preparation.
No extracted table or command bytes are tracked or added to the hosted bundle.

The pinned graph contains 62 script slots (slot zero is null), seven arrays of
32 fighter attack-list pointers, 32 distance values and six reach values. Tests
read the owner's ignored local archive, exercise malformed graphs, and verify
copy/move ownership and retained use after `DatCommon` is destroyed.

This boundary survives the future host extraction: both pages will mount the
same Wasm runtime, whose common-data owner and fighter processes stay unchanged.
The temporary iframe adapter has no CPU-specific dependency to carry forward.

## Validation scope

CPU integration is development work. A successful menu transition or visible
movement does not establish original-game agreement. CPU decision data,
generated fighter input, RNG and fighter state must be included in validation.
The first browser smoke reached gameplay with an autonomous P2 but preceded
the complete CPU data audit; it is retained as UI evidence only.

The native menu contract checks levels 1–9, rejects out-of-range levels and
non-VS CPU types, and preserves a level-9 selection through CSS, SSS, four-stock
preparation and return to CSS. The real-HTTP browser check exercises ordinary
keyboard input in both pages. The original-game comparisons below validate two
bounded Mario/Final Destination workloads, not every admitted CPU selection.

This work does not establish audio, controller hardware, latency, performance,
or public release admission for the 4×4 slice. Existing evidence and holdout
requirements remain in [STATUS](../STATUS.md) and [the replay corpus](REPLAY_CORPUS.md).

## Reference inputs

CPU calibration plans use input-plan version 2. They name source player types,
ordinary CPU kind, difficulty and an immutable CPU PAD transport mode. The
current capture runner controls human P1 only; P2 is either disconnected or a
connected neutral device. It never sends human actions to drive the CPU.
Four-port samples are still verified against every original `PADRead` consumed
by the source scheduler. The existing human input-plan version 1 stays valid.

This changes neither the MWRC v2 recipe format nor the observed fighter-state
fields. Paired recipes retain the original setup, entry RNG and PAD history.
CPU-generated fighter input is an output of original simulation, not an input
copied from the reference into the port. Difficulty and player-type metadata
must agree with the independently observed original setup before comparison.

## CPU original-game comparisons — 2026-09-12

Mario human P1 versus Mario CPU P2 on Final Destination matches independent
original-game captures at both ends of the difficulty range. Each workload
supplies 480 neutral P1 ticks and leaves P2's connected PAD neutral; the CPU
generates its own fighter input. Both setups use four stocks, no timer,
ordinary VS CPU kind 4, and source-selected levels 1 or 9. Observed entry data
has human rumble enabled and CPU rumble disabled.

| Difficulty | Independent original executions | Compiled trace target | Drawn browser runtime |
| --- | --- | --- | --- |
| 1 | Exact agreement, 480 ticks | Exact declared state, 480 ticks | Exact declared state, 480 steps and 480 draws |
| 9 | Exact agreement, 480 ticks | Exact declared state, 480 ticks | Exact declared state, 480 steps and 480 draws |

The existing strict comparators check entry, consumed input, declared fighter
fields including CPU-generated input, RNG, match clock, PAD configuration and
all three PAD history banks. No comparison field, tolerance or acceptance gate
was relaxed. Each original draw audit observes all 480 camera traversals with
no change to those declared state fields. The browser reports pass their
`state_capture` validation with zero timing resumes. Drawing was executed;
pixels were not compared. Neither 480-tick workload completes a match.

Each original run starts a fresh pinned Dolphin Interpreter64 process for
GALE01r2, using the owned baseline SSS snapshot and save data. A hashed input
driver repeats the original CSS/SSS menu selection before arming the observer:
it changes P2 to CPU, selects the difficulty through the original slider and
starts the match. It writes no source memory, registers, RNG or fighter state.
The baseline must already contain the recorded unlock state; the driver rejects
otherwise. This is baseline plus repeated original menu preparation, not a
saved CPU checkpoint. Run metadata retains the preparation script, local
snapshot/save/config hashes, input plan and observer identity.

The reference uses Dolphin commit `c77bbaa0f372c3f72281602a8b087206706542cb`,
source revision `b43912cc78606f96c9569f5d6229bc9d7e265ea5`, and verified original
DOL SHA-1 `08e0bf20134dfcb260699671004527b2d6bb1a45`. Raw-v2 pipe configuration
has no stick calibration or deadzone; the game retains its original PAD
processing. Every consumed four-port vector and original CPU setup is checked
against the input-only plan. CPU decisions are never copied into port input.

Paired recipe SHA-256 values:

- Level 1: `fe5dfa245d47f0207991d6666b109ecc8a5877e1230d19d23903f32a188d7d99`.
- Level 9: `ebffb1b403cffda1d09c97b9202bb434d9d70c1f17875f7c7ba4c2cdd137e0fe`.

Level 1's diagnostic recipe ran before its second original capture finished;
after pairing, the official exported recipe was verified byte-identical to
the executed recipe. Its final comparison uses both independent captures.
Level 9 executed the paired export directly.

The ignored evidence folders are `work/cpu-reference/inline-level1/` and
`work/cpu-reference/inline-level9-v2/`. They retain source traces, plans,
independent run metadata, draw audits, exported recipes, native/browser traces
and comparison reports. `work/prototype-cpu-validation.json` binds those hashes,
decoded setups and the frozen build identity; its SHA-256 is
`384e6e91d54c2444a86f9cf1c614929925eacd98bd3bf911bc4615364ac88864`.

Failed checkpoint saves, raw-pipe preflight attempts and the initial level-9
slider preparation remain diagnostic records in `work/cpu-reference/`; they
are not passing captures. Levels 2–8 have menu contract coverage but no original
trace comparison here. Other fighters/stages, full and consecutive matches,
audio, physical controls and performance still require their own evidence.
No reserved holdout was used and no gold/public admission is claimed.

## Human regression control

The existing Mario/Mario Final Destination development pair (`retail-a4` and
`retail-b4`, Interpreter64) matches the CPU-data build through all 240 ticks:
entry, supplied input, fighter fields, RNG and match clock. This headless
control excludes drawing, audio output and performance; it is not CPU evidence
or complete-match admission. No reserved holdout was used.

Recipe SHA-256: `22c182c1e93d7fb76dbb4c6e0596db1c645bb085386f2de6965fb1cf37525346`.
Port trace SHA-256: `721dcdb3a39417d6d3339eb483252a8281a51c800f9e80d35fdfa0f6e3932f44`.
The local report and build/reference identities are retained under
`work/prototype-cpu-human-control/`.

## Browser and build checks — 2026-09-12

The final Release runtime and retail-trace targets build. Full unittest
discovery reports 550 tests, no failures and 46 skips for optional fixtures and
separate trace targets not present in this worktree. The new CPU data, input
plan and menu tests run.

The CI workflow now installs pinned dependencies before full unittest discovery;
the former order ran dependency-backed tests before their tools and patched
sources existed. Native build configuration is unchanged.

Headed Chrome 153 over real HTTP passes all 17 prototype checks. Ordinary
keyboard input selects B0XX mode, sends Start with P2 disconnected, selects
Final Destination and reaches an attacking CPU. The same route exits through
original No Contest, returns to CSS and releases the session through Eject.
A fresh direct `runtime.html` page independently enters CPU gameplay with
P2's keyboard disabled, then unloads. Both use the same frozen Wasm artifact.
The report records zero page/HTTP/console errors, uploads or timing resumes.

This is UI/lifecycle evidence, not complete-match or performance admission.
An earlier root22 browser run reached combat but then hit the existing timing
guard before No Contest. That red remains in
`work/prototype-cpu-root22-browser/`. The final harness explicitly reports any
visible utility resume after screenshot/automation stalls; the passing run
needed none. Pre-root22 movement-only runs remain diagnostic evidence too.

Final UI report: `work/prototype-cpu-final-browser/report.json`.
Final test log: `work/prototype-cpu-final-unittest.log`.
Preview Wasm SHA-256:
`314fcfbacac69e262446cfb5708e520f1577af5f4523ee77a89feb9103399f61`.
