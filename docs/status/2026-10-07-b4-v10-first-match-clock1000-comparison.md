# B4 v10 first-match clock-1000 comparison

**Compiled / Source identified / Retail compared / Browser exercised**

One bounded comparison joined the retained GALE01 revision-2 source trace, using its v10 recipe, to an ordinary browser capture through the first match-0 `match_frame` at or above 1000. The setup and 1,124 contiguous match ticks from source tick 0 through 1123 agreed on the declared fields. The terminal row was source sequence 9730 after PAD consume 9729, at timeline index 2307 and browser cursor 2308. No declared compared field differed.

The comparison freshly consumed 50,873,188 source bytes across 9,731 records through sequence 9730. The clock-1, clock-60, clock-300, clock-500 and terminal clock-1000 prefixes each rejoined in order before the reader continued. The original trace retained its previously recorded size and SHA-256; the full trace was not rehashed, and its stat was stable during the bounded read.

The capture requested cursor 2308, observed 2310 and exported 2312 contiguous session frames through EOF. The export contained 937 CSS, 247 SSS and 1,128 match frames. CSS/SSS rows establish recipe input order only; no state fields were compared for them. One setup and 1,124 match-state frames were compared for RNG, match frame, PAD state, fighters and primary fighter entities. Exported rows after the target received shape and order checks only.

The capture ended as an incomplete bounded prefix after unload: capture child and owner returned 1. The pre-stop browser-error and unexpected-request lists were empty. Both 32-artifact inventories and strict process cleanup passed. The comparison CLI also returned 1, as expected for `boundary_result: equivalent`, `result: incomplete`, `complete: false` and `whole_session_equivalent: false`. The historical v1 source audit does not record `report_write_failed`; this remains unobserved, rather than being treated as false. The [portable receipt](../evidence/b4-v10-first-match-clock1000-comparison-v1.json) separates capture, comparison and current-main validation identities and retains the preparation failures.

On current main, the focused comparator suite passed 80 tests and the full Python suite completed 2,029 tests with 151 skipped. The existing Release build was reused after all 60 runtime source inputs and all 32 artifacts matched their frozen identities; no runtime rebuild was needed for this comparator-only change.

This is an incomplete first-match prefix result. It does not establish gameplay after match frame 1000, later matches, Results, return to CSS, whole-session accuracy, pixels, PCM, live timing, performance, physical input, competitive readiness or admission.

Issue [#229](https://github.com/ericvanlare/melee-web/issues/229) tracks the bounded clock-1000 comparison boundary.
