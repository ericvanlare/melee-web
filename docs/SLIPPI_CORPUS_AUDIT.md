# Slippi corpus audit

Audit date: 2026-09-10

## Decision

The public Slippi tournament corpus is large enough for browser performance,
resource discovery and broad downstream-gameplay coverage. It is not a suitable
standalone non-UCF vanilla oracle. The sampled supported games all enable UCF,
and most use older replay versions that do not expose every modern raw
controller field.

Use the public corpus in two ways:

1. play coverage-selected games visibly at real time as performance workloads;
2. compare source state downstream of an explicitly declared processed-input
   boundary where the recorded format permits it.

Build the vanilla gold corpus separately by replaying physical input histories
through the pinned, cheats-disabled GALE01 revision-2 reference and capturing
the resulting vanilla state. The source `.slp` state is not the expected result
after UCF is removed; the independently captured vanilla result is.

## Dataset and method

The audited dataset is
`erickfm/slippi-public-dataset-v3.7` at revision
`c82be5f6e43f3388555cfe0cf8652580601f396d`. Its card describes approximately
95,102 unique tournament games, with hand warmers, doubles and matches shorter
than 30 seconds pruned.

The audit enumerated the current Hugging Face repository tree for FOX, FALCO,
MARTH and MARIO, deduplicated files by their LFS object hash, and retained games
whose two character directories and filename stage identify a currently
supported cross-character match on Final Destination, Battlefield, Yoshi's
Story or Dream Land. Dittos were omitted from this count, making it
conservative.

This produced 6,813 unique candidate games:

| Matchup | Games |
| --- | ---: |
| Falco / Fox | 2,998 |
| Fox / Marth | 2,359 |
| Falco / Marth | 1,370 |
| Fox / Mario | 37 |
| Falco / Mario | 28 |
| Mario / Marth | 21 |

| Stage | Games |
| --- | ---: |
| Battlefield | 2,160 |
| Yoshi's Story | 1,801 |
| Dream Land | 1,439 |
| Final Destination | 1,413 |

The audit fetched only each sampled file's initial byte range and decoded the
versioned Game Start event defined by the Slippi specification. It inspected
both dashback-fix and shield-drop-fix fields for every active player. Two full
files, one version 2.0.1 and one version 3.9.0, were also parsed with the
official `@slippi/slippi-js` 9.1.3 library to cross-check the header decoder.

The sample combined 25 deterministic samples from each available
matchup/stage stratum with a deterministic uniform sample of 600 candidates.
After overlap removal it contained 952 files. All 952 parsed successfully and
all 952 enabled UCF. The uniform 600-file subset also contained zero UCF-off
games. With zero observations, the one-sided 95% binomial upper bound for the
UCF-off fraction is approximately 0.50%. This bound describes the sampling
model; it does not prove that the complete corpus contains no UCF-off file.

The uniform subset contained 540 version-2.0.1 files and 60 version-3.9.0
files. Those versions predate some later physical-axis fields. They can still
provide extensive visible performance and declared post-input evidence, but
they cannot silently receive full raw-`PADStatus` equivalence credit.

## Resulting corpus plan

Maintain three machine-readable evidence classes:

- **Vanilla gold:** exact or independently captured vanilla initialization,
  physical inputs and reference state. This is eligible for the GALE01
  revision-2 accuracy gate.
- **Processed-input comparison:** legacy or UCF recordings supplied at a named
  post-processing boundary. This tests the original fighter/stage engine after
  that boundary and lists controller preprocessing as excluded evidence.
- **Performance workload:** any supported recording whose match assets and
  runtime behavior are admissible, played through normal Release rendering and
  audio without using its state as a vanilla oracle.

For scalable vanilla generation, prefer modern Slippi files that contain all
physical controller fields. Extract only their physical input histories and
match inventory. Run those inputs from ordinary match construction in the
existing pinned retail reference with UCF and other gameplay modifications
disabled. Capture the vanilla outcome at the same source boundary used by the
port comparator. Human inputs remain useful after the resulting match diverges
from the UCF recording; the new retail capture defines the expected trajectory.

Select reference runs by marginal execution coverage rather than raw replay
count. Keep adding candidates until action states, animation commands, Articles,
effects, attacks, collisions, ledges, deaths, respawns and stage schedulers stop
gaining coverage, then add focused generated inputs only for the remaining
rows. This should require hundreds of gold traces rather than storing and
running thousands of redundant full matches for every change.

## Remaining acquisition work

No large public non-UCF corpus was identified in this audit. The official
Slippi SDK fixtures prove that UCF-off offline recordings exist, and Slippi's
console build keeps recording and UCF as separate modules, but those sources do
not provide broad match coverage.

Before building the vanilla generator, sample the newer ranked corpus by replay
version and physical-field completeness. Its online games remain UCF workloads;
their value is supplying diverse modern raw input histories, not providing the
vanilla expected state.
