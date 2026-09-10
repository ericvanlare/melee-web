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
exactly against that repeated reference.

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
agreement. The recipe currently excludes initial global PAD histories; the
collector records master/game histories and configuration but still needs copy
history and a typed restoration contract before general input admission. The
probe excludes source drawing and records state after the source tick, before
PCM transport. Those phase and lifecycle limits must be resolved or explicitly
proven irrelevant for a future admitted fixture; matching this neutral prefix
cannot waive them.

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

## Capture contract and failure rules

- Entry is `gm_Scene_Vs_OnEnter` entry. The full StartMeleeData and RNG are read;
  the capture also records source PAD configuration and master/game histories.
- Initial state is the verified return instruction of VS entry. First input
  consumption and frame observations are forbidden before that boundary.
- Inputs are read at `HSD_PadRenewMasterStatus` entry from the queue slot actually
  consumed by the game. Each tick must consume exactly one four-port vector;
  each PADStatus contains 11 semantic bytes, with C padding excluded.
- State is read at `HSD_GObj_80390CFC` return: both fighters' motion/animation,
  position, velocities, facing, grounded state, damage, shield, stocks and input
  bytes, plus RNG and clocks. Floats retain exact IEEE bits.
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
