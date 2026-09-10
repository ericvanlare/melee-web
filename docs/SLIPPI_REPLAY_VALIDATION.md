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

Prefer modern raw-rich recordings. Missing raw fields in older recordings may
be converted into explicitly derived canonical PAD workloads later; those bytes
are not recovered original hardware input. Even modern physical trigger floats
can admit more than one original byte, so record the canonicalization policy.
Do not inject processed stick floats into a raw controller boundary.

Construct the match once from the declared fixture initialization, then supply
one input vector per source tick. Never write recorded positions, action states,
damage, stocks or per-frame RNG into the running game. RNG after initialization
is an observation/assertion, not a repair mechanism. Compare exact bits first;
any numerical tolerance needs a documented field-specific rationale.

### 3. Establish reference trust before interpreting a red

Pin the retail executable, Dolphin build, source revision, CPU mode, settings,
save/setup recipe and collector. Dolphin remains an external validation tool;
no emulator enters the browser runtime. Preserve ordinary retail setup and
capture the complete `StartMeleeData` at VS entry. Decode native source fields
explicitly; never transplant PPC pointers or C bitfield layouts.

The calibration recipe v2 carries the complete semantic PAD configuration and
Master/Copy/Game histories into source initialization. Compare those histories
as well as fighter fields on every tick; fresh-zero PAD state is not equivalent
to entering from an ordinary menu. The native queue and rumble pointers stay
owned by the port. See [the wire and phase contract](RETAIL_REPLAY_CAPTURE.md).
The optional retail draw audit binds every camera traversal to its source tick
and reports any declared-state mutation. Its passing neutral sequence does not
replace an active, visibly rendered replay or prove pixel agreement.

Capture PAD samples at actual source consumption, not at every hardware poll:
raw polls can continue during scene loading. Observe state after the original
GObj scheduler. The game match counter can remain zero during Ready, while the
source scheduler still advances. Incomplete, reordered or ambiguous captures
are invalid evidence, not gameplay divergences.

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

Normalization and the input-only v2 workload runner are implemented. A 686-frame
Fox/Falco Battlefield recording completes source playback and teardown without
an expected-state comparison. The independent retail/port calibration passes
240 neutral Mario/Mario Final Destination ticks and exposed shared particle-bank
and rumble gaps. See [the reproducible capture procedure](RETAIL_REPLAY_CAPTURE.md)
for exact provenance, field/phase limits and retained negative evidence.
No Slippi fixture is gold-admitted yet. The typed initial PAD contract and its
240-tick comparison pass. The neutral retail draw audit also passes for declared
fields; active source draw-phase coverage remains open. Next run the modern
input donor against the reference and in the visible browser.

## Immediate implementation order

The parser/indexer, rollback normalization, input-only runner, independent
retail capture and scoped port comparison are working. Keep their negative
controls and the retained pre-fix RNG divergence as calibration evidence.

1. Retain the passing typed master/copy/game PAD history/configuration and
   neutral source draw-audit controls. Extend the draw lifecycle evidence to
   active replay before claiming general input equivalence.
2. Feed the existing modern Fox/Falco Battlefield recording through the same
   reference process without arbitrary opponent replacement. Capture a new
   vanilla trajectory and compare it; the original UCF state stays excluded.
   Require independent repeatability, actual consumed-input agreement and a
   complete port comparison before admitting the fixture.
3. Run that workload visibly in Release, cold and warm, with normal source audio,
   rendering, hard hitch gates and complete teardown. Headless runtime timings
   are never browser acceptance evidence.
4. Validate a faster offline reference exporter against the small GDB oracle;
   reuse immutable expected traces during routine content iteration.
5. Add execution coverage, select canaries and a held-out set, then broaden
   supported matchups/stages and make evidence generation the admission gate.

Completed-file singles playback comes first. Seeking, live spectating, rollback
presentation, doubles, items-on rules, netplay and a coverage dashboard follow
once this end-to-end validation path is dependable. Machine-readable evidence
and useful first-divergence reports are needed now; the dashboard is not a
prerequisite.
