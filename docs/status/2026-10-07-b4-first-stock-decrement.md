# B4 v10 first typed stock decrement

**Source identified / Browser exercised / Retail compared**

Issue [#258](https://github.com/ericvanlare/melee-web/issues/258) compares the
recorded queue through the first typed fighter-stock decrement after clock 2000.
Slot 1 changes 4→3 at match clock 3483, source tick 3606 and browser cursor 4791.
All six earlier clock checkpoints (1, 60, 300, 500, 1000, 2000) rejoin their exact
freshly consumed source-prefix hashes before continuing. The scoped identities,
fields, counts and limits are in the
[portable receipt](../evidence/v10-first-stock-decrement-comparison-v1.json).

The historical [#252](https://github.com/ericvanlare/melee-web/issues/252)
128 MiB/24,000-record audit stopped at its cap before the event; it remains an
incomplete observation. The separate
[#256](https://github.com/ericvanlare/melee-web/issues/256) 256 MiB source audit
identified the event. This strict comparison retains its terminal 256 MiB/48,000
records/60-second bounds and the earlier checkpoint 128 MiB/24,000 bounds.

The fresh ordinary Release capture uses the merged Koopa callback adapter,
headless installed Chrome and muted speaker output. Native build and capture
producer is d1914d7; corrected Python comparator producer is 4dddc3b. The
browser stopped after observing cursor 4804 and exported through 4807. State is
compared only through 4791; the later export rows receive shape, input-order
and exact-EOF checks. Capture and comparison remain incomplete and return 1.
Both 32-file local/HTTP inventories and attributed process cleanup pass.

The earlier missing-TMPDIR startup failure and Wasm callback-signature failure
remain retained. The first fresh metadata preflight exposed missing stock-scope
boundary arguments in browser-export validation; the narrow Python correction
passes the real export validator with all six checkpoints and typed event,
including missing/malformed boundary negatives. Its first fixture cursor error
is also retained. The full suite passed on the corrected comparator producer;
no native rebuild or second capture was needed for that Python-only correction.

This is conditional recorded-queue state evidence for the declared match
fields. CSS/SSS frames establish consumed-input order, not menu scalar-state
equivalence. It does not establish KO cause, whole-session equivalence, live
input, foreground timing, pixels, PCM, performance or admission. The next
proposed source-only experiment is the first subsequent typed stock decrement,
rejoining this event under unchanged terminal caps; no next scan or browser
run is authorized by this receipt.
