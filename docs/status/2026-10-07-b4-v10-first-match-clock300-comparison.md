# MWRC v10 first match clock-300 comparison

**Compiled / Source identified / Retail compared / Browser exercised**

One bounded GALE01 revision-2 MWRC v10 comparison joined the retained original source to an ordinary state-mode browser replay through the first match-0 `match_frame` at or above 300. The source and browser agreed on the setup and all 424 contiguous match-0 ticks from source tick 0 through tick 423. The terminal row was source sequence 6230 after PAD consume 6229, at recipe timeline index 1607 and browser cursor 1608. The report recorded no declared-field difference.

The comparison first rejoined the accepted clock-1 prefix at source sequence 4735, then the clock-60 prefix at sequence 5030. It freshly hashed 22,506,388 consumed source bytes across 6,231 records through sequence 6230. The full MWRO retained its previously recorded size and SHA-256; it was not rehashed, and its stat stayed stable during the bounded read.

The browser requested cursor 1608, observed 1614, and exported 1615 contiguous frames. The trace contained 937 CSS and 247 SSS rows, which established recipe input order only; no scalar state fields were compared for those rows. One match setup and match frames corresponding to ticks 0–423 were compared for RNG, match frame, PAD state, fighters and primary fighter entities. The seven exported match rows after the target received shape and order validation only.

The capture ended incomplete after manual unload: the capture child and owner returned 1. The pre-stop browser-error and unexpected-request arrays were empty; the capture failure log retains the incomplete-boundary reasons. The comparison CLI also returned 1, as expected for `boundary_result: equivalent`, `result: incomplete`, `complete: false` and `whole_session_equivalent: false`. The historical browser producer schema was accepted only for this post-clock-60 boundary scope; its frozen manifest identity remains unchanged.

The capture used producer `7127265` and its preserved 32-file Release manifest. The comparison ran with producer `fc4222d`. Integration rebased both owned commits unchanged onto current main and validated producer `ad8c94e`; its Release build changed only the WASM artifact relative to the captured 32-file inventory. The current-main build was not used for a new browser capture or comparison. The full Python suite completed 2,001 tests with 145 skipped, and the default-off Release runtime build succeeded. The [portable receipt](../evidence/b4-v10-first-match-clock300-comparison-v1.json) separates those capture, comparison and integration identities and lists retained preparation failures.

This is an incomplete first-match prefix result. It does not establish state agreement after match frame 300, later matches, Results, the whole session, pixels, PCM, live timing, performance, physical input, competitive readiness or admission.
