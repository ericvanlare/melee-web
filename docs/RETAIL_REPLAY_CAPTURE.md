# Retail replay calibration

This is the correctness calibration path for replay validation. It runs an
owned GALE01 revision-2 disc in a separate pinned Dolphin process, observes
ordinary execution, and compares declared fields against the compiled source
port. Dolphin is a development reference; no emulator enters the browser port.

## Current result

Two independent automated captures of a four-stock Mario/Mario Final Destination
match repeat exactly for 240 source ticks, including Ready and the first 117
match-clock values. The input is neutral on two human ports; red/yellow costumes
and the complete 0x138-byte StartMeleeData come from ordinary retail menus.
The port then completes the same input recipe and teardown. Entry data, all
supplied inputs, both fighters' declared fields, RNG and match clock compare
exactly against that repeated reference. Schema/recipe v2 also restores and
compares the semantic PAD processing configuration and all four ports of the
Master, Copy and Game history banks, at initialization and on every tick.

This exposed two shared runtime gaps, subsequently fixed:

- Ground initialization registers stage particle data at bank 30 as well as
  the loader's bank 64. The missing bank 30 silently prevented authored stage
  animation events from creating generators. A read-only retail RNG call stack
  traced tick zero through Ground_801C1CD0, JObj animation, efLib_Cb_DPtcl,
  grLib_801C99C0 and the original generator routines. Retail consumed eight RNG
  values on that tick; the probe previously consumed none. Both bank slots now
  share the same decoded command and texture data. Every decoded stage animation
  event must resolve to a published bank/command before stage entry.
- The probe aborted on Mario's entrance landing with retail rumble enabled.
  LbRb.dat now has an owned decoder/publication, and the match supplies the
  original 12-list rumble pool and interpreter. The interpreter reads the opcode
  from the native 16-bit word instead of its first memory byte. Its command
  semantics are unchanged; physical actuator output remains Aurora's PAD
  responsibility. A focused test executes the original motor-command sequence,
  restart and malformed-program rejection. Hardware rumble is not validated.

The machine report deliberately says `declared_state_match`,
`gold_admitted: false` and `performance: not_evaluated`. This is a calibration
fixture, not a Slippi-derived gold replay, full-match acceptance or visual/audio
agreement. The headless probe excludes source drawing and records state after
the source tick, before PCM transport. A separate retail audit now observes
entry/return of the original camera traversal on all 240 ticks: none changes
the declared fighter fields, PAD configuration/history, RNG or clocks during
drawing. This is a neutral-fixture result, not a claim about all render state or
active gameplay. A visible source draw/replay comparison remains necessary;
matching this prefix cannot waive it.

The v2 initial PAD contract is 822 big-endian semantic bytes: 30 configuration
bytes, then Master/Copy/Game banks with four 66-byte statuses each. C padding,
queue storage and rumble pointers are excluded. Native code decodes each scalar
and float bit pattern, validates the configuration, and applies it once to an
owned unticked match before fighter/world initialization. Each history keeps
its own buttons, trigger/release/repeat state, repeat counter, raw and normalized
axes, cross-direction and error status. This preserves held-button history
instead of synthesizing a fresh press. The live queue and rumble allocations
remain native-owned. Existing v1 evidence remains readable with its narrower
scope; it cannot silently acquire v2 coverage. This explicit recipe constructor
does not change ordinary browser CSS/SSS controller-history ownership.

## Reproduce with local owned inputs

Use the dependency versions and configuration recorded in the local provenance
manifest. The present reference is Dolphin 2606a, commit
`c77bbaa0f372c3f72281602a8b087206706542cb`, Interpreter64, single CPU thread,
cheats off and fixed RTC 1704067200. The expected DOL SHA-1 is
`08e0bf20134dfcb260699671004527b2d6bb1a45`. The manifest must also pin the exact
Dolphin binary SHA-256, SSS snapshot SHA-256 and every external GC save-file hash.
The template controller configuration uses `Pipe/0/pad1` and `Pipe/0/pad2`.
The snapshot must be an ordinary SSS checkpoint paired with its external GC
state, after any memory-card prompts have been resolved. A savestate alone does
not identify the whole input state.

```sh
python3 scripts/capture_retail_replay.py \
  --dolphin /path/to/pinned/Dolphin.app \
  --disc /path/to/owned/game.ciso --dol /path/to/owned/main.dol \
  --template-user /path/to/template-user \
  --snapshot /path/to/sss.sav --checkpoint-gc /path/to/checkpoint-gc \
  --provenance /path/to/provenance.json \
  --output work/reference/run-a.jsonl --frames 240 --timeout 180
# Repeat the same command with a fresh output run-b.jsonl.
python3 scripts/compare_retail_replays.py \
  work/reference/run-a.jsonl work/reference/run-b.jsonl \
  --output work/reference/repeatability.json
python3 scripts/export_retail_replay.py \
  work/reference/run-a.jsonl work/reference/run-b.jsonl \
  --output work/reference/input.mwrc
cmake --build build/browser --target gameplay_retail_trace gameplay_rumble_trace
/path/to/project-node build/browser/gameplay_retail_trace.js \
  /path/to/owned/menu-files /path/to/owned/game-files \
  work/reference/input.mwrc > work/reference/port.jsonl
python3 scripts/compare_port_replay.py \
  work/reference/run-a.jsonl work/reference/run-b.jsonl \
  work/reference/port.jsonl --output work/reference/port-comparison.json
```

The runner copies the user directory, paired external GC bytes, snapshot and
collector sources for each invocation. It creates unique controller pipes and a
short Unix socket, explicitly enables background input, bounds the capture,
validates completion and terminates only its own GDB/Dolphin process groups on
success or failure. Its hidden `.retail-replay-run-*` directory preserves
configuration, commands, hashes and logs. Captures and extracted assets remain
local and ignored. The runtime now also requires the owned `LbRb.dat` file.
Before launching, the runner also reads the executable from the supplied disc
and requires its hash to match the independently pinned DOL. Supplying a correct
standalone DOL beside a different game image is insufficient. The local CISO's
executable passes this check. Snapshot creation remains a trusted, documented
retail procedure; hashing a snapshot alone cannot prove how it was produced.

The procedure currently handles this specific SSS checkpoint: advance 12 neutral
source ticks, arm observers, press A for eight source ticks, release for eight,
then capture the requested match ticks. It is not a general menu driver or a
Slippi input injector. Do not feed another checkpoint and assume equivalent
initial conditions merely because the command exits successfully.

Add `--draw-audit` to observe `HSD_GObj_80390FC0` entry and its verified return.
The owned evidence directory receives `draw-audit.jsonl`; run metadata binds
its hash to the capture and records the comparison. Missing, reordered or extra
draws, invalid state, or a draw entry that does not match the immediately
preceding scheduler state fail validation. The expected source scene-counter
increment between scheduler and draw is checked explicitly. Changes during
drawing remain visible as `declared_state_changed`, never waived. This audit
requires one draw per source tick; it does not cover retail's queued multi-tick
cadence, pixels, GPU results or timing.

## Capture contract and failure rules

- Entry is `gm_Scene_Vs_OnEnter` entry. The full StartMeleeData and RNG are read;
  v2 also records semantic source PAD configuration and master/copy/game histories.
- Initial state is the verified return instruction of VS entry. First input
  consumption and frame observations are forbidden before that boundary.
- Inputs are read at `HSD_PadRenewMasterStatus` entry from the queue slot actually
  consumed by the game. Each tick must consume exactly one four-port vector;
  each PADStatus contains 11 semantic bytes, with C padding excluded.
- State is read at `HSD_GObj_80390CFC` return: both fighters' motion/animation,
  position, velocities, facing, grounded state, damage, shield, stocks and input
  bytes, plus RNG and clocks. Floats retain exact IEEE bits.
- V2 compares the complete semantic PAD snapshot at entry, initial state and
  each scheduler observation. A PAD divergence names its bank, port and member.
- Scene ticks must be contiguous from zero. Match time can remain zero during
  Ready; using it as the input-step clock would collapse real simulation ticks.
- Repeated debugger traps were observed at identical boundaries. A repeat is
  discarded only when its boundary identity, complete observed state/input and
  GPR/PC/LR/CTR/CR context agree. A changed observation at that boundary, new
  consumption at a repeated scheduler stop, recurring older boundary, missing
  tick or incomplete end record fails. The setup driver counts actual source
  counter advances and has a total trap budget, rather than treating each GDB
  stop as a frame.
- Distinct UUIDs are necessary pairing checks, not proof of independence.
  Preserve separate launch metadata and process lifetimes. The comparator checks
  the entire capture before reporting agreement; a matching prefix is insufficient.

GDB observes retail without writing game state, inputs, RNG, registers or
executable bytes. Controller input uses the ordinary Dolphin pipe backend.
Debugger and interpreter timings are not performance evidence. This collector
is intentionally a small calibration oracle: before collecting a large corpus,
validate a faster reference exporter against it and then reuse pinned immutable
reference traces. Do not schedule thousands of interpreted GDB captures for each
content change.

## Local evidence for this checkpoint

The ignored `work/reference-replay-proof/` directory contains:

- `retail-auto-a.jsonl` SHA-256
  `6ecc596cad7c5c81f4f99afe8875e13b9179711e822b0b9e50a756434c8d4f81`;
- `retail-auto-b.jsonl` SHA-256
  `b17eb1e2b636630d183ed5f283c41c79a0012b02769c82ecb1ccc34711c6ffbb`;
- `repeatability-auto.json`, the passing independent reference comparison;
- `retail-auto-c.jsonl` SHA-256
  `48b84c4d56f1d141b236456612e7f8354f09e1087ffbda7def258e08cfaed31b`,
  a further independent run after runner cleanup and provenance checks;
- `repeatability-final-runner.json`, the passing B/C reference comparison;
- `port-final.jsonl` SHA-256
  `721dcdb3a39417d6d3339eb483252a8281a51c800f9e80d35fdfa0f6e3932f44`;
- `final-port-comparison.json`, the passing declared-field comparison against B/C;
- `port-before-alias-comparison.json`, a retained red at tick zero's RNG;
- separate diagnostic RNG call-stack captures and failed earlier attempts. These
  are diagnosis records, not additional admitted reference fixtures.

Integration verification also retained the 686-frame input-only workload result,
Release build and warm Marth/Dream Land action-sweep reports. The full automated
suite exercised 350 tests: 344 passed, while six recipe tests rejected an outdated
synthetic fixture that began at scene tick 100. After correcting that fixture to
tick zero, all 41 replay tests passed in a focused rerun. The actual retail
captures already began at zero and the final port comparison still passes.
Logs are `verified-unittest.log`, `verified-replay-tests.log`,
`final-slippi-workload.jsonl`, `browser-sweep-teeter-failure.json` and
`browser-sweep-pass.json`. The browser records are compact transcriptions of the
visible DOM reports, not reference-state traces; the passing record pins the
tested Release artifact hashes and records unload/cleanup.

The follow-up ignored `work/reference-input-v2/` evidence contains independent
`retail-a.jsonl` / `retail-b.jsonl`, repeated-reference export `retail-input.mwrc`,
`port-final.jsonl` and `comparison-final.json`. All 240 ticks match, including
the complete PAD snapshot. The respective capture SHA-256 values are
`ce5561fc4e52b887006a2d90ddca764586b749c05ea5c2328ada97a80a1d69d3`,
`616a5849655cc732446b0b50cb35e2663f4633fc64c3659b1157e2b5e9c509c5`
and `6e6ebea3ae091c25bb9892c09b45b564012501922583e946bd98cf0627576b75`.
`retail-draw.jsonl` is a further independent capture with drawing observation;
`draw-audit-comparison.json` records 240 traversals and zero measured changes.
Native boundary tests cover exact signed/float bytes, distinct held/released
edges, invalid snapshots and retention of queue/rumble ownership. Schema
negative controls reject omitted histories and broken draw lifecycles.
The follow-up full suite passes all 354 tests, and both Debug and Release
`gameplay_menu_browser` builds pass. These builds are integration checks, not
new visible-browser replay performance evidence.
