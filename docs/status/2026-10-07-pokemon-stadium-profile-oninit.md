# Pokémon Stadium diagnostic profile and OnInit boundary

**Compiled / Synthetic controls / Source identified**

**Historical October 7 checkpoint.** The current scoped result is the
[October 9 zero-tick two-world lifecycle](#october-9-two-zero-tick-stadium-world-lifetimes)
below. It supersedes only the earlier statement that no actual OnInit/OnStart
or second-world lifecycle had run; Ready/GO, gameplay ticks, and C3 remain
unrun.

The diagnostic-only Stadium profile and profile-to-`DatNativeMapContract`
adapter compile in the C1 trace target. Focused profile and borrowed-yakumono
controls passed. The prototype parser accepts only the explicitly scoped,
marked diagnostic row; the ordinary manifest still contains its seven existing
stages and excludes Stadium.

The corrected source producer passed the standard suite (**2,049 tests, 133
skipped**) and the default-off ordinary `gameplay_menu_browser` Release build.
The suite included the existing retained C0 map-owner regression and ran both
OnInit source-boundary controls. The separate map-1 constructor test remained
skipped behind its explicit gate. Its 98-file fixture remains present in its
owned local resource location and was not selected or configured by this suite.

Source review found that the later OnInit fixture needs a dedicated owner for
the map-1 display callback's three text-class GObjs and borrowed SIS slot 1.
`melee_web_stage_last_end` removes stage-class Ground objects, cameras and
lights; it does not own these callback-created text/SIS objects. The map-1
Ground removal callback is empty. Therefore this result does not establish a
safe executed OnInit/teardown lifecycle, stage admission, camera setup, OnStart,
scheduled callbacks, rendering, or browser behavior. The cleanup boundary and
its partial-init ordering need review before any actual OnInit attempt.

The [portable validation receipt](../evidence/issue251-stadium-profile-oninit-v1.json)
binds the corrected source/tree, focused C1 producer, suite and ordinary build
logs, cache options, affected object/output hashes, retained failures, and the
source-only cleanup audit. Raw logs and the prior-artifact preservation
manifest remain under external run ID `issue251-oninit-profile-20261007`.


The October 7 profile result above remains historical. The retained V26 OnInit
experiment on `b8c272c4` later failed heap return with **88,576 bytes** outstanding.
After the collision component and reviewed feature integration, the canonical
Release diagnostic build on `21b60e83` passed. One authorized experiment using
the same 98 inputs reached all eight initialization taps, completed actual
StageLast and light destruction, and retired the exact Ground and owned map-2
buffer leases. Its fifth teardown predicate failed: free heap was **33,373,120**
versus **33,459,040**, leaving **85,920 bytes**. The remaining 16 acceptance
predicates were not executed. Both children exited and were reaped naturally;
all 98 input hashes matched afterward. No fixture retry followed.

The reduced queue producer `3bd7cf0f` passed its canonical diagnostic build.
Its actual SDK control enqueued two original Ground callback headers, each
requesting 12 bytes, then executed the explicitly bounded StageLast restoration
assignment fragment. Free heap changed **8,331,328 → 8,331,200** and remained
there after restoration: both exact leases remained live, no callback ran, and
the borrowed GObjs stayed unchanged. Native ASan/UBSan controls executed the
original queue bodies with synthetic service witnesses, verifying stage
OnStart before LIFO callbacks, each header release after its callback, and two
repeat lifetimes. The fragment is not a full StageLast execution, and these
controls do not validate actual Stadium OnStart services. Both preparation
failures remain retained separately.

At that checkpoint C3 remained blocked. The selected progression is required original OnLoad and
OnStart while map owners are live, including checked generator and camera
ownership. Header-only cancellation would cover an early diagnostic abort;
it would not establish that source lifecycle. No runtime cancellation,
generator/camera implementation, full fixture rerun, idle-world loop, or
whole-session acceptance is included. The existing portable receipt records
both historical producers and these exact later build/control identities;
raw evidence remains under run ID `stadium-c3-lifetime-20261009-074216-4d7cf7d1`.


The later Stadium start-owner component on `b016f3c6` adds a separate checked
original OnLoad/OnStart continuation, generator data/scheduler retirement and
return of the borrowed map-1 camera subject to its existing match pool. The
old OnInit caller and its **85,920-byte** failure remain unchanged. The
canonical Release diagnostic builds passed; four native ASan/UBSan controls
passed actual constructor, Camera, owner/selection and tracker bodies with
explicit synthetic services or context construction.

The first asset-free SDK generator control stopped on a mistaken observer
expectation: freed allocation records are erased rather than kept with their
old generation. The consumers were corrected to that established API. The
second control observed the generator data's generation **10 → 0**, with
zero requested bytes and live flag afterward in world 1 / heap 0, then failed
its strict heap/object/process return predicate during the first lifetime.
Its second lifetime did not run, and the compound check did not separately
print those three numerical values. Original GObj/proc release returns cells
to allocator free chains; cache retention is the next source-derived
hypothesis to isolate with a smaller actual allocator control. Neither SDK
failure is waived. Both exact producers and raw failures remain retained.

At the `b016f3c6` checkpoint no full Stage continuation fixture, source proc
tick, idle-world loop or C3 acceptance had run. The next reducer had to distinguish
component retirement from complete owned-world retirement between lifetimes.


The smaller actual SDK GObj/proc reducer on `d89df9bc` completed two owned
world lifetimes. Original construction/release left **160 bytes** in rooted
GObj/proc allocator caches: free heap was **8,331,520 → 8,331,360**, while
objects/processes returned from **1/1 to 0/0** and both allocators reported
used **0**, free **1**, with unchanged exact backing leases. Complete owned
world shutdown made the tracker inactive and cleared the heap/session owners;
fresh original startup reset the caches and restored the exact initial heap.
This distinguishes source cache lifetime from component release. It does not
waive the retained generator same-world failure or the OnInit teardown gap.

Two later zero-tick real-fixture attempts used the same 98 frozen inputs. On
`4e49c485`, the first stopped before original E8/OnInit because the caller tried
to re-export menu selection after MatchContext had acquired the RNG owner.
The production host guard remained intact. Caller-only `3e8bdf8c` now exports
before each match acquisition and after checked release, and observes named
active-match RNG/copied-selection/save witnesses while the match owns source.
Its focused linked-original-RNG sanitizer control and diagnostic build passed.
The second fixture reached all eight original OnInit taps and observed RNG
**1,425,827,233 → 1,024,668,128**, then refused before OnLoad/OnStart at the
pending callback borrower guard. Both failed processes were reaped; all input
and producer bindings were unchanged. Partial stage/world owners were retained
until process exit. Neither attempt completed StageLast or world retirement.

Pinned callback row 2 initializes map2, constructs nested map5, and then
registers its own map2 GObj with `fn_801D13C8`; map1 registers
`fn_801D11E4`. Ground prepends these headers, giving **map2 → map1**. The
`31a91e5b` actual SDK enqueue reducer confirmed two exact 12-byte header
leases and both independent expected object/callback pairs. A distinct map5
witness was refused without header or borrowed-GObj mutation. The original
restoration-fragment control still reproduced **128 bytes** of live root loss
without executing callbacks, Stadium OnStart or full StageLast. Synthetic map
identities and actual source callback pointers establish the reduced pair
contract; they are not a successful real Stage continuation. The guard
correction is approved source; its build and real-fixture validation remain
pending. Ready/GO, scheduler ticks, the second real world
lifetime and C3 acceptance remain unrun.


## October 9: two zero-tick Stadium world lifetimes

**Native traced / zero-tick Stage and MenuWorld lifecycle / C3 not passed**

The source sequence leading to this pass remains scoped. Commit `30798af0`
added the exact started-manager GObj/proc and separate SDK allocation leases
to the checked private Ground map-set; its actual SDK manager/map-set control
passed once, accepting the expected manager and refusing an unknown fifth
object or wrong callback. That control had an incomplete prelaunch
object/archive inventory: JS/WASM were hashed and copied before the run, but a
wrong StageLast object path raised `FileNotFoundError`; the missing
object/archive provenance and complete inventory were recovered after SDK
exit. No rerun or pristine-preparation claim is made. The fourth real fixture
on that source returned original OnStart and checked StageLast/light
destruction, then its caller rejected the Ground lease before MatchContext
end, world shutdown, or a second lifetime. Its stale pre-OnLoad watermark
changed from **3021 to 3026** during construction; the compound check did not
emit every subpredicate, so this remains a retained bounded failure. Commit
`3d032f74` then recorded the post-OnStart, pre-retirement Ground/map2 baseline
and ran a frozen SDK phase control: the unchanged strict helper rejected stale
watermark **10** and accepted the observed post-construction baseline **12**
after Ground end, with negative copies refused. That control was not a full
Stage fixture. The portable receipt binds the producer, result receipts, and
reviews for each step.

The fifth authorized real fixture, produced from `3d032f74` (tree
`3add40ed`), passed two sequential original Stadium stage lifetimes at zero
source/scheduler ticks. Each lifetime reached original OnInit, original
OnLoad/OnStart and the two queued callback drains, then completed checked
StageLast/light retirement, MatchContext end, SIS retirement, and owned-world
shutdown. World generations were **3** and **4**. Both began from the same
fresh heap/object/process baseline; after the first world's retirement, the
new second world matched that baseline. Both world closures were inactive at
retirement and the final owned session closed with zero bytes. No third-world
recreation was observed. The [frozen result receipt](../evidence/issue251-stadium-profile-oninit-v1.json)
binds this result to the independently reviewed 98-file fixture inventory
and 136 input bindings; both sets matched before and after. The 98 files are
the verified fixture inventory, not proof of complete full-match asset
closure. The next match seam must reuse this inventory and fail on any missing
required asset or service. Root independently verified 160 unique bound
files, all 98 fixtures, and 15 trace rows. Result and review identities are
recorded in the portable receipt.

The Stage-only same-world heap observation was **33,451,424 → 33,365,248**
bytes, a retained **86,176** bytes. Whole-world object/process counts and used
GObj/proc pool counts remained **2/2** across the Stage interval while
MatchContext stayed live; the Stage registry and markers were empty after
retirement. This is distinct from the earlier
85,920-byte failed OnInit teardown predicate and from the retained generator
same-world SDK predicate failure. Neither earlier failure is waived or
resolved by this different zero-tick whole-world result. The first retirement
and new second-world initialization restored the fresh baseline; no third
world was observed. The generic E8 subprobe
still reports `stage_objects_started=false` because it describes the E8
request sub-scope; separate `stadium_source_onstart_returned` events and
numeric rows record actual OnStart in both lifetimes.

This is a successful zero-tick Stage/MatchContext/MenuWorld lifecycle, not a
gameplay or C3 pass. It did not construct active fighters/items/HUD/audio
match services, observe Ready/GO, dispatch a source scheduler tick, or reach
the 3,500-tick interval. The retained evidence still excludes C3 acceptance,
rendering, browser behavior, and original-equivalence claims.

The source-to-Ready work has seven explicit next steps under C3's existing
prerequisite groups; these steps do not replace or redefine those groups:

1. Add an exact diagnostic-only prepared-stage seam to the existing
   `GameplayMatchSession`/`GameplayWorld` path. Keep ordinary Stadium content
   and player admission closed; do not add a parallel match driver.
2. Reuse the complete source scene owners and verified 98-file fixture
   inventory as known inputs, not as a claim that full-match closure is
   complete. Require and validate every full-match asset; fail on any missing
   asset or service. Cover rules, save/RNG/PAD, common and fighter data,
   effects, item runtime, refraction, camera/render, audio, and HUD; add no
   success stubs.
3. Use the Stadium collision/map markers and authored spawn matrices/facing
   for the selected two Human Marios; verify source slot, costume, and fighter
   identity before Entry/Ready.
4. Reach exactly one original OnStart at the original intro boundary. The
   direct zero-tick helper must not start it a second time when HUD Ready/GO
   dispatches the source callback.
5. Keep the live Stadium display/screen path valid through the match, including
   canonical IMAGE/preload/fallback, SIS and slot swaps, item consumers,
   scheduled display procedures, and the source camera owner. Check the screen
   countdown, GO, mode, timers, and scratch title along the original path.
6. Observe original Ready/GO, then run at least 3,500 neutral-input, no-draw
   source ticks in each world before the first transformation. Retain a
   per-tick RNG ledger by owner and check source cursor, fighter/map/generator/
   item state, and borrowers; also exercise the required stock-out and
   standings observations without skipping their source path.
7. Complete checked Stage/component retirement and whole-world shutdown,
   require both world closures inactive, verify the recreated world's exact
   fresh baseline, then repeat the full lifecycle in a second world. Record
   final session closure; do not infer a third recreation unless observed.

The receipt records the zero-tick result and these remaining steps.
Earlier OnInit, generator, and fixture failures remain retained with their
original scopes; no source-to-Ready or 3,500-tick runtime attempt has run.
