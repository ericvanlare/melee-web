# B4: next typed stock decrement compared after the accepted event

**Source identified / Browser exercised / Retail compared**

[Issue #271](https://github.com/ericvanlare/melee-web/issues/271) extends the
accepted #258 comparison through the first subsequent typed stock decrement.
Slot 3 changes 4→3 at source tick 3982, match clock 3859 and browser cursor 5167,
producing `[4,3,3,3]`. All six earlier clock checkpoints and the accepted stock
event freshly rejoin their observed state, completed PAD join and exact consumed
prefix before continuing. The portable
[comparison receipt](../evidence/v10-next-stock-decrement-comparison-v1.json)
binds the capture, validators, one original comparison and independent reviews.
The separate [#269 source-only receipt](../evidence/v10-next-stock-decrement-source-v1.json)
remains unchanged.

The comparison agrees on 3,983 match states (RNG, match frame, PAD, fighter fields
and fighter entities), one setup, and consumed input order for 1,184 nonmatch
frames. It stops at sequence 24022 / PAD sequence 24020, consuming 166,680,623
bytes and 24,023 records with fresh SHA-256
`c31863f04fcc44a454f316b9090989aac057ab868f8b3afb4a034604f53889dc`.
All 13 strict checks pass. The result remains `incomplete`, `complete=false`
and `whole_session_equivalent=false`; this is the first decrease after the
accepted event, not the historical first decrease after clock 2000.

The Mac mini capture uses headless installed Chrome with muted speaker output.
It requests cursor 5167, observes 5177 and exports 5179 frames. Every export row
passes shape, input-order, lineage and exact-EOF validation; the tail after 5167
is outside source-state equivalence. Capture and comparison retain exit 1 for
the intentional incomplete session. The actual loaded runtime data hash matches.

Native JS/Wasm/data retain producer d1914d7 and its original Release flags/cache.
Comparator and capture runner use tested producer bb891d0, based on main 0f129cf.
The 32 retained artifacts were freshly rehashed. Reused web files match
unchanged authored source, and staging adds the two current lockstep modules. Both exact 34-file local/HTTP
inventories pass; the served allowlist does not establish which modules loaded.
No native rebuild was needed. The final executable suite ran 2,062 tests:
1,909 executed, 153 skipped, zero failures.

The source comparison uses unchanged 256 MiB / 48,000-record / 60-second fresh
limits, with a 65-second owner deadline and five seconds for cleanup. Historical
clock-2000 limits remain 128 MiB / 24,000 records. Capture retains its 180-second
bound and 30-second cleanup reserve. Attributed browser, comparison and server
process cleanup passes, and the owned lane marker is released. An outer plain
bind check initially encountered TCP `TIME_WAIT`; that receipt remains separate
from the reduction proving no listener, refused connection, reusable bind and
absent attributed processes before marker release.

All failed preparation fixtures remain retained, including the inventory HTTP
response-header error, comparator fixture/guard errors, and metadata helper
record-spelling and recorded-runtime-identity mistakes. The final metadata
validators opened no original content; the subsequent single approved comparison
read only its bounded prefix, and the original trace stat stayed stable.

This is conditional recorded-queue state evidence. It does not establish KO
cause, CSS/SSS scalar-state equality, full-match or whole-session acceptance,
Results/CSS return, live input, foreground timing, pixels, PCM, performance or
competitive readiness. A meaningful next proposal is first-match completion and
its original Results transition, using this prefix as the accepted baseline.
No further scan or capture is authorized by this entry.
