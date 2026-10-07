# Bounded MWRC v10 first-setup/tick-0 comparison

**Compiled / Source identified / Retail compared / Browser exercised**

One bounded comparison joined the original GALE01r2 v10 source capture to the
existing ordinary headless browser capture at cursor 1200. It stopped after
the first match setup and completed match-0 source tick 0. The comparison
result is `boundary_result: equivalent`; the overall result remains
`incomplete`, with `complete: false` and `whole_session_equivalent: false`.
The incomplete CLI exit 1 is expected for this prefix scope.

The comparator ordered 1,184 CSS/SSS input frames without comparing menu
scalar fields (`nonmatch_fields_compared` is empty), then compared one setup
and one match-tick state using RNG, match frame, semantic PAD bytes, Fighter
state and strict primary fighter-entity identity. It also checked the declared
setup. It did not compare later match frames. Every match row in the existing
browser trace reports `match_frame: 0`, so this is first-entry state evidence,
not active-gameplay evidence. The browser capture and runtime identity are
documented in the [primary-identity entry](2026-10-07-b4-v10-primary-identity-target1200.md).

The original prefix contains 4,116 records and 5,364,736 bytes through source
sequence 4115; its freshly computed SHA-256 is
`4ace98afe66c1bb2229c0289b9d76bc321049f1434c68a65fd7ee5b0840db21a`. The full
2,137,257,881-byte MWRO keeps its previously recorded SHA-256
`81f8a0a3c8cac644e86be2e07451a5876be08b60ebb4295290a35c8a187c5580`; the
comparison did not recompute that full-file hash. Its pre/post stat identity
was stable. The [portable receipt](../evidence/b4-v10-first-setup-tick0-comparison-v1.json)
binds the exact recipe, source capture, browser sidecars, expectation packet,
comparator commit and result, and retains both failed preparation attempts
separately from the successful comparison.

This prefix does not establish whole-session equivalence, later gameplay,
active-match accuracy, pixels, PCM, timing, performance or competitive
readiness. The existing v8/v9 whole-session path remains the default; v10 uses
the explicit bounded scope documented in
[recorded-session comparison](../RECORDED_SESSION_STATE.md).
