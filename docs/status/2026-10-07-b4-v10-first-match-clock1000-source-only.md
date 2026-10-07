# B4 v10 first-match clock-1000 source-only audit

**Source identified**

A bounded source-only audit passed through the first match-0 row with `match_frame >= 1000`. It observed tick 1123 / source sequence 9730, after PAD sequence 9729, at timeline index 2307 and candidate browser cursor 2308. The audit joined the accepted clock-1, clock-60, clock-300 and clock-500 tuples and freshly consumed prefix digests in order before continuing.

The audit consumed 9,731 records and 50,873,188 raw bytes. Its fresh prefix SHA-256 is `7f71d2c8c9529e8171dfab854cd114abb0eef44ca8d59c6f7e96af5495d13cf7`. The retained full-trace identity remained the previously recorded 2,137,257,881-byte trace with SHA-256 `81f8a0a3c8cac644e86be2e07451a5876be08b60ebb4295290a35c8a187c5580`; it was not rehashed, and before/after stat identity was stable. Caps were 64 MiB and 12,000 records.

CSS frames 0–936 and SSS frames 937–1183 establish recipe input order only. Four primary fighter entities were present at the target, and the observed match ticks were contiguous through tick 1123. This is a source observation, not browser comparison; the result remains incomplete and `whole_session_equivalent` is false. It does not establish later gameplay, whole-session accuracy, pixels, audio, live timing or performance.

Two metadata preflights failed before any source open: v1 had a launch-packet path mismatch, and v2 had a source-trace identity mismatch. V3 passed metadata preflight without opening the source, then the single authorized audit completed. The portable [receipt](../evidence/b4-v10-first-match-clock1000-source-only-v1.json) retains the run, source, checkpoint and review identities.

Issue [#218](https://github.com/ericvanlare/melee-web/issues/218) records this bounded source-only outcome. It does not claim browser comparison or whole-session acceptance.
