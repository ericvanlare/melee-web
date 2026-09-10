# Slippi replay validation architecture

Slippi replays are the primary scalable gameplay workload for this port. A
single normalized replay drives both deterministic source-state comparison and
visible Release-browser performance. Hand-authored action traces remain short
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
- expected state fields with presence masks and original float bits;
- RNG observations, item/stage observations and match-end data; and
- a content hash for cache and evidence identity.

The original gameplay runtime consumes that timeline through the same
`PADStatus` boundary as a physical controller. Slippi parsing must not become a
dependency of fighter, stage or HSD source code.

### 2. Simulate; do not resynchronize

Construct the match from the recorded Game Info Block, source settings and
game-start seed. Then supply finalized controller samples one source tick at a
time. Never write recorded positions, action states, damage, stocks or per-frame
RNG into the running port. Doing so would conceal the divergence being measured.

At the matching original post-frame boundary, compare every available declared
field and stop on the first mismatch. Report replay hash, frame, port, field,
expected and actual bit patterns, nearby inputs, RNG and relevant source state.
Per-frame RNG is an assertion. It is not a repair mechanism.

Modern files expose raw axes and physical buttons progressively by format
version. Exact-input admission initially requires a version new enough to carry
all controller fields needed to reconstruct `PADStatus`. Older recordings may
be used as stress workloads but cannot silently receive exact-input credit.

### 3. Treat modifications as named profiles

The first accuracy profile is GALE01 revision 2, NTSC, singles, vanilla rules,
supported fighters/stages and no gameplay-modifying codes. A file with UCF,
PAL, Frozen Stadium or another modification is rejected from that profile.

Additional profiles may be added when their code and settings are explicitly
supported. A tournament Slippi workload is useful performance evidence even
when it is ineligible for a vanilla-retail accuracy claim. The report always
names the profile and the reason each replay was accepted or rejected.

For rollback recordings, consume only the finalized frame sequence indicated by
frame bookends. Superseded speculative frames can separately stress rollback
infrastructure later; they are not ordinary match simulation inputs.

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
or stage admission runs every eligible replay for that content plus repeated
cold/warm visible samples; scheduled runs process the full local corpus. This
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

## First implementation slice

1. Pin `slippi-js` and import its public fixtures as external test inputs.
2. Build the corpus indexer and normalized timeline schema with parser negative
   tests and explicit version/profile rejection reasons.
3. Add a headless `GameplayReplaySession` that constructs an ordinary source VS
   match, supplies exact `PADStatus` frames and reports the first post-frame
   divergence without resynchronizing.
4. Automate replay of modern physical-input histories through the pinned
   cheats-disabled retail reference to generate coverage-selected vanilla gold
   traces; never reuse the UCF recording's state as the vanilla expectation.
5. Pass one short supported Fox/Falco/Marth replay on Final Destination,
   Battlefield, Yoshi's Story or Dream Land, then expand across every eligible
   official fixture and local replay.
6. Add file loading and real-time playback to the browser and emit the existing
   performance evidence schema.
7. Generate the coverage-maximizing canary manifest and make it the default
   character/stage admission gate.

The first slice deliberately starts with completed replay files, singles and
the currently supported content. Live spectating, seeking, rollback display,
doubles, items-on rules and unsupported modifications can reuse the timeline
later without complicating the validation foundation.
