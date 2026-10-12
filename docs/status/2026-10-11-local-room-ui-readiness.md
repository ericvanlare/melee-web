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


## Full-route extension — local functional pass

The follow-on local keyboard route **passed once** in attempt 5. Its
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

The fifth attempt used the reviewed observer join on clean execution source
`d67886769f24ad020ebf3e9de0bf2c3d0d1c3026`, with the same runtime, source game
and deadline. Both browsers followed original CSS → SSS → four-stock Mario/FD
→ original Results → CSS in the retained functional witness. Four natural P1
stock losses, three grounded respawns and the original P2 win (`[0,4]`, outcome
2, winner source port 1) were observed. The original Results trace retained
329 rows, including each Human port’s Start confirmation and source exit.
The 32 keyboard actions passed their literal native PAD-record checks.
P2 stayed neutral during gameplay; this does not establish combat coverage.

Independent byte comparison verified all 2,950 frozen 64-byte checksum records, ticks
0–2,949, byte for byte. Their scene ranges cover CSS (0–211), SSS (212–347),
match (348–2,596), Results (2,597–2,925) and returned CSS (2,926–2,949).
The required return-plus-input-delay boundary 2,945 is covered. Reported
unpaired tails of two alpha and three beta records remain separately scoped.
Input delay was two ticks; each peer recorded eight network-wait callbacks
in eight episodes. This local observation makes no Internet-latency claim.

Attempt 5’s owner finished in 68,264 ms; all 22 owned process IDs were absent
in fresh independent checks, with 16 CDP-attributed browser processes.
Each owner stayed within the five-minute wall budget and reserved cleanup time.
All five cleanups passed normal UI Leave, native quiescence, unload to stopped
phase zero, Worker disposal without fallback, and owned-process absence.
Failed profiles and reports remain retained. The observed Final Destination
geometry still has the known magenta rendering defect; no visual acceptance is
claimed. The complete local route is functional evidence only. It does not establish
retail equivalence, reliable sustained performance or broader acceptance.

The full regression suite on `d67886769f24ad020ebf3e9de0bf2c3d0d1c3026` passed
2,309 tests with 166 skips and no failures or errors. Focused observation and
wait controls, syntax checks and independent source/evidence review pass. The
Release runtime was built from `b5797986`; this branch changes only harnesses,
tests and documentation, and all 46 unchanged runtime hashes were reverified.
Final metadata edits were documentation-only. [PR #303](https://github.com/ericvanlare/melee-web/pull/303)
merged as `2a73717682372d440dbad16ef4bcb418e31a948d` after all 14 checks
on `cafec4554877c70439ba05b9945594e87b350030` completed: 12 successful and
two expected skips. The merged tree exactly matches the reviewed tree.

The bounded provider follow-up made zero provider requests because existing
credentials were missing or expired; it did not refresh authentication. The
earlier scoped provider receipt remains the only service evidence. Public
staging, physical input and the other acceptance gates remain open.

The next public-endpoint adapter is a [source-only checkpoint](../handoffs/2026-10-11-public-relay-keyboard-checkpoint.md).
Its focused refusal checks do not establish a deployed Worker or public browser run.


## P2 gameplay input extension — local functional checkpoint

The `codex/public-relay-keyboard` branch adds one short P2 gameplay keyboard
pulse before the same full route. The branch was later merged with current main
for integration; this capture remains bound to execution source
`9c41b9145ba7b566e68ac450ec65a9d08b2079a1` and uses the unchanged `b5797986`
Release runtime above; all 46 artifact hashes were reverified. The
[P2 extension receipt](../evidence/local-room-full-route-v1.json) keeps this
producer separate from #303 and records the complete input window.

At paired source cursor 469 both peers observed P1 at X −60 and P2 at X 60,
with four stocks each. The second browser's actual ArrowLeft pulse produced
literal port-1 PAD records at input ticks 470–472, followed by all-zero neutral
at 473. All four corresponding P1 port-0 records were neutral. Both peers
crossed release-plus-delay boundary 476; the fresh paired observation at cursor
491 showed P2 at X 41.159996, P1 still at −60, and stocks still `[4,4]`.
This establishes one short movement-and-release interval, not combat coverage.

The unchanged route then completed four natural P1 losses, three grounded
respawns, the original P2 win, both Human Results confirmations and CSS return.
All 2,938 frozen 64-byte records match at ticks 0–2,937, covering all five
scene intervals and the required exclusive return boundary 2,938. No paired
record at tick 2,938 is claimed. Reported unpaired tails
remain two alpha and three beta records. Input delay was two ticks; observed
network-wait episodes were six and eight respectively, without an RTT claim.

The owner completed in 64,436 ms. Normal Leave, native unload to stopped phase
zero, Worker disposal without fallback and all 21 owned process IDs' absence
passed, including 16 CDP-attributed browser processes. Successful profiles were
removed. The known magenta FD geometry remains visible; pixels and audio
fidelity were not compared. Independent raw-result review passed.

Four focused checks pass. The first root test invocation used an unsupported
module-import path; canonical discovery passed without source changes. An
initial private audit miscounted the tail object's keys; corrected metadata
uses its actual counts, and the initial audit remains retained. After current
main was merged, the integrated source at `f5b949d7` passed full unittest
discovery: 2,321 tests, 166 skips, no failures or errors. All three A3-focused
modules ran and passed; none was skipped. The skip count reflects optional
assets, DOL inputs and native/reference trace targets unavailable in that
checkout. The [integration validation receipt](../evidence/local-room-p2-integration-validation-v1.json)
binds this suite and focused results to that source. No `src/` or `web/`
compiled-runtime inputs changed from the `b5797986` producer, so no new runtime
build or capture is claimed. Current-head CI is a separate integration gate;
this receipt records local validation before PR publication.

This checkpoint stays in the existing #288 milestone branch. The supplied
external-preflight interface validates a provided receipt structurally; it does
not verify a deployed Worker. No public endpoint, remote run or RTT evidence is
available, and broader #288 acceptance remains open.
