# Pokémon Stadium diagnostic profile and OnInit boundary

**Compiled / Synthetic controls / Source identified**

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

C3 remains blocked. The selected progression is required original OnLoad and
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

No full Stage continuation fixture, source proc tick, idle-world loop or C3
acceptance has run for this component. The next reducer must distinguish
component retirement from complete owned-world retirement between lifetimes.
