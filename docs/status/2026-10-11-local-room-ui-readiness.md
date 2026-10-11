# Local room UI and keyboard readiness

**Source identified / Compiled / Browser exercised**

The near-term readiness boundary in [#288](https://github.com/ericvanlare/melee-web/issues/288)
passed one same-machine, headless Chrome run through the actual development
Host, Copy, Join and Stop / leave controls. The harness transferred the exact
Host-generated invite into Join; OS clipboard contents were not asserted.
Two independent profiles imported the same owned disc and entered original CSS.
P1 used the first browser's D key;
P2 used the second browser's right arrow. Each input moved its source cursor in
both browsers. The runtime owned input capture, transport and checksum progress;
the harness did not inject PAD/Gamepad samples or pump either peer.

The [portable receipt](../evidence/local-room-ui-readiness-v1.json) binds the
raw report, screenshots, process supervision and producer inventory. The Release
runtime producer is `989e6dbc33b54eaefa3cc73af9064449b85bf716`; the clean harness
execution source is `6cb92c1625cad31622b0e27bafd36d97a4aa7ec3`, whose only later
changes are the harness and planning documentation. Both include main
`5f274ed3fc6743937c8ec2ae66e23c1db4094c4e`. Both profiles loaded identical Wasm
bytes, with SHA-256
`23849a4e09d38a3f746cb3f24e124f34ea6404a7558469c70250571c4fc5ef9a`.

The exact common prefix contains 196 matching native records of 64 bytes at
source ticks 0–195, covering nonneutral input ticks 132 and 168 plus the authored
two-tick input delay. Each peer observed the same cursor displacement for each
player. The snapshots retained 200 and 198 records respectively; the four and
two records beyond the common compared prefix remain explicitly unpaired.
Each native runtime recorded one network-wait episode and one resume. This is
neither a claim of uninterrupted progress nor a timing measurement.

Both UI Leave operations closed normally. Native quiescence was verified,
unload reached stopped phase zero, both Chrome contexts closed, and the pinned
local Worker disposed without fallback. Process supervision attributed the
Chrome processes through CDP and observed all owned browser, capture and server
process groups absent. Successful browser profiles and the owned Worker scratch
were removed; reports and screenshots remain retained.

The run used an Apple M4 Mac mini, macOS 26.6 (25G72), Chrome 154.0.8037.99,
Playwright 1.62.1 and Node 22.23.2. Both canvases were isolated 640×480 surfaces
with WebGPU adapters. Speakers were muted through the shared launch policy;
audio processing remained active. Screenshots show the original CSS, but were
not compared against retail pixels.

The Release build and 79 focused room/owner tests passed. The final unfiltered
suite on `0ec7088cfee6fa2f955e6d33b9cb48e5238c40be` completed 2,307 tests
with 166 skips and no failures. The first full-suite run had one status-link
failure because this receipt had not yet been written; that failed log is retained.
The receipt was created, the four status checks passed, and the full suite was
rerun successfully before integration. The final metadata update changes only
documentation.

This is **local functional readiness**, not accepted Internet multiplayer. The
Worker ran locally; no deployed staging endpoint was exercised. SSS, the
four-stock match, Results and CSS return were outside this run. Physical input,
two-machine/Internet play, foreground timing, sustained performance, pixel/PCM
equivalence, audible output and whole-session acceptance remain open. The
broader #288 playable-slice criteria remain unchanged.


## Full-route extension — in progress

The follow-on local keyboard route is **not yet complete**. Its
[separate producer and attempt receipt](../evidence/local-room-full-route-v1.json)
preserves the readiness result above and binds a fresh Release runtime built
from merged `b57979868339faf07394f614320e59b0e7fe9b5d`. The fresh Wasm SHA-256
is `3c475521da3b101a17c384d90659ec2ee00ae1181cabeccf2f6d04abc71b7409`;
all 46 runtime file hashes matched before each attempt. Only the keyboard
harness and its tests changed afterward; the runtime and source game did not.

The first three retained attempts used actual Host/Join and keyboard input to enter
original CSS, SSS and four-stock Mario/Final Destination. P1 used ordinary
outward movement and P2 stayed neutral during gameplay. Their outcomes differ:

- Attempt 1 recorded three natural P1 stock losses and grounded respawns, then
  the driver rejected a stopped state. Its native message was not retained, so
  the cause remains unknown. The exact common checksum prefix contains 4,903
  records; alpha has no unpaired tail and beta has two.
- Attempt 2 retained the exact refusal: beta was in normal first-use rendering
  preparation at source tick 1,816 and match frame 1,349 with stocks `[3,4]`.
  The driver had treated every stopped match state as terminal preparation.
  The exact common prefix contains 1,817 records, with alpha one and beta zero
  unpaired records. A smaller offline reproducer and focused regression now
  distinguish that exact healthy wait from timing pauses, faults and invalid
  terminal states. The initial test-only variable typo is retained separately.
- Attempt 3 passed that reviewed driver change but exhausted the operation
  budget during a checksum wait after two recorded losses and respawns. Its
  final wait rows were not retained. Cleanup snapshots had already compared
  beyond the requested checksum tick, so neither desync nor the actual blocked
  predicate is established. All 8,220 common 64-byte records match; alpha has
  no unpaired tail and beta has two. A focused diagnostic change now retains
  the exact wait predicate rows and separately labelled post-deadline reads,
  without changing its checks or budget.
- Attempt 4 reached original stage select in both browsers, but the driver
  required the live cursor reads to coincide in a single concurrent poll. The retained 32-poll suffix shows both
  peers progressing with one-to-two-tick offsets; the full 4,929-poll stream
  was not retained. The operation budget expired. All 13,274 common 64-byte records match, with
  no alpha tail and one unpaired beta record. This establishes a driver
  observation-liveness failure. The smaller reducer confirms no exact-key overlap in that suffix. A
  reviewed observer change starts each acquisition fresh and rereads the
  lagging peer until actual rows share an identical source cursor. Focused
  controls pass; synthetic catch-up samples are labelled separately from the
  retained observations. It does not interpolate, relabel or compare across
  ticks, and does not guarantee that a bounded live acquisition will overlap.

Each owner stayed within the five-minute wall budget and reserved cleanup time.
All four cleanups passed normal UI Leave, native quiescence, unload to stopped
phase zero, Worker disposal without fallback, and owned-process absence.
Failed profiles and reports remain retained. The observed Final Destination
geometry still has the known magenta rendering defect; no visual acceptance is
claimed. Fourth stock loss, Results, CSS return and the complete route remain
unobserved in this new keyboard harness.

The bounded provider follow-up made zero provider requests because existing
credentials were missing or expired; it did not refresh authentication. The
earlier scoped provider receipt remains the only service evidence. Public
staging, physical input and the other acceptance gates remain open.
