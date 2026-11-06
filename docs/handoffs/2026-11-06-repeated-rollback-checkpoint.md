# Repeated rollback checkpoint

Execution issue: [#372](https://github.com/ericvanlare/melee-web/issues/372).
Branch: `codex/runtime-repeated-respawn`.
Base: `a70cabdb6e166f543e587c6791a2bbb4c121eb39` (merged #371).
Executable source and capture harness:
`3a470ddfc76d6b5db5779e515412a54dcfbce9ac`.

## Completed boundary

PR #371 merged the independently reviewed 120-input local two-browser
late-correction result. Seven source ticks replay without draw, the corrected
frame is presented, and both peers converge on equal local final checksums.
Its full regression, affected builds and exact publication-head CI passed.
This is finite local functional evidence, not a complete online match.

The current branch extends that owner to a 600-input stock-loss/respawn
scenario. Reviewed repairs permit an authenticated music refill at explicit
interior operations and isolate draw-authored shadow membership/material links.
The music repair passed 13 focused controls and its private Release build.
The final shadow repair passed its focused controls and private Release build.
Neither repair relaxes the timing guard or input/history limits.

## Latest retained result

The unchanged scenario at the executable source above crosses the earlier
music and shadow refusals. Alpha completes both declared seven-tick
corrections: PRE21 to frame28 for late input26, and PRE221 to frame228 for
late input226. Original keyboard input produces an observed stock change
from four to three by frame220. Grounded respawn is not observed.

The run fails immediately after the second reconciliation/rebase at alpha's
zero-budget clock sample: frame228, elapsed65.61ms, accumulated debt9 against
the unchanged limit8. That sample spans restore, replay, corrected
presentation/idle and rebase/capture; it does not identify one slow operation.
Beta later also stalls while awaiting remote input228. The third correction
and the 600-input completion are unrun.

Outer cleanup passes, with all 19 recorded browser PIDs absent. The retained
H1 export does not identify an H1 incident; the native clock stall must remain
separate. Preserve the failed capture and earlier failed source/fixture runs.

## Resume

1. Read the independent raw-result review and retained command timing/queue-wait
   breakdown for the second correction. Reuse the existing source owner and
   reviewer; do not repeat the capture unchanged.
2. Fix the measured bottleneck. If current timings cannot attribute it, add a
   bounded reconciliation-subphase timing receipt before another experiment.
   Do not reset clock debt, extend limits or auto-resume the stalled run.
3. Complete the existing 600-input pass criteria, then run full regression,
   affected builds, independent review and exact-head CI before merging #372.
   The current branch has not passed this integration validation.
4. Continue to confirmed match termination and local original Results handoff.
   Source match-frame counters stop advancing during the original ending tail;
   use the retained terminal-interface review before implementing that boundary.
   Menus and Results remain outside rollback.

Find machine-local evidence through the autonomous coordination checkpoint's
`rollback_renderer_resume_20261028.runtime_sustained_current` entry. Its
`repeated_respawn_latest` points to the latest failed capture, whose sibling
independent review records exact producers and artifact hashes. Incremental
builds remain retained for resumption. No browser capture is running.
