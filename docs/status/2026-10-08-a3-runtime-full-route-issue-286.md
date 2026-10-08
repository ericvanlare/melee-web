# Issue #286: runtime-owned full route through CSS return

**Source identified / Compiled / Browser exercised**

[Issue #286](https://github.com/ericvanlare/melee-web/issues/286) has one
independently reviewed same-host headless browser run through the supported
original route: CSS, SSS, four-stock Mario on Final Destination, Results, and
return to CSS. The [scoped receipt](../evidence/a3-browser-native-full-route-v2.json)
binds the run, retained failure, current Release producer, harness, recipe,
checksum streams, source accounting, and cleanup review. Producer commit
`403ea51b76cbe97115b12039e0d374fd6963bf64` (tree
`a6fe302bd6da163322e0ebc2d31c74d1982ac491`) contains current main
`26ff73a3236e0c72884fe44a92d54678e52fad08`; a later documentation-only commit
does not change the producer identity.

Two runtime-owned peers used local Room Worker signaling and WebRTC. Each
consumed 5,082 recipe inputs through a saved synthetic standard Gamepad profile
and the ordinary controller manager; all 5,084 native-consumed input
components matched the declared recipe witness. The profile kept digital L/R
clicks independent from analog trigger axes. Each peer reached source cursor
5,084, observed source scenes `[1, 2, 3, 4, 1]` and collapsed native phases
`[1, 3, 7, 8, 1]`, and recorded 5,084 checksums of 64 bytes. The streams were
identical (SHA-256
`4ac68c125ef7de127db9ab6c56e3fd40665bd42f02bff7ad8a75519b461d82a0`), with
input ACK 5,081, checksum ACK 5,083 and no native-local divergences or checksum
mismatches. A read-only no-RPC interval completed with 1,528 health
observations while both native cursors progressed.

The first active-match witness was source cursor 432, phase 7: two human Mario
players, four stocks, Final Destination. Both peers retained the original
completed-match result `outcome=2`, `winners=[1]`. At the final CSS boundary,
both had a stable source cursor of 5,084 and phase 1, with structured native
render readiness before the PNG capture. Source accounting was frozen with
5,084 source steps and draws per peer, no overflow, and no errors. These
screenshots establish the declared final-boundary readiness only; no pixel
equivalence claim is made.

Beta recorded one native network-wait episode at source tick 2,195 (four
callbacks, one wait resume). Neither peer recorded a timing-pause diagnostic
or timing resume. The empty run-level `wait_observations` list is a harness
probe result and does not erase beta's native wait. Both peers closed normally
with verified native quiescence; the local Worker disposed and exited cleanly.
The supervised process groups were absent afterward and the capture ports were
reusable.

The AP4 failure remains preserved as a separate result. Its no-RPC observer
failed to complete: alpha was at cursor 2,864 in `network_wait`, while beta was
at cursor 2,862 in phase 7 with the native timing-disruption pause message.
Both browser unload attempts then reported the runtime input fixture owner
failure, although browser closure and the separately checked OS cleanup
succeeded. No checksum export or route acceptance completed in that attempt;
AP6 success does not relabel it.

The AP6 full suite passed 2,109 tests with 154 skipped and no failures; the
ordinary Release runtime build passed with all ten diagnostic/profile flags
off. The independent functional review freshly verified the 103-row frozen
source/artifact closure and the raw checksum and accounting evidence.

This is conditional local functional and source-boundary evidence only. It does
not establish physical input, remote-machine or Internet play, foreground
timing, sustained performance, pixel equivalence, PCM or uninterrupted-audio
fidelity, full A3 acceptance, or whole-session/competitive-set accuracy. The
[roadmap](../ROADMAP.md#current-priorities) and
[accuracy contract](../ACCURACY_CONTRACT.md) keep those gates separate.
