# Slippi replay validation architecture

Slippi replays are the primary scalable gameplay workload for this port. The intended
pipeline uses canonical input workloads for independently captured vanilla
state comparison and visible Release-browser performance. These are separate
gates, and workload completion alone passes neither. Hand-authored action traces remain short
smoke tests and first-divergence reproducers; they are not the admission proof
for an entire fighter or stage.

This does not make a `.slp` file a complete retail oracle. Replays begin at
match setup and do not prove CSS/SSS transitions, rendered pixels, emitted PCM,
physical-controller latency or browser scheduling. Those keep their existing
independent gates.

## Evidence provided by a replay

The pinned Slippi format supplies:

- the original Game Info Block, players, stage, rules, costumes, game-start RNG
  seed, PAL flag, controller-fix settings and other versioned setup fields;
- per-player pre-frame controller values and RNG observations;
- end-of-frame player state suitable for first-divergence comparisons;
- item/projectile observations and, in newer versions, Dream Land Whispy,
  Fountain of Dreams platform and Pokémon Stadium transformation events; and
- frame bookends identifying the finalized timeline in rollback recordings.

The runner must preserve field availability by Slippi version. Missing fields
are explicit unsupported evidence, never inferred values.

## Architecture

### 1. Parse outside the source clock

Use a pinned release of the official `@slippi/slippi-js` parser as the format
authority and conformance oracle. Parsing, validation and corpus indexing occur
before match construction and outside active gameplay timing. The browser build
may use the library's browser entry point in a worker.

Convert each accepted `.slp` file to one versioned, engine-neutral replay
timeline containing:

- immutable match setup and provenance;
- one finalized input record per source frame and controller port;
- original observed state fields with presence masks and original float bits;
- RNG observations, item/stage observations and match-end data; and
- a content hash for cache and evidence identity.

The original gameplay runtime consumes only its validated input/setup product
through the same `PADStatus` boundary as a physical controller. Original
recording observations remain outside the executable input transport. Slippi parsing must not become a
dependency of fighter, stage or HSD source code.

### 2. Separate workloads from trusted reference fixtures

A gold fixture is a versioned contract containing canonical input bytes, complete
initial conditions, the observation phase, independent vanilla expected state,
and provenance. A `.slp` file alone is not that contract. Its observed state
belongs to its original executable, modifications, setup and input boundary.

Use two explicit routes:

- **Derived workload:** extract the available inputs, declare every conversion
  and setup change, then execute ordinary source match construction. Completion
  provides workload evidence only. The v2 `MWRP` transport carries source
  identity and the original Game Info Block for provenance, but deliberately
  contains no expected state and cannot enable an accuracy comparison.
- **Reference fixture candidate:** run a declared input sequence through pinned,
  cheats-disabled vanilla retail. Capture initialization, actual consumed PAD
  samples and state at a named source phase. First require two independent
  executions to agree. Only then compare the port at the corresponding phase.

Prefer modern raw-rich recordings. The current `dolphin-pipe-raw-v2` plan is
the default. Older recordings with missing raw axes may use the explicit
`dolphin-pipe-processed-v2` fallback: finite processed axes in `[-1,1]` are
converted with the pinned scale 80 and nearest half-away-from-zero rounding.
These are derived workload bytes, never recovered hardware input or UCF
expected state. Physical buttons and invertible trigger floats remain required;
mode-3 serial PAD A/B pressure is zero, while digital L/R maps to analog 255.
The historical raw-v1 plan is readable only for neutral A/B samples. Never
inject processed floats directly at the raw controller boundary.

An exporter prefix is initial-only: `--frames N` requires a positive `N`, keeps
source frame `-123`, and rejects values beyond the complete parsed timeline.
The exporter parses and hashes the complete `.slp` before slicing, so a prefix
plan retains the full source-byte provenance. The retail runner also checks the
actual consumed four-port PAD vector on every scheduler tick.

Construct the match once from the declared fixture initialization, then supply
one input vector per source tick. Never write recorded positions, action states,
damage, stocks or per-frame RNG into the running game. RNG after initialization
is an observation/assertion, not a repair mechanism. Compare exact bits first;
any numerical tolerance needs a documented field-specific rationale.

### 3. Establish reference trust before interpreting a red

Pin the retail executable, Dolphin build, source revision, CPU mode, settings,
save/setup recipe and collector. `Interpreter64` is the strict default;
`JITARM64` is an explicit ARM64 opt-in. Dolphin remains an external validation
tool; no emulator enters the browser runtime. Preserve ordinary retail setup
and capture the complete `StartMeleeData` at VS entry. Decode native source
fields explicitly; never transplant PPC pointers or C bitfield layouts.

The collector runs GDB in hidden batch mode and uses Dolphin's
`Dolphin.DSP.Backend=No Audio Output`. The older `Null` sink is invalid and
falls back to Cubeb; `No Audio Output` removes the host sink while preserving
DSP and source audio execution. Calibrate a changed collector or JIT backend
against two immutable Interpreter64 captures on the same bounded control
trajectory before using it for corpus capture:

```sh
python3 scripts/calibrate_retail_collector.py \
  --reference-a work/reference/complete-a.jsonl \
  --reference-b work/reference/complete-b.jsonl \
  --candidate work/reference/complete-jit.jsonl \
  --candidate-cpu JITARM64 \
  --output work/reference/jit-calibration.json
```

That report compares entry, actual inputs, state and end records while allowing
the explicitly selected candidate backend/collector identity to differ. Its
scope is the selected trajectory only; it is not broad equivalence,
performance acceptance or gold admission. Expanded workloads still require
independent repeat captures with the same collector/profile. Cross-check a
representative complete workload in the interpreter when extending the
calibrated execution coverage; compare it to the repeated JIT pair using
explicit `--reference-cpu JITARM64 --candidate-cpu Interpreter64`. A failed
cross-check remains red even if the JIT pair repeats.

The calibration recipe v2 carries the complete semantic PAD configuration and
Master/Copy/Game histories into source initialization. Compare those histories
as well as fighter fields on every tick; fresh-zero PAD state is not equivalent
to entering from an ordinary menu. The native queue and rumble pointers stay
owned by the port. See [the wire and phase contract](RETAIL_REPLAY_CAPTURE.md).
The optional retail draw audit binds every camera traversal to its source tick
and reports any declared-state mutation. Its passing neutral sequence does not
replace an active, visibly rendered replay or prove pixel agreement.

Capture PAD samples at actual source consumption, not at every hardware poll:
raw polls can continue during scene loading. The collector publishes the first
input at VS entry, bootstraps one input of lookahead at the first protected
HSD dequeue, then observes PADRead before interrupt restoration to publish the
next vector. It reads the consumed slot after the queue decrement while
interrupts remain disabled, and checks all four actual PAD ports at scheduler
return. Observe state after the original GObj scheduler. The game match counter
can remain zero during Ready, while the source scheduler still advances.
Duplicate debugger observations are accepted only when their boundary identity
and complete payload agree; incomplete, reordered or ambiguous captures are
invalid evidence, not gameplay divergences.

Draw audits record observed source indices and validate sparse ordered camera
traversals; they do not synthesize one draw for every source tick. The optional
`--require-match-complete` bound requires the collector exit callback, JSONL end
record and `match-completion.json` sidecar, including its capture hash, final
draw index and match-end observations.

The initial profile is GALE01 revision 2, NTSC, singles, supported content and
vanilla rules. UCF, PAL, Frozen Stadium and online initialization cannot silently
inherit that profile. Modern UCF recordings remain useful input donors: their
independently captured vanilla trajectory supplies the expected state. Measure
coverage on that new trajectory because removed modifications can change the
interaction or end the match earlier.

For rollback recordings, assemble complete frame attempts and apply the pinned
format's finalization rules. Never mix a pre-frame from one speculative attempt
with a post-frame from another, or treat the largest observed watermark as proof
that every intervening frame is present. Reject unsupported/incomplete histories.

### 4. Run the same timeline visibly

After a replay passes deterministic source-state comparison, run it at real
time in the Release browser with normal rendering and audio. The existing hard
gates apply: zero pauses, intervals over 33.3 ms, long tasks, audio underruns,
live pipeline creation, aborts and WebGPU errors. Record native phases, GPU
completion when available, heap high-water, uploads and pipeline descriptors.

Replay parsing, compilation and match preparation are measured separately and
finish before the source clock starts. Playback must not skip rendering, mute
source audio work, accelerate source ticks or synthesize samples after a host
stall.

Pipeline discovery is a separate corpus pass. It gathers descriptors from
unchanged visible draws, persists them only after complete source teardown, and
then reruns cold and warm acceptance from a clean origin. A discovery pass is
not a performance pass.

## Corpus and coverage

Keep replay bytes outside Git. The local importer recursively indexes a user
selected directory and writes a privacy-minimized manifest containing hashes,
format/profile eligibility, stage, fighters, costumes, duration and coverage.
Player names and connect codes are neither required nor copied into reports.

The official `slippi-js` fixture set bootstraps parser compatibility and edge
cases. The audited public tournament corpus supplies gameplay and performance
breadth, while an independently captured vanilla corpus supplies the strict
GALE01 revision-2 oracle. The measured corpus limits and generation plan are in
[SLIPPI_CORPUS_AUDIT.md](SLIPPI_CORPUS_AUDIT.md). The manifest records
provenance without publishing copyrighted assets or personal replay metadata.

Coverage is measured from execution, not filenames. For every replay collect:

- common and character-specific action states;
- animation-command branches and admitted original source functions;
- attacks, grabs, throws, shields, hits, ledges, recoveries, deaths and respawns;
- Articles, items, effects and stage schedulers;
- render pipeline descriptors, uploads and allocation high-water marks; and
- compared state fields and first divergences.

Select a small canary set that maximizes unique coverage, then deduplicate
longer corpora by marginal coverage. Pull requests run the canary set; fighter
or stage admission runs a coverage-selected acceptance set plus held-out
recordings and repeated cold/warm visible samples; scheduled runs process the
broader local corpus. Use measured marginal coverage to grow the set, rather
than making every pull request replay thousands of redundant matches. This
keeps routine iteration quick without replacing breadth with a few scripted
moves.

## Admission result

A fighter/stage pair is admitted only when its machine-readable report shows:

1. source identity, assets and ownership checks pass;
2. every eligible canary replay completes with no state divergence;
3. corpus coverage meets the versioned requirements for that content, with
   uncovered rows listed explicitly;
4. visible Release-browser cold and warm replay samples pass every hard gate;
5. repeated full matches tear down without unexplained retained mutable state;
   and
6. transition, rendering and audio evidence required outside `.slp` is still
   green.

The report feeds the evidence inventory in issue 3. A smaller model can add
content, but it cannot mark the content admitted or suppress an unsupported row;
the gates and evidence generator make that decision.

## Implemented calibration boundary

Normalization, the input-only v2 workload runner and the full-source prefix
export are implemented. The Fox/Falco Battlefield donor is a processed-v2
derived workload; its complete-match prefix is 3,122 ticks beginning at -123,
while its source file and provenance remain the full 9,898-frame donor.
Independent JITARM64 captures A and B repeat exactly through the original
elimination exit and final draw. The port now matches all declared fields in
headless and visible Release execution and tears down cleanly. Final visible
Release cold/warm timing gates pass with zero failures. The final protected-
dequeue collector also passes the entire Interpreter64 cross-check against the
repeated reference pair. No broad gold or content admission follows from this
trajectory. The
[complete-game evidence ledger](COMPLETE_REPLAY_CALIBRATION.md) records hashes,
shared arithmetic/memory fixes and the measured coverage gaps.

The 686-frame movement donor and the 240-tick neutral pair remain scoped
calibration evidence for parser, PAD-history, queue and draw controls. They do
not establish combat equivalence, complete-match teardown, audio-output
agreement or browser acceptance. See [the reproducible capture procedure](RETAIL_REPLAY_CAPTURE.md)
for exact provenance, CPU selection and retained negative evidence.

## Immediate implementation order

The parser/indexer, rollback normalization, input-only runner, independent
retail capture and scoped port comparison are working. Keep their negative
controls and the retained pre-fix RNG divergence as calibration evidence.

1. Freeze the passing full-game interpreter/JIT calibration and visible Release
   cold/warm evidence as regression controls. Keep ambiguous controller
   observations and timing failures red; require exact state, original ending,
   final draw and teardown.
2. Select a small representative set and an untouched held-out set from observed
   vanilla trajectories. Prioritize dense combat, up-special, ledge, grab/throw
   and stage-specific gaps; donor post-frame coverage is not vanilla coverage.
3. Reuse immutable reference traces during routine port iteration. Capture a
   new independent pair when its input, source setup or reference profile changes.
4. Make the joined state/lifecycle/performance evidence the admission gate, then
   resume broader fighter/stage integration. Further exporter optimization is
   justified by measured capture cost; a dashboard is not a prerequisite.

Completed-file singles playback comes first. Seeking, live spectating, rollback
presentation, doubles, items-on rules, netplay and a coverage dashboard follow
once this end-to-end validation path is dependable. Machine-readable evidence
and useful first-divergence reports are needed now; the dashboard is not a
prerequisite.
