# Match-only rollback feasibility

**Compiled / Source-only snapshot diagnostic / Browser rollback and frame-budget feasibility unproven**

[Milestone #318](https://github.com/ericvanlare/melee-web/issues/318) and
[draft #319](https://github.com/ericvanlare/melee-web/pull/319) implement the
owner's match-only direction. Original menus, transitions, Results and rematch
remain local. The planned mode choice is Vanilla offline-only or Slippi rules
offline/online, with distinct pinned references; the toggle/profile are not yet
implemented. The [structured receipt](../evidence/match-only-rollback-feasibility-v1.json)
binds current producer and retained failure identities.

## First current-source copy/restore result

The existing synchronous source-only Mario/FD fixture passes one selected
checkpoint at input sample300, source frame176, after Ready at sample124/frame0.
Both fighters are observed grounded before the snapshot. One original scripted
input advances to frame177. Three restores and one-tick replays exactly match
the baseline observation, native PCM and entire linear-memory endpoint. A
one-bit restored RNG perturbation is detected by the unchanged negative control.
The source match/audio/world closes; the recorded Node child exits normally and
is independently absent. There is no browser or server in this experiment.

| Measurement | Observed value |
| --- | --- |
| Whole linear memory copied | 134,217,728 bytes (128 MiB) |
| One capture | 14.646459 ms |
| Three restores | 6.953208 / 3.240584 / 3.080333 ms |
| Three one-tick replays | 0.518792 / 0.488333 / 0.729500 ms |
| Forward baseline step | 0.748375 ms |

These are raw samples on Apple M4/arm64, Darwin25.6.0, Node22.23.2. Capture
includes the native quiescence check and full-memory copy; restore includes
memory/stack restoration. Replay is the checked source step, excluding host
comparison/log copies. There is only one capture and three repeat samples;
no p95, generalized performance pass or combined frame-budget result follows.
Historical Node24 measurements belong to a different cohort. The copied heap
includes immutable and unrelated data: **128 MiB is not the minimum or complete
mutable simulation-state size**.

Executable source is `d78af98c`. The artifact was built from `2f12d0b9` plus the
exact reviewed patch later committed unchanged; the receipt keeps those
identities separate. The affected Release target, compiler layout assertions,
three focused controls, and independent source/result review pass. Full
regression and final-head CI remain required before integration of this
milestone. The Wasm and harness/source hashes are recorded in the receipt.

## Remaining measurement boundaries

1. **State size/copy/restore:** the current mechanical whole-heap baseline above
   is observed. Owner-complete mutable ranges, host bytes and the existing
   page-sharing alternative remain to be measured and distinguished.
2. **Source draw and seven corrective ticks:** unmeasured. Existing
   `suppress_draw` skips required game callbacks and cannot answer this gate.
   A terminal diagnostic is being prepared to run the full tick/draw path with
   zero GPU submission, while counting actual resource creation and queue
   writes. No browser restore or resumed frame is authorized by that diagnostic.
   Measure seven corrective ticks plus current separately from seven total,
   including combined capture/restore and final presentation before a budget claim.
3. **Host effects:** source inspection identifies GPU resources/caches/staging,
   depth/profiler callbacks, native and browser audio state, file/stream requests
   and persistent profile/card state. Actual effect counts and rollback-safe
   ownership/commit behavior remain open. Omitting Submit alone does not undo
   earlier queue writes, complete callbacks, or make heap restoration safe.

The recommendation remains pending these answers; this fixture is no evidence
of browser rollback safety or a seven-tick budget pass. Source draw, GPU/JS
ownership, browser input/clocks, externally committed audio, persistent saves,
async/growing memory, Slippi/retail equivalence and human acceptance gates are
excluded. Original arithmetic, PAD, RNG and source ordering remain unchanged.

## Retained preparation failures

The first current target build failed at a C/C++ `GObj_Create` linkage mismatch.
A direct replay of the single failed link preserved the diagnostic. Moving the
source declarations before their transitive C++ include required reusing the
existing HSD compatibility shim; failed intermediate syntax attempts exposed
`ssize_t` and duplicate typedef conflicts and remain retained. Actual Wasm
layout is now compile-checked: match stats192 bytes, observation1280 bytes, with
explicit field/PAD offsets. The checker refuses an incompatible layout and
invalid or partial checkpoint/depth selection. The default full-fixture matrix
and its strict original assertions remain unchanged.

## Slippi profile source checkpoint

The [profile inventory](../investigations/SLIPPI_GAMEPLAY_PROFILE_INVENTORY.md)
and [source receipt](../evidence/slippi-gameplay-profile-inventory-v1.json) map
the selected patch configuration, mode behavior and license notices. Ranked and
Unranked Stadium are unfrozen; Direct/Teams offer a selection. UCF0.84 is selected
in the shared configuration, while live toggle semantics remain unresolved.
File/hash closure is reviewed, but150 untagged ASM hooks lack bounded semantic
checks. Compatible release/configuration and selected m-ex binary provenance/rights
remain open. No profile code was ported, and the source inventory task remains open.
