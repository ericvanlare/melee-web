# B4 v10 nested ordered-prior validation

**Synthetic comparator validation**

The ordered clock-lineage validator now accepts an earlier ordered-scope packet as a nested prior while preserving the same source, recipe, anchor, audit, tuple and fresh-prefix checks. Synthetic production-path tests cover the historical clock-1000 prior under a clock-2000 boundary and a modern ordered clock-2000 prior under a clock-3000 boundary. They stop at a source-iteration sentinel; the retained clock-1000 metadata was also validated while opening the source trace was blocked. The validator retains bounded checkpoint, depth and work limits, and rejects cycles and refreshed outer hashes that conceal altered inner metadata.

The focused comparator module passed 84 tests. Full Python discovery passed 2,033 tests with 151 skipped. Its first run exposed a stale mock fixture that omitted the real boundary packet labels and validator keywords; the fixture was corrected without changing production code, then the comparator module and full suite passed. The tested fixture bytes were committed unchanged after validation.

This is synthetic metadata validation only. There is no authentic clock-2000 audit, no MWRO content read, no browser capture or original-to-browser comparison, and no whole-session acceptance claim. The historical clock-1000 audit’s absent `report_write_failed` remains unobserved. The [portable receipt](../evidence/b4-v10-nested-ordered-priors-v1.json) records exact implementation and test identities, prior runtime reuse evidence, and retained rebase/test failure chronology. This change touches only the Python comparator and its tests, so no runtime rebuild was needed.
