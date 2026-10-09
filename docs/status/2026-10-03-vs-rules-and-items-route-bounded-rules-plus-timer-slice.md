# VS Rules and Items route — bounded Rules Plus timer slice

The original competitive profile and initialized VS setup now pass the full
predeclared comparison with browser V9. The
[original profile receipt](../evidence/original-competitive-profile-entry-comparison-v1.json)
binds the actual original entry/setup records and the independently replayed
comparison. This covers GameRules, every selectable item switch, saved and
normalized item masks, player/source-port identities, teams, stocks, damage and
colors at their declared boundaries. It does not compare RNG, PAD schedules,
positions, pixels, PCM or the natural terminal. The original eight-minute
terminal experiment remains pending; its separate capture must supply its own
starting-state and terminal observations.

The integrated original diagnostic and test-only observer assertion correction
passed 2,240 tests with 159 skips in 501.325 seconds at `d19fd07d`. The first
integration run at `0751744b` retained one failure: an older test searched the
whole remainder of the observer source after `AddSessionSlices`, so it rejected
the intentional opt-in profile slice. The corrected assertion limits that
exclusion to the normal session helper and separately checks the opt-in and
first-CSS guards; no runtime bytes changed. The fresh canonical original
reference build at `00ef116f` also passed. Those native overlays exactly match
the integrated source; this does not relabel browser V9's producer.

The first original natural-timeout attempt stopped before gameplay on the
SSS retirement guard after 28.085 seconds. The reduced raw boundary is last
active tick 238 followed by the exact retiring route at tick 239, with neutral
input, Final Destination, zero cooldown and the consumed-input count preserved.
The original scene loop increments its cursor after the final on-frame callback.
A receiver-only correction accepts that first final increment and freezes the
actually observed retirement cursor thereafter; active gaps, later drift, changed
input, stage and ownership remain failures. Eleven focused controls pass, and the
retained buffered prefix replays without admitting its interrupted ending as
completion. Native overlay bytes are unchanged. The original natural terminal
comparison remains pending; the failed trace and incomplete MWRI are retained.

The second original attempt passed setup, readiness and one input-driven P1
stock loss, then Dolphin's Metal backend aborted with an IOGPUDeviceShmem
allocation assertion. The retained raw prefix ends at source tick 11,561 and
match frame 11,438; the stale sidecar's last source tick was 11,552. The exact
abort instant and host GPU/memory state were not observed, so no leak or resource root cause
is claimed. The receiver was waiting for another record; an interrupt to its
exact owned process ran the existing cleanup and reaped the aborted native child.
All four owned processes are absent. The raw stream and incomplete MWRI remain
retained, with stale recording sidecars explicitly not treated as success.
No comparison ran on this attempt. The receiver now detects an exited owned
native child while waiting for a missing record; four focused child/wait controls
and 12 legacy-reader controls pass. Complete buffered records remain readable
after child exit. This receiver-only change leaves native overlay bytes unchanged.
A separate Null-renderer profile-entry experiment is being prepared before any
further long capture. It changes only the copied renderer configuration and
cannot establish rendered output equivalence or the Metal failure's cause.

The combined Rules/SD runtime passed the eight-minute functional route in V9.
The [same scoped receipt](../evidence/competitive-rules-profile-preflight-v1.json)
binds producer `87d89c27` and its fresh ordinary Release build: original menus
set four stocks, 8:00, all 31 items off/None, pause off, friendly fire on,
ratio 1.0 and handicap off. Two-Human Mario/FD reached a unique P2 timeout
at source frame 28,800 after one P1 stock loss and neutral play, with live
stocks `[3,4]`. All 361 Results trace rows were retained; source P2 Start
consumption reached phase 4/statistics 2/all confirmations before transfer.
Active CSS retained exact GameRules and Items, then Eject cleared source owners
and assets. The 17 preparation-only incidents had zero dropped incidents;
32,230 thinned samples are explicit. Browser errors/timing pauses were zero.
Exact owned Node/server processes retired and the socket rebound.

The merged SD main `bedc15ec` has the exact reviewed SD candidate tree. The
Rules refresh preserves the exact premerge combined tree; it does not relabel
the V9 build/capture producer. The clean `9fcf7771` combined source passed 2,202 unittest cases with 159
skips in 486.092 seconds. Exact suite/supervisor processes retired, and all
46 served hashes stayed unchanged. Skips include native-host trace controls
whose target is not built in this checkout; separate SD native evidence keeps
its own producer. Exact-head CI and final integration review remain open. Original natural-terminal
comparison, physical input, foreground timing, pixels/PCM, uninterrupted audio
and competitive-set acceptance remain separate gates.

The 2026-10-08 [competitive-profile preflight receipt](../evidence/competitive-rules-profile-preflight-v1.json)
adds **Compiled / Source identified / Browser exercised** evidence for setting
four stocks, 8:00, all 31 item switches off, None frequency, pause off, friendly
fire on, damage ratio 1.0 and handicap off through original menu inputs. The
headless Chrome run returned to CSS and Ejected with source owners and assets
cleared. Source producer `21ac2ff4` used unchanged Release runtime `049f9a5b`;
the receipt binds the full identities, screenshots, retained failure and cleanup.
The raw CSS payload is not the normalized match-start oracle. The earlier
menu-only receipt ends at CSS; subsequent route evidence is recorded separately.

The later 2026-10-08 [normalized match-start prefix](../evidence/competitive-rules-profile-preflight-v1.json) continues through original SSS to a live two-Human Mario match on Final Destination. At source frame 180 it observed the normalized 480-second timer, four stocks each, all item switches off, pause disabled, friendly fire on and damage 1.0, then Ejected with owners cleared. It stops before any stock loss, timeout, Results or Results-to-CSS return. The raw output retains one stale reused check string that says no match ran; the structured observation and final route check show the short prefix. The hash-bound private run receipt is named in the evidence JSON. This is not a full competitive route or original-game comparison.

The first natural eight-minute browser route passed on 2026-10-08. The same
[scoped evidence receipt](../evidence/competitive-rules-profile-preflight-v1.json)
binds harness `1801061d` to ordinary Release runtime `365c6b31` and its 46 served
files. Input produced exactly one P1 stock loss; both ports then remained neutral.
Original MatchEnd reached source frame 28,800 with `OUTCOME_TIMEOUT`, live stocks
`[3,4]`, unchanged four-stock setup and unique terminal winner `[1]`. Original
Results consumed separate P1 presentation/P1 confirmation/P2 confirmation edges;
the P2 row had phase 4, statistics phase 2 and all four confirmation flags set.
All 368 trace attempts were retained without overflow. Active CSS retained the
exact competitive GameRules and item preferences before Eject cleared the owners
and assets. Browser errors and timing pauses were zero; all 17 preparation
incidents were retained, with no invalid or dropped incidents. Node and supervisor
exited 0, Playwright closed, and the exact owned HTTP server was reaped.

This is **Compiled / Source identified / Browser exercised** evidence. It does
not supply a matching original-game comparison. The SD candidate changes post-VS
mode and returned CSS cache ownership; this ordinary `365c6b31` result does not
validate those changes. The later SD ordinary native controls and combined V9 browser route cover
those ownership changes under their distinct producers. The later combined suite passed at `9fcf7771`; current-head CI remains
open, along with physical input, foreground timing, pixel/PCM, audio and performance gates.
The private receipt preserves the historical unavailable original preflight stderr
and first readiness-control failure raw-log gaps explicitly.

The first preflight stopped on an incorrect test expectation for the item mask.
The original DOL's setter instructions confirm that clearing preference bit 31
also clears the upper 32 bits through signed mask widening. The corrected test
preserves that arithmetic and the unmapped preference bit 28; the observed saved
mask is `0000000010000000`. All 31 selectable rows are independently observed
off. This static instruction check establishes setter arithmetic only.

The following October 3 result remains historical evidence for its different
rules, input route and producers:

**Source identified / Native traced / Browser exercised / Compiled** for the
original Main → VS → Rules → Items → Rules Plus route, one item-row toggle,
None item frequency, one-minute stock timer, three-stock Final Destination
browser match, No Contest Results, and retained Rules Plus/CSS navigation. The
[scoped receipt](../evidence/vs-rules-plus-timer-source-route-v1.json) binds
the retail source prefix, rendered headless Chrome report and screenshots,
Release package hashes, callbacks, assets, and focused/full-suite checks. The
retail capture reaches its first match setup, input-driven No Contest Results, scene teardown,
and CSS return; its enclosing three-match request later failed during another
match teardown/recreation path. That failure and the reduced prefix are retained
in the receipt. Retail video used Null rendering, so no retail pixel comparison
is claimed. The older receipt's natural-Results label was corrected: its procedure uses No Contest and
supports original Results/CSS lifecycle, not natural non-tied timeout.

The earlier v2 receipt's item-mask mapping is superseded. Rev. 2 source
observation and the corrected browser route show that toggling Items cursor 0
clears in-memory item preference bit 5 and `StartMeleeData` mask bit 18. The
source menu's authored arrays were assembled into a stable `MnItemSwTable`
because Emscripten object sections do not preserve the adjacency assumed by
retail. The browser match receives the edited mask `fffffffffffbffff`, None
frequency, and enabled 60-second timer, then reaches Results and CSS. Its
screenshots show real rendered output; the Final Destination frame still has
magenta outline artifacts, so visual equivalence remains unclaimed.

This route used a fresh isolated Everything-unlocked context and did not exercise
Personal autosave or source GCI persistence. Item-family spawning, PCM/pixel
equivalence, physical input, and performance remain outside its evidence. Host
speaker output was muted while browser audio processing remained enabled.
