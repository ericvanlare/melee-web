# Retail replay calibration

This is the correctness calibration path for replay validation. It runs an
owned GALE01 revision-2 disc in a separate pinned Dolphin process, observes
ordinary execution, and compares declared fields against the compiled source
port. Dolphin is a development reference; no emulator enters the browser port.

## Current result

The 240-tick Mario/Mario Final Destination pair remains the small Interpreter64
calibration trajectory. It repeats exactly through Ready and the first 117
match-clock values, including the complete semantic PAD configuration/history
contract. That result is scoped calibration evidence.

A processed-v2 Fox/Falco Battlefield donor now supplies a 3,122-tick complete
vanilla match. Two independent JITARM64 captures repeat exactly through the
original elimination exit and final draw. Release/headless and visibly drawn
port traces match all declared fields and finish teardown. Final visible Release
cold/warm timing gates pass with zero failures, and the final collector passes
the full Interpreter64 cross-check against the repeated references. See the
[complete-game evidence ledger](COMPLETE_REPLAY_CALIBRATION.md) for hashes,
shared fixes, retained reds and observed coverage.

The [corpus expansion](REPLAY_CORPUS.md) adds source-driven match-length
discovery and coverage selection with source-hash separation of held-out inputs.
It also records why source-drawn comparison is mandatory: magnifier rendering
sets an offscreen flag that can change damage and RNG on a later source tick.
A headless trace or a draw audit of the currently declared fields cannot prove
independence from that hidden state.

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

Machine reports deliberately retain `gold_admitted: false` and
`performance: not_evaluated`. A capture is a calibration candidate until the
independent pair, actual-input checks, complete callback/end/draw evidence and
port comparison all pass. The headless probe records state after the source
tick and before host audio transport; it is not visual or audio agreement.

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

`Interpreter64` is the strict default and the backend used for the immutable
reference pair. `JITARM64` is an explicit ARM64 opt-in; it is not silently
substituted for the interpreter. Calibrate a changed collector against the
immutable Interpreter64 pair on the same scoped trajectory before using it:

```sh
python3 scripts/calibrate_retail_collector.py \
  --reference-a work/reference/complete-a.jsonl \
  --reference-b work/reference/complete-b.jsonl \
  --candidate work/reference/complete-jit.jsonl \
  --candidate-cpu JITARM64 \
  --output work/reference/jit-calibration.json
```

The calibration report permits the candidate collector identity and explicitly
selected CPU backend to differ, but compares the declared entry, PAD input,
state and end trajectory. Its scope is that trajectory only: it is not broad
equivalence, performance acceptance or gold admission.

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
GDB runs in hidden batch mode while retaining hardware observers. Dolphin is
launched with `Dolphin.DSP.Backend=No Audio Output`; the old `Null` value is
invalid and falls back to Cubeb. `No Audio Output` removes the host sink while
retaining DSP and source audio execution, so the capture does not become a
different source workload.
Before launching, the runner also reads the executable from the supplied disc
and requires its hash to match the independently pinned DOL. Supplying a correct
standalone DOL beside a different game image is insufficient. The local CISO's
executable passes this check. Snapshot creation remains a trusted, documented
retail procedure; hashing a snapshot alone cannot prove how it was produced.

The procedure handles the documented SSS checkpoints: advance 12 neutral
source ticks, arm observers, press A for eight source ticks, release for eight,
then capture the requested match ticks. It is not a general menu driver.
Do not feed another checkpoint and assume equivalent
initial conditions merely because the command exits successfully.

## Input donors through vanilla retail

The initial profile requires a timeline beginning at Slippi frame -123, with
two human players on ports 1/2 and neutral disconnected samples on ports 3/4.
All four consumed PAD vectors are checked at every tick. Seeking and arbitrary
mid-recording slices require a separate explicit initialization contract.

`export_retail_input_plan.py` derives a bounded, input-only plan from a completed,
finalized `.slp` file. It requires both human ports 1/2 and no follower or
doubles inputs. The strict plan schema rejects state-bearing or unknown fields,
duplicate keys and incomplete vectors. It retains the full source hash, frame
origin, character IDs and stage ID; the retail menu checkpoint must select those
same characters and stage. Costumes, rules, starting RNG and all initial PAD
histories come from the independently observed retail initialization, not the
recording's online/UCF post-state.

```sh
python3 scripts/export_retail_input_plan.py /path/to/donor.slp \
  --output work/reference/donor-plan.json \
  --policy dolphin-pipe-raw-v2
# An initial complete-match prefix keeps frame -123 and parses/hashes the full
# .slp before slicing its plan:
python3 scripts/export_retail_input_plan.py /path/to/donor.slp \
  --output work/reference/complete-plan.json --policy dolphin-pipe-processed-v2 \
  --frames 3122
# The capture count must equal the entire supplied plan:
#   --input-plan work/reference/complete-plan.json --frames 3122
# Add --draw-audit and, for a completion claim, --require-match-complete.
```

The current `dolphin-pipe-raw-v2` policy is the default. It clears stick
calibration, center and modifier bindings, and requires zero dead zone and
virtual notches. `dolphin-pipe-processed-v2` is an explicit legacy conversion:
finite processed axes in `[-1,1]` become raw bytes with scale 80 and nearest
rounding, with half ties away from zero. Those bytes are a derived workload,
never recovered hardware or UCF expected state. The serial-interface mode-3
PAD boundary keeps A/B pressure at zero; digital L/R still forces analog 255.
The historical `dolphin-pipe-raw-v1` policy is readable only for neutral A/B
samples and cannot be used to imply the v2 pressure contract.

`--frames N` accepts only a positive initial prefix beginning at frame `-123`
and no larger than the complete parsed timeline. The exporter parses the whole
`.slp` and hashes all source bytes before selecting the prefix, so the plan's
provenance remains the donor's full SHA-256. The runner pins the plan and all
collector helpers, then requires **every actual consumed four-port PAD vector**
to agree with the intended input. A configuration check alone is insufficient.

The collector publishes the first input at VS entry and bootstraps one input
of lookahead at the first HSD queue consumption. Later input publication observes
all four statuses inside PADRead, before OSRestoreInterrupts can allow the next
VI sample. The consumed slot is observed at `80377584`, after HSD has decremented the queue
count and while interrupts are still disabled; `r25` retains the consumed slot
and `r6` its original read index. The collector verifies that pointer against
the queue allocation. Its header names `HSD_PadRenewMasterStatus_dequeued_slot`;
legacy entry-queue captures remain readable but cannot be mixed into a repeat
pair. The actual four-port vector is independently checked at every scheduler
return. Duplicate debugger observations must have identical
boundary identity and complete payload; ambiguous observations invalidate the
capture. It refuses an extra source tick before the final requested draw.
Draw audits retain sparse ordered source indices, and complete-match evidence
requires the final draw plus the original elimination exit observation.

The first donor is the official Slippi JS 9.1.3 `wavedash-1.slp` fixture, SHA-256
`173e24ef8c7d600fbe12b09a2e4e3217669f0b19c306912ff80c3bf224ddd2e5`:
Fox/Falco on Battlefield, 686 source frames from -123 through 562. Its UCF/online
state is excluded. Two independent vanilla executions repeat exactly for all
686 ticks, and both 686-draw audits find no changes to the declared state.
The first port comparison exposed a real one-bit sine difference at tick 155,
then an arctangent difference at tick 252 after the sine boundary was corrected.
Neither red is waived with a tolerance. This short movement workload does not
cover combat, a complete stock match, all numerical functions or browser timing.

For the separate movement canary only, the final port run matches all 686 ticks after compiling the original MSL
`trigf.c` and enabling the recovered `lbtrigf.c` arctangent. The downstream patch
preserves the retail polynomial tables and each `fmadds`/`fnmsubs` rounding
boundary with explicit `fmaf` calls. It also preserves the original initialized
range-reduction constants, sign bits and integer conversion/shift behavior
without undefined C casts, aliasing or out-of-bounds pointer formation.
The range-conversion helper retains positive-overflow saturation to `INT32_MAX`
and negative-overflow saturation to `INT32_MIN`, matching [IBM’s fctiwz
instruction definition](https://www.ibm.com/docs/en/aix/7.1.0?topic=is-fctiwz-fcirz-floating-convert-integer-word-round-zero-instruction).
Compiler builtins are disabled for these gameplay functions; general fast-math
and implicit contraction remain disabled. This is a shared math-boundary fix,
not an air-dodge special case or a position correction.

Read-only call-site probes independently captured these function results:

| Source tick | Stick X/Y bits | Angle bits | Sine bits | Cosine bits |
| --- | --- | --- | --- | --- |
| 155 | `bf466666` / `bf200000` | `c01da0a6` | `bf20b463` | `bf474614` |
| 252 | `3f500000` / `bf100000` | `bf1b04f7` | `bf11b7bd` | `3f527b31` |

The automated Wasm test executes the actual patched source functions against
these observations, with host libm as a required failing negative control.
All six air-dodge entries in the new vanilla trajectory (155, 252, 318, 396,
465, 598) are included in the passing full-state comparison. This remains
declared-state evidence; unmeasured math paths need their own comparisons.

### Preparing the Fox/Falco checkpoint

The local `work/reference-input-v2/fox-falco-checkpoint/` manifest records the
setup recipe separately from the read-only capture. An isolated copy of the
persistent availability state enabled Falco (character-unlock bit 5) and
Battlefield (stage-unlock bit 6), using verified revision-2 tables. That setup
step writes availability data; it does not write executable code, RNG, fighters
or active-match state. Ordinary source cursor input then selects Fox/Falco,
waits for the CSS ready condition before Start, resolves the original memory-card
overwrite/continue-without-saving prompts, and hovers Battlefield in SSS.

Save from Dolphin's main-window Emulation → Save State menu into an unused
slot, then copy that snapshot, external GC files and controller configuration
together. A hotkey sent only to the render window did not save this checkpoint.
Pin all copied hashes and the setup description before capturing. The local
SSS snapshot SHA-256 is
`55dfef2413480ed37531f584296ffc0850551b9d64612278bcf6792a2ee0c30c`.
Every subsequent collector process is read-only and uses ordinary controller
pipes. Do not conflate this documented setup with capture purity, or reuse an
unrelated save directory just because the savestate loads.

Add `--draw-audit` to observe `HSD_GObj_80390FC0` entry and its verified return.
The owned evidence directory receives `draw-audit.jsonl`; each row records the
source index and run metadata binds its hash to the capture. Missing, reordered
or conflicting observations, invalid state, or a draw entry that does not match
the immediately preceding scheduler state fail validation. Sparse draw indices
remain explicit evidence rather than being filled in as one-draw-per-tick.
For `--require-match-complete`, the final draw must cover the final source tick;
pixels, GPU results and timing remain outside this audit.

`--require-match-complete` is an explicit stronger bound. It requires
`--draw-audit`, the collector's exit callback observation, the JSONL `end`
record, and `match-completion.json`. That sidecar records the capture hash,
frame count, original exit request, match-end bytes and final draw source index;
the runner validates it against the capture and requires the final draw after
the final scheduler tick. A normal bounded prefix capture may stop at its
requested tick without making any full-match or teardown claim.

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
Release build and warm Marth/Dream Land action-sweep reports. Those are separate
movement/integration evidence; they do not accept the 3,122-tick combat
trajectory. The full automated
suite exercised 350 tests: 344 passed, while six recipe tests rejected an outdated
synthetic fixture that began at scene tick 100. After correcting that fixture to
tick zero, all 41 replay tests passed in a focused rerun. The actual retail
captures already began at zero. Those are historical calibration logs; the
newer complete-game ledger records resolved combat numerical/teardown reds and
the final full-suite result.
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

The input-donor follow-up in `work/reference-input-v2/` contains:

- `donor-b.jsonl`, SHA-256
  `27af52b3e12385b0d67a3b5741ca0bda06b33952fe4fcf4c1d825829c9e80216`;
- independent `donor-c.jsonl`, SHA-256
  `6f33592b670151e1e40627001d3cf2f05cb1c563c5b550aa482a2e81480aeee4`;
- `donor-paired.mwrc` and its sidecar, exported only after complete repeatability;
- `donor-first-red.json` (tick 155) and `donor-msl-comparison.json` (tick 252),
  retained without filtering or tolerance changes;
- `donor-port-trig.jsonl`, SHA-256
  `ce999008f9544fd4225c0b9fab86be64f36d2d3641145cfd3fbc884cf5f0fb0d`,
  and `donor-trig-comparison.json`, the passing 686-tick declared-state comparison;
- owned run metadata binding both 686-draw audit files to their captures;
- `trig-diagnostic-prefix.jsonl` / `trig-diagnostic-prefix-253.jsonl`, separate
  read-only call-site diagnostics whose owned evidence contains the scalar
  observations above; and
- `retail-trig-disassembly.txt`, instruction inspection of the owned pinned DOL.

The original 240-tick neutral calibration also remains exact after the sine
boundary change. No visible browser replay or performance gate is implied by
these native traces, and the number of admitted gold Slippi fixtures remains zero.

## Visible browser replay

The ordinary Release player now accepts the paired **MWRC v2** input recipe in
Diagnostics → Reference replay. Native and browser playback share the bounded
recipe decoder, typed original setup, PAD initialization and diagnostic observer.
The browser constructs a fresh source match through its existing deferred owner,
waits for ordinary audio acknowledgement/resource preparation, and feeds one
recipe sample to each source tick in the normal render/audio loop. It requires
a successful source draw in every callback that consumes replay input, plus the
final input, final draw and successful teardown before completion. Normal
scheduling can group ticks in a callback; this is not a one-draw-per-tick claim.
Startup is locked before asynchronous work, and failed teardown rejects entry.
Diagnostic raw input cannot override the timeline. Source pause/early exit beyond
this bounded profile fails explicitly; seeking and replaying the original Slippi
post-state are not implemented.

Two modes deliberately collect different evidence:

- **State capture (instrumented)** emits the same initial/per-tick semantic trace
  as the native probe, before audio transport and source drawing. It can resume
  an instrumentation-induced timing pause and records those resumes. This mode
  never provides performance evidence.
- **Performance** does not serialize state. It uses ordinary production timing,
  resource and audio counters; a timing pause ends the run as a failure. No
  automatic/manual resume can turn that run into a passing timing report. The
  real-browser manual-pause negative control stopped at input 301 with an
  explicit failure and incomplete timeline; the automated handler test also
  rejects overlapping startup and failed teardown.

The completed state run offers `retail-port.jsonl` and both modes offer
`retail-browser-report.json`. Reports include the recipe SHA-256, consumed count,
preparation, callback tails, memory/resource activity, audio counters, visibility,
cache state and browser configuration. The disabled audio acknowledgement carries
final counters so an underrun in the last reporting interval is not lost.
Downloads and serialization happen after teardown.

When browser download support is unavailable, start the existing loopback server
with an explicit evidence directory outside the served build:

```sh
python3 scripts/serve.py --directory build/browser-release --port 8791 \
  --evidence-directory work/replay-evidence
```

**Save replay to local evidence server** saves only the generated trace/report.
The opt-in endpoint requires its loopback origin and known diagnostic names,
limits upload sizes, and saves files under SHA-256 names without overwriting
existing evidence. It returns the local paths and hashes in the page. Ordinary
server operation remains read-only. The disc/file loader does not use this
endpoint; game archives stay in the tab.

For pipeline discovery, complete/unload the visible replay, then use **Export
render cache** followed by **Save cache to local evidence server** after pending
pipelines drain. This saves only `pipeline_cache.db` and its nonempty SQLite WAL,
not the driver-specific Dawn cache. Copy both exports to a temporary directory
as `pipeline_cache.db` and `pipeline_cache.db-wal`, respectively, before opening
SQLite and checkpointing. The main database alone may contain only its header;
a WAL cannot be omitted. Do not modify the immutable captured files in place.
Review the schema, descriptor version/size and added rows against the previous
seed; preserve all prior descriptor payloads. Materialize only those portable
rows into the reviewed seed, update its digest/inventory test, rebuild Release,
and repeat the cold/warm runs. Discovery is never a timing pass.

For the timing pair, use **Clear render cache + reload**, reload the local disc,
choose the same recipe and run **Performance** with the tab visible and no build,
full-suite, emulator or other agent CPU work in flight. Save that report. Then
use **Reload application state**, reload the disc/recipe, run Performance again
and save its report. This discards the entire runtime/asset/audio heap between
runs. It does not clear browser or GPU-driver caches. Capture the Release
artifact hashes, machine, browser, power and display profile before running and
inspect the browser error log. Source-frame instrumentation stays off throughout
both live timing runs.

Join the raw evidence using:

```sh
python3 scripts/check_browser_replay.py \
  --reference-a work/retail-a.jsonl --reference-b work/retail-b.jsonl \
  --recipe work/paired.mwrc --port work/retail-port.jsonl \
  --state work/state-browser-report.json \
  --cold work/cold-browser-report.json --warm work/warm-browser-report.json \
  --profile work/browser-profile.json --browser-errors work/browser-errors.json \
  --build-directory build/browser-release --output work/scoped-replay-evidence.json
```

The profile pins `build: "Release"`, machine `hardware`, `power` and `display`,
and an `artifacts` mapping of SHA-256 hashes for `gameplay_menu_browser.js`,
`gameplay_menu_browser.wasm`, `gameplay_menu_browser.data`, `runtime.html`,
`runtime-cache.js` and `audio-worklet.js`. Preserve OS, input, power settings,
driver-cache scope and instrumentation notes alongside those required fields.
The inspected browser-errors file is a JSON list. The checker revalidates the
paired references and actual rendered port trace, compares the exact recipe,
checks report hashes and every hard counter, rejects wrong cache profiles, and
verifies the named build artifacts have not changed. The profile and console
inspection remain operator attestations; the checker cannot prove which browser
binary rendered a self-reported artifact. It emits scoped evidence, never
fighter/stage admission or an unqualified gold-corpus claim.

### First donor browser evidence (movement canary)

The ignored `work/reference-input-v2/` receipt
`scoped-replay-evidence.json` passes the implemented movement-canary gates for
the complete 686-input donor, with all six air dodges retained. The rendered
trace SHA-256 is
`fe695b74c56e2945fd17082eae306abe39c5e8fb2e2e9552865d2f8e3d2da65c`.
It is identical across repeated browser state runs, including a discovery run
whose instrumented scheduler batched some ticks. Every declared entry/fighter,
RNG, match-clock, raw input and full PAD-history field matches both retail
captures. This does not establish pixel, audio-output or retail VI-cadence
agreement.

The final-build reports under `browser-evidence/` are SHA-256 named:

- state: `2e7609e253c00224696f1cb5b43ee025b8481d186be654512215ca0d0236d791`;
- cleared origin: `74e6bdc1c7c3dfdc9f35085fbfecef7981d3e017458b7dfc691a2ad1339d26a9`;
- full warm reload: `88c56280fdb7f49959bd249c5b501bad1faadacf05b08d40a2eaaa1b6a18e83f`.

The reviewed seed gained 45 portable GX descriptors, preserving all 344 earlier
payloads. Its 389 pipelines use config version 65549 with 2,772 bytes per
pipeline, plus the existing 64-byte clear record. Seed SHA-256 is
`49bf551d057cd9d597e9c145132be763fb99b92289876934a5f21b8bd7c0f047`.
The Release Wasm SHA-256 is
`d5e9639aa22ad0af62450690aa80ca7692c1e252953e54a315d76751520f2f67`.

Both timing runs complete 686 source ticks with zero timing gaps, long tasks,
audio underruns/overflows, live pipeline creation, visibility loss or resume.
Worst native callbacks are 12.735/7.42 ms, intervals 26.11/19.615 ms, and match
preparation 186.745/200.905 ms. Both upload 3,811,328 texture bytes during play
and have no live heap growth. An earlier warm run grew the Wasm heap by
86,441,984 bytes without a timing failure; that report remains retained.
These are explicit resource measurements, not a zero-allocation guarantee. The named machine is Apple M4,
32 GiB, macOS 26.6.2 (25G83), AC power with low-power mode off, built-in
2560×1664 display, visible in-app Chromium 152.0.0.0, 640×480 framebuffer,
DPR 2. Driver caches are uncontrolled. The next corpus work is a faster validated
reference exporter and coverage-selected combat/stock-loss donors; this movement
canary does not substitute for those trajectories.

Final verification: all 372 automated tests pass, including actual JavaScript
startup/pause handlers, malformed evidence and raw-input rejection controls;
Debug native/browser and Release browser builds pass. The neutral 240-tick and
donor 686-tick native comparisons remain exact after integration.
