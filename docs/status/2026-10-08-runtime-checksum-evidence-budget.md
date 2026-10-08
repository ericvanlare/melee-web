# Runtime checksum evidence budget

**Compiled**

Scoped component controls.

Issue #279 removes the cumulative 512-record retention barrier for explicitly
bounded development sessions. The [portable receipt](../evidence/2026-10-08-runtime-checksum-evidence-budget.json)
binds the component controls and ordinary Release to source producer
`1792f5bf512d57fc1be8436d4087fb8320062ac3`, based on merged main
`724cbb1c7220a10921521aae58e21a958c401c56`. Documentation commits are separate
from that producer.

The owner keeps its default 512-record budget. An explicit
`checksumEvidenceRecords` must equal the declared source tick count, within the
existing authored maximum of 216,000 source ticks. The development caller binds
that budget explicitly and rejects excessive bounds before identity fetch,
reservation, adapter allocation or native entry. The peer's per-batch capacity,
canonical 64-byte record checks, protocol and wire format remain unchanged.

The retained default control reaches source tick 513, preserves 512 owner
records and refuses the next record with the original retention error. An
explicit 520-record control uses the actual runtime owner, live capture callback,
browser peer and paired protocol cores with synthetic native records and queued
endpoint delivery. It consumes and compares all 520 ordered records, reaches
checksum ACK 519 and input ACK 517, retains immutable snapshots and verifies
normal-close quiescence in that fixture. Malformed records, conflicting duplicate
checksums, out-of-range source ticks and cumulative overflow retain their failure
guards and first causes. Two preparation assertion failures remain retained.

Current-main regression validation passed 2,075 tests with 153 skips and no
failures; ordinary Release also passed with all ten optional runtime flags off.
Forty build artifacts were staged and hashed, and all eight runtime module
bodies match their source files. Owned suite/build processes and process groups
exited, and the external temporary directory is empty.

This is component acceptance of a bounded evidence policy. There is no new
browser run or original-game trace at 520 ticks. The earlier
[nonneutral eight-tick CSS capture](2026-10-08-runtime-owned-nonneutral-css.md)
retains its original producer, measurements and limitations. Sustained runtime
sessions, the full CSS → SSS → four-stock Mario/Final Destination → Results → CSS
route, public player admission, physical input, foreground timing, performance,
two-machine Internet play, original pixels and PCM equivalence remain separate
gates. Whole A3 acceptance remains open.
