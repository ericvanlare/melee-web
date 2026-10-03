# Reference Capture roster unlocks

Reference Capture provisioning now defaults to all roster characters unlocked.
Existing installations can select the new immutable private fixture with
`--unlock-characters`, preserving the original save, configuration history,
recordings, Dolphin binary, and physical-controller profile. The pinned GCI's
character mask changes from `0x0024` to `0x07ff`; stage progress is unchanged.
The encoded result matches the original HSD routines and passes the original
C decoder. The installed 0.2.3 verifier accepts the new fixture, and an ordinary
retail boot reaches the title/attract screen without a card error. A visual CSS
check remains unverified because automated keyboard input did not reach Dolphin.
Focused save/provisioning tests pass. With the pinned dependencies prepared,
the complete 904-test suite passes (54 optional skips). The earlier attempt
with missing dependencies remains preserved. This is save/provisioning evidence,
not new gameplay or port acceptance.
See the [upgrade procedure](../REFERENCE_CAPTURE_APP.md).

The physical Falco/Marth/Yoshi replay now matches every declared fighter field
across all 9,019 visible-browser ticks except 17 CPU-related input samples.
Four original fused operations in `HSD_MtxSRT` remove the tick-473 position
error; the first remaining core difference is CPU input at tick 901. Passive
original probes establish its RNG-seed and fighter-pointer register producers,
without using captured addresses as inputs or changing CPU integration. The
1,199-tick 2P and 4,346-tick 3P browser core/CPU regressions remain exact.
Camera, later subject rounding, draw scheduling and headless completion failures
remain open. See the [causal audit and scoped evidence](../investigations/PHYSICAL_REPLAY_SRT.md).
The installed capture app, PR #16, PR #20 and public deployment are unchanged.

Reference Capture 0.2.3 adds startup cancellation and interrupted replay cleanup,
CPU-sidecar binding to its core timeline and derived artifact, and exact Dolphin
source-composition checks with atomic, repeatable provenance publication. The
existing 9,019-tick physical browser trace passes the stronger binding checks and
preserves its known CPU-input, camera, subject, and draw differences. This is
validation-tool hardening; it does not fix those gameplay differences.

Reference Capture 0.2.2 disables Dolphin’s stop-confirmation dialog for each
capture/replay process. The installed app passes an ordinary boot/Stop Capture
check: Dolphin exits without a dialog and the interrupted bundle remains
incomplete. The saved profile and prior recordings remain unchanged. See the
[application behavior](../REFERENCE_CAPTURE_APP.md).

Reference Capture 0.2.1 imports the pinned private prepared save into Application
Support and rejects external fixture locations before reading them. The original
fixture and existing capture bundles remain unchanged. Guarded environment
verification makes zero Documents-access attempts. One new operator-attested
four-stock Falco versus level-6 CPU Marth match on Yoshi's Story replays through
the preserved 0.2.0 tools after relocation: all 47,658 semantic events, 9,019
source ticks and 9,011 draws match through result and teardown. This is an
original-versus-original diagnostic pair, not port or performance acceptance.
Older recordings require their matching tooling identity; a changed capture
build is now rejected before launch. See the [bounded receipt](../evidence/reference-documents-migration-v1.json).

Reference Capture 0.2.0 adds original Dolphin controller recording and replay
from ordinary boot. A controller-driven four-stock Mario/level-1 Fox match on
Final Destination records 61,838 SI operations and 66,231 observer events;
two independent original replays match every declared semantic event, including
1,639 gameplay ticks/draws and result/teardown. Its human-only recipe completes
in native and visible browser execution with matching core state, RNG and CPU
decisions. Camera, subject rounding and headless magnifier/draw differences
remain explicit. All 886 tests pass (37 optional skips). This is bounded
development evidence, without physical replay, performance or gold admission.
Earlier captures lack the SI stream and require a new recording for faithful
Dolphin replay. See the [bounded receipts](../evidence/reference-dolphin-replay-v1.json)
and [input-stream contract](../../reference-capture/dolphin/INPUT_STREAM.md).

The private [Reference Capture application](../REFERENCE_CAPTURE_APP.md)
accepted its first operator-attested physical-controller session: four-stock
Falco versus level-5 Marth on Yoshi's Story, with 35,519 ordered events and
6,492 source ticks through result publication and teardown. Replaying only the
human inputs in the visible browser completes all ticks with the same winner.
The first core/CPU-output difference is index 5,057; RNG, PAD state, match timing,
HUD and magnifier observations agree throughout. Camera, subject rounding and
five extra draws remain separate failures. The headless native prefix stops
after 5,704 ticks and first differs at index 1,482 because it omits the drawn
magnifier dependency. This single capture does not establish repeatability:
the bundle lacks a demonstrated Dolphin boot/menu replay route. Operator-reported
choppiness remains open. All 867 repository tests pass with 37 optional skips;
see the [bounded receipt](../evidence/reference-operator-session-v1.json).
No gameplay fix or gold admission is claimed. PR #16 and its allocation evidence
remain unchanged and unmerged.
