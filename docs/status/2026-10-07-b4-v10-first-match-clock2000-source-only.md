# B4 v10 first-match clock-2000 source-only audit

**Source identified**

A bounded source-only audit reached the first match-0 row at `match_frame = 2000`: tick 2123, source sequence 14728, PAD sequence 14727, timeline index 3307, and cursor 3308. It rejoined the accepted clock-1, clock-60, clock-300, clock-500, and clock-1000 checkpoints in order. Four primary entities were present at the target, and source state fields were observed through this boundary.

The audit read 14,729 records / 91,371,214 bytes and freshly hashed exactly those consumed bytes: `3f5eaf2c845a387d22c1da7abf74d0f4e4ca2c40fd8503b31a890f0f3f6b7296`. The retained full-trace identity is 2,137,257,881 bytes with recorded SHA-256 `81f8a0a3c8cac644e86be2e07451a5876be08b60ebb4295290a35c8a187c5580`; it was not recomputed, and before/after source stat remained stable. The audit stayed within 128 MiB and 24,000 records.

CSS/SSS ranges account for 1,184 frames of recipe input order only; they are not compared state. This is a source observation, not a browser comparison. The result remains `complete: false` and `whole_session_equivalent: false`; it says nothing about later matches, the whole roster, pixels, audio, live timing, performance, or gameplay acceptance.

The [portable receipt](../evidence/b4-v10-first-match-clock2000-source-only-v1.json) retains the source, runner, packet, prefix, checkpoint, preflight, preparation-failure, and review identities. A bounded comparison is pending a separate review because the current generic ordered comparator is limited to 64 MiB / 12,000 records, below this prefix.

Issue [#237](https://github.com/ericvanlare/melee-web/issues/237) covers this source-only boundary. No browser comparison through clock 2000 or full-session acceptance is claimed.
