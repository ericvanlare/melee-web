# B4 v10 first-match clock-2000 comparison

**Compiled / Source identified / Retail compared / Browser exercised**

One bounded comparison joined the retained GALE01 revision-2 source trace, using its v10 input recipe, to the ordinary state-mode browser capture through the first match-0 `match_frame` at or above 2000. One setup and 2,124 contiguous declared match-state frames from source tick 0 through 2123 agreed on the compared fields: RNG, match frame, PAD state, fighters and primary fighter entities. No declared field differed.

The comparison consumed 91,371,214 source bytes across 14,729 records through source sequence 14,728. The clock-1, clock-60, clock-300, clock-500, clock-1000 and terminal clock-2000 prefixes all rejoined in order with fresh hashes before continuing. The full MWRO retained its recorded size and SHA-256; it was not rehashed, and its stat was stable during the bounded read.

The capture requested cursor 3308, observed 3309 and exported 3310 contiguous session frames through EOF. Its trace contained 937 CSS, 247 SSS and 2,126 match frames. CSS/SSS rows establish recipe input order only; `nonmatch_fields_compared` is empty. The two exported match rows after the target received shape and order validation only, not source-state comparison.

The bounded capture ended incomplete after unload; capture child and owner returned 1. The retained capture report lists the bounded unload, incomplete input timeline and source tick/draw count mismatch; its capture-level `first_mismatch` is that final CSS was not entered. Those are capture-completeness diagnostics, not a compared-field divergence. The comparison report has `first_difference: null`. Pre-stop browser errors and unexpected requests were empty; both 32-artifact inventories and strict process cleanup passed. The comparison CLI also returned 1, as expected for `boundary_result: equivalent`, `result: incomplete`, `complete: false` and `whole_session_equivalent: false`.

The [portable receipt](../evidence/b4-v10-first-match-clock2000-comparison-v1.json) separates the source-only audit, browser capture, comparator, runtime reuse and prior validation identities. It retains the preparation failures and root reviews. The current 32-file runtime inventory is fresh; no complete historical 32-file inventory for producer 7740 exists. Three generated outputs match retained 7740 bytes, 29 copied web sources match unchanged source blobs, and the native target-input delta is empty.

The prior Python suite completed 2,039 tests, with 151 skipped, on the comparison producer. No full suite or runtime build was rerun for this documentation-only receipt.

This remains an incomplete prefix result. It does not establish later match frames, later matches, Results, return to CSS, whole-session accuracy, pixels, PCM, live timing, performance, physical input, competitive readiness or admission. Issue [#246](https://github.com/ericvanlare/melee-web/issues/246) tracks the bounded cap-policy and comparison boundary.
