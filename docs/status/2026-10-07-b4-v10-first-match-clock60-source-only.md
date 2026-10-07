# MWRC v10 source-only first match clock-60 audit

**Source identified**

Source-only bounded observation passed; browser comparison through this boundary is pending.

The retained GALE01 revision-2 MWRC v10 source trace passed provenance, full recipe input order, CSS/setup binding, PAD-to-tick join and primary fighter-entity checks through the first match-0 `match_frame` value at least 60. Before continuing, the audit rejoined the accepted clock-1 boundary against its exact source tuple and freshly consumed prefix digest. The clock-60 target was tick 183 / sequence 5030, after PAD consume 5029, at timeline index 1367 and candidate browser cursor 1368.

The audit consumed 5,031 records and 12,780,628 raw bytes through the target. Its fresh bounded-prefix SHA-256 is `32b763453688c6335b90c9807b71c79d919b85e6e7435a3c2abccd1792f00e77`. The full 2,137,257,881-byte trace kept its recorded SHA-256; it was not rehashed, and its before/after stat identity remained stable. CSS frames 0–936 and SSS frames 937–1183 established consumed-input order only. The audit did not compare clock-60 state with the browser.

The existing browser export requested cursor 1309, observed 1311 and exported through 1314, so it does not reach cursor 1368. Its provenance was checked; the prior bounded browser comparison remains limited to clock 1. A fresh ordinary browser capture and separately reviewed bounded comparator scope are required to compare through clock 60. This result remains incomplete and is not whole-session acceptance.

The first v2 launch was rejected during argument selection because four browser-sidecar path values had a date typo; it exited before MWRO stat or stream open. That failure is retained. The corrected v3 packet was separately reviewed and executed once. Exact source, packet, report and failure identities plus the next-scope proposal are in the portable [receipt](../evidence/b4-v10-first-match-clock60-source-only-v1.json).

Issue [#202](https://github.com/ericvanlare/melee-web/issues/202) remains open for a separately reviewed bounded browser comparison. This source-only result does not include a browser capture, comparator implementation, full replay or second original-trace read.
