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
checkpoint at input sample 300, source frame 176, after Ready at sample 124/frame 0.
Both fighters are observed grounded before the snapshot. One original scripted
input advances to frame 177. Three restores and one-tick replays exactly match
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

These are raw samples on Apple M4/arm64, Darwin 25.6.0, Node 22.23.2. Capture
includes the native quiescence check and full-memory copy; restore includes
memory/stack restoration. Replay is the checked source step, excluding host
comparison/log copies. There is only one capture and three repeat samples;
no p95, generalized performance pass or combined frame-budget result follows.
Historical Node 24 measurements belong to a different cohort. The copied heap
includes immutable and unrelated data: **128 MiB is not the minimum or complete
mutable simulation-state size**.

Executable source is `d78af98c`. The artifact was built from `2f12d0b9` plus the
exact reviewed patch later committed unchanged; the receipt keeps those
identities separate. The affected Release target, compiler layout assertions,
focused controls, and independent source/result review pass. The subsequent
`92c11c2d` snapshot-runner checkpoint passes the full 2,383-test regression
(165 skips) and all 14 CI checks (12 successes, two expected skips). Later
executable changes require affected validation and final-head CI before
integration. The Wasm and harness/source hashes are recorded in the receipt.

## Shared-page ring and seven source-only ticks

A second experiment reuses the existing shared-page snapshot owner on the same
runtime, Node 22 binary and 34 assets. It starts from the same active grounded
sample 300/frame 176. Three restores replay seven original ticks with exact
observation and PCM agreement per tick and exact whole-memory endpoint agreement.
Eight further retained snapshots cover samples 307–314; every restore check,
RNG-bit corruption/recovery control and release/close invalidation passes.
The source/audio/world closes, all retained pages are released, and its recorded
Node child exits and is absent. This is still a fixture with source drawing off.

| Measurement | Observed value |
| --- | --- |
| Eight-state logical snapshot bytes | 1,073,741,824 (1 GiB) |
| Unique retained page bytes | 80,740,352 (77 MiB) |
| First cycle capture, without previous-snapshot hint | 57.489375 ms |
| First ring capture, without hint | 50.969500 ms |
| Seven later ring captures, with hint | 8.432000 ms, then 4.587167–4.854500 ms |
| Three seven-tick replay costs | 1.766582 / 1.623835 / 1.501042 ms |
| Three cycle restores | 7.240250 / 2.633500 / 2.496917 ms |
| Eight ring restores | 2.486250–2.737875 ms |
| Process peak RSS | 537,051,136 bytes |

These are limited samples, not percentiles. Shared pages reduce retained payload;
restore still copies every 64 KiB page. Hinted captures compare all 2048 pages and
hash 25–33 changed pages in these samples. The initial full-memory page scan and
hash/copy capture is much more expensive than the later hinted samples. The
seven-tick timing sums synchronous source steps and excludes source draw, browser
host state and comparisons/logging. **Do not add unrelated timing samples into a
claimed frame-budget result.** Source draw and combined seven-corrective-plus-current
measurement remain open; the seven replayed fixture ticks are seven total.

Harness commit `cbe64e8e`, execution source `92c11c2d`, and the earlier build
producer are distinct. The receipt retains exact hashes and the two-header
source-inventory caveat. Sample 300 observation/PCM matches the first experiment;
its one-tick and this seven-tick memory endpoints are different states and are
not compared with each other. No successful capture was repeated.

## Remaining measurement boundaries

1. **State size/copy/restore:** whole-heap and shared-page measurements are
   observed. Complete mutable owner ranges and external host-state bytes remain
   unknown; retained page payload is not the minimum simulation-state size.
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

The reviewed source owner inventory is retained in the structured receipt. Native
audio uses a separate host-time accumulator; the AudioWorklet output clock is
independent again. Transferred PCM already consumed by output cannot be rewound.
File requests also cross into JavaScript promise/result maps and asynchronous
completion. HPS decoded slots and SSM bank mappings have distinct owners.
The receipt proposes restore, journal or commit-once policies for each owner;
these are design options, not implemented rollback safety.


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
layout is now compile-checked: match stats 192 bytes, observation 1280 bytes, with
explicit field/PAD offsets. The checker refuses an incompatible layout and
invalid or partial checkpoint/depth selection. The default full-fixture matrix
and its strict original assertions remain unchanged.

## Slippi profile source checkpoint

The [profile inventory](../investigations/SLIPPI_GAMEPLAY_PROFILE_INVENTORY.md)
and [source receipt](../evidence/slippi-gameplay-profile-inventory-v1.json) map
the selected patch configuration, mode behavior and license notices. Ranked and
Unranked Stadium are unfrozen; Direct/Teams offer a selection. UCF 0.84 is selected
in the shared configuration, while live toggle semantics remain unresolved.
File/hash closure is reviewed, but 150 untagged ASM hooks lack bounded semantic
checks. A source/configuration pairing is now identified for release v3.6.4, and the
selected m-ex artifact is traced to matching checked-in bytes. Executable reference
validation, reproducible m-ex generation and reuse rights remain open. No profile
code was ported, and the source inventory task remains open.
