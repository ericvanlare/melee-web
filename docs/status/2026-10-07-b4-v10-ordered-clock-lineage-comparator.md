# B4 v10 ordered clock-lineage comparator controls

**Synthetic comparator validation**

The explicit `v10-first-match-clock-ordered-lineage` scope now accepts an externally bound ordered list containing the clock-1 and clock-60 anchors, intermediate source-audited checkpoints and a terminal target. Focused production-path fixtures exercised three, four, five and six checkpoints through a terminal clock-90 target; they also verified first-mismatch stopping and rejection of altered inner checkpoint data despite a refreshed outer packet identity. The 81 focused regressions passed under the reviewed source revision.

These controls use synthetic source records/byte accounting and fixture source/browser provenance. They validate the comparator contract only. No retained MWRO content was read for these tests, and they do not compare original state with a browser. The separate [clock-1000 source-only observation](2026-10-07-b4-v10-first-match-clock1000-source-only.md) is not a browser comparison.

The [portable comparator receipt](../evidence/b4-v10-ordered-clock-lineage-comparator-v1.json) records the reviewed and rebased source identities, limits, test scope and evidence limitations. Full-suite current-main integration validation is recorded there when complete.
