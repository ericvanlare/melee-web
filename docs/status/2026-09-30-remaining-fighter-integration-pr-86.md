# Remaining fighter integration — PR #86

Current per-character coverage, original CPU9 references, rendered browser
match loops, comparison limits, and explicit open gates are recorded in
[Remaining fighter port notes](../REMAINING_FIGHTERS_PORT_NOTES.md).

Final integration checkpoint (2026-10-01): runtime implementation is `5a9830b`, based on
`origin/main` `3179aaa`. The copy-dynamics/parameter-endian repair at `3f8657f`,
Opening follower and copy-asset closure at `4b1975b`, public-export and Opening
reconciliation at `b03a653`, and the ftdemo `temp1.x5=-1` repair at `5a9830b`
are covered by scoped receipts. The deterministic poisoned-stack regression
fails with baseline `x5=-86` and passes with the original no-slot sentinel.

The fixed original-A run completed cleanly: the strict comparison is equivalent
over 42,492/42,492 frames (39,032 match and 3,460 nonmatch), with three timing
recoveries and `nonmatch_pad=uncovered`. The browser report passed with no
browser errors or finalization failures. The fixed Lineup-B run also compares
equivalently over 45,226/45,226 frames (41,907 match and 3,319 nonmatch), with
six timing recoveries and three setups; its nonmatch PAD field is uncovered.
Receipts and hashes are indexed in
`runs/pr86-demo-cache-fix-20261001/`.

The 12-distinct recording also passed its capture and strict comparison over
43,035/43,035 frames (38,296 match and 4,739 nonmatch), with 15 timing
recoveries and three match setups. Its v9 comparison includes indexed fighter
entities; the v8 A/B comparisons cover primary fighter fields. Runtime-data
bytes and hashes were verified. All three return to final CSS; nonmatch scalar
state remains outside the comparison scope. The earlier 12-distinct attempt
that omitted runtime-data verification is retained as a capture-configuration
failure, even though its declared-state comparison completed.

The final suite passed **1,679 tests with 76 skipped** (exit 0) with owned DOL, disc, symbols and source
inputs configured; its receipt is `runs/pr86-demo-cache-fix-20261001/full-suite.log`.
The clean public package passed 19/19 silent-public checks, and the clean
production audio package passed 19/19 audio-public and 13/13 audio-preview
checks in headless installed Chrome with muted output and the real disc. Their
portable receipts are under `runs/pr86-demo-cache-fix-20261001/`. These are
functional package and audio-transport checks; they do not establish
foreground timing, audible quality, pixels, PCM equivalence, physical-
controller support, performance, or admission.

The first clean attempts are retained as superseded diagnostics: silent public
reached 15 checks before a phase-7-to-8 timeout after three recoveries, preview
reached six checks before the same timing pause, and audio public initially
observed the shell’s asynchronous enabled-control race. The bounded test wait
for published control readiness preserves the original startup assertion; the
reruns passed without changing production timing. The historical pre-fix A
failure is retained separately: its four-stock match completed with Fox as
winner and Results began before the native camera-pool teardown error at source
cursor 13,835. The fixed A run crosses that boundary and completes.

The older Sheik-winner camera report remains an unattributed historical case;
it lacks the observations needed to identify its writer retroactively. Fresh
Results ownership/winner-demo checks and the current comparisons provide the
integration evidence. Complete roster admission and full-game equivalence
remain separate. No deployment is included. The [final integration receipt](../evidence/pr86-final-integration-v1.json)
binds these results to their inputs and runtime artifacts.
