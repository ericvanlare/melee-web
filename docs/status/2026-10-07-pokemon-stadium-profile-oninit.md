# Pokémon Stadium diagnostic profile and OnInit boundary

**Compiled / Synthetic controls / Source identified**

**Historical October 7 checkpoint.** The
[zero-tick two-world lifecycle](#october-9-two-zero-tick-stadium-world-lifetimes)
passed. The latest [source-session preparation attempts](#october-9-source-session-ready-preparation)
now complete match construction and reach the original HUD Ready predicate at
124 source ticks, then fail because the display owner’s second text node has
a non-null successor during teardown. That successor’s ownership remains
unverified. The smaller input, marker, collision and SIS text controls pass;
a complete lifetime and C3 remain unverified.

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

At the zero-tick checkpoint, the source-to-Ready work had seven explicit next steps under C3's existing
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
original scopes. The source-to-Ready attempts below now reach original HUD
readiness and a checked teardown refusal; no 3,500-tick runtime attempt has run.


## October 9 source-session Ready preparation

**Compiled / Synthetic controls / Native traced failure / C3 not passed**

Source `62fd69b8` adds a diagnostic-only route through the existing
`GameplayMatchSession` and `GameplayWorld`. It preserves ordinary stage admission
and brackets the untouched original OnStart dispatch with the exact HUD Ready
object/process identity. Original rules, camera, generator and pending-header
owners remain checked. Root and independent review passed; focused controls and
the canonical diagnostic Release build passed. This implements and compiles the
first of the seven steps above; full composition and the other six remain
runtime-unverified.

The first one-shot stopped before constructing a match. Original SSS leave had
already drained and verified the prior SIS allocator (epoch 3 → 4, world 2,
18,432-byte prior lease). The new caller passed a copy of that verified token to
the deliberately one-use verifier, which correctly refused it. The trace records
`retirement_verified=true`; the refused call's other current root/epoch fields
were not emitted. No Ready-session observation, OnInit, OnLoad, HUD or gameplay
tick was reached. The first producer, three trace rows and failed output remain
retained.

The caller-only correction at `094782e6` consumes that immutable verified token
after checked menu-world shutdown. It neither clears the token nor reads an old
allocation through the retired registry. The actual linked SDK control passes:
original capture → drain → verification, followed by duplicate verification
refusal with unchanged token bytes, epoch, heap owner, roots and allocation
record. Its existing two allocator lifetimes and foreign-owner controls also
pass. The production SIS guard is unchanged.

Preparation for that control first failed because an inventory script expected
the wrong build-result filename. A queued launcher command was also attempted
after that failure; its file was absent, so no SDK process started. The partial
26-file inventory and sequencing error remain retained. Root then completed a
fresh inventory before a separately invoked SDK control. That recovery does not
relabel the failed preparation as successful.

The changed Ready attempt on `094782e6` passed the SIS evidence check, with no
source world active, then failed decoding the captured PAD snapshot:
`PAD snapshot has invalid processing configuration or nonfinite history`.
The raw snapshot and exact rejected subfield were not emitted. No match Session,
VS owner, new world, OnInit/OnLoad, HUD or Ready state was reached. Both attempts
used the same 600-tick cap and retained audio-processing path; neither reached
the tick loop. All 240 first-run and 284 second-run bound files, plus 98 fixtures
for each run, matched after execution. Exact owned children exited naturally and
were reaped; root independently checked their absence.

After those two attempts, the smaller original-menu PAD handoff reducer at
`8e087161` observed the failure precisely. The pre-leave 822-byte snapshot passes
the unchanged strict decoder. The post-leave globals are all zero: eight
processing-configuration constraints fail, while all 96 normalized history
floats remain finite. Original host leave had already saved the valid decoded
input before restoring the caller's external globals. The corrected Ready
caller uses `melee_web_menu_host_input`, as the browser already does, and keeps
that owner alive through deferred construction.

The native reducer completed, but its first Python check incorrectly expected
no SDK world immediately after host leave. MenuWorld still owns that world
until its separate close. This failed test remains retained. The phase-only
correction at `e2ff861b` expects that live menu world, while preserving the
strict zero-after-close and final zero-arena checks. The changed reducer passes:
raw fields agree with strict decoding, the retained input pointer survives
MenuWorld close, the exact seed/owner remains unchanged, and the final session
allocation is zero. Both canonical diagnostic builds pass; the second run's
356 bound files and 98 fixtures match afterward. This is **Native traced** input
handoff and cleanup evidence, with no MatchSession construction.

The next Ready attempt on `e2ff861b` passes the PAD and SIS handoffs and reaches
`before-construction`. Deferred match construction then refuses
`Invalid or duplicate marker binding`. The specific marker was not logged.
The emitted before/after world and tick counts are zero; no OnInit/OnLoad, HUD
or Ready is observed. Source inspection shows SDK startup precedes this marker
guard and the world storage destructor performs cleanup during exception
unwinding. Zero after failure does not establish that no world was acquired
between observations; that interval was not separately logged. All 357 bound files and 98 fixtures remain unchanged. Exact owned
unittest and Node children exit and are reaped naturally. The diagnostic still
reports its 32 MiB session arena at the failure; process exit is not checked
match/session teardown. The next step was a source-led reduction of the
marker binding against the retained Stadium catalog, described below.

Source `8b0973d2` reuses the same-asset C0 marker inventory in an explicit
immutable stage profile: the exact 20 ordered bindings include marker 135 twice
and omit marker 148. Both complete-stage consumers use the existing structural
decoder with that exact contract; ordinary stages retain unique IDs and their
existing required set. Numeric readiness uses the profile's required published
slots. Original camera arithmetic and its dummy-CamRange branch are unchanged.
Root and independent source review passed. The first build command incorrectly
named existing CMake fixtures as CLI trace targets and was rejected before any
compiler ran; the failed preparation is retained. Canonical ordinary
configuration followed by the guarded existing targets, and the canonical
Stadium diagnostic build, both passed.

Four focused SDK checks pass on the frozen producer: synthetic profile and
structural negatives, actual FD readiness and unchanged-state controls, the
retained Stadium C0 marker/map owner, and the complete FD map/restart control.
The FD readiness controls cover missing spawn, camera 148 and blast 152 slots,
nonfinite/inverted ranges, null/wrong owners and repeated readiness, while
preserving lazy matrix evaluation. Copied-profile negatives establish
unregistered-profile refusal, not individual malformed-field branch coverage.
These controls do not establish Stadium numeric readiness.

The changed Ready attempt on `8b0973d2` gets past the marker decoder and logs the
original `use dummy CamRange ...!` branch. It then stops at
`Static floor queries require source left-to-right nonvertical lines`.
The failure observation retains match ownership and world 3, with zero ticks,
17 objects and 17 processes. No Ready/GO is observed. The exact offending line
and source callback phase were not emitted; a source-led
caller and collision-ownership reduction followed. All 420 bound files and 98 fixtures
remain unchanged. The unittest and Node children exit naturally and are reaped;
the retained world is released only by process exit, not checked Session close.
The prior marker failure and all earlier failed producers remain retained.

The retained original Stadium collision data explains the static guard failure:
all 16 rejected directions belong to the dynamic range. Its 24 dynamic lines
also form two authored floor-hint cycles. No static floor fails the direction
check, and all static floor/ceiling seeds terminate. Original static island
construction seeds only the static ranges; the original dynamic updater
classifies moving edges from current endpoints. The shared correction at
`7068afdc` applies direction checks to the static floor range and seeds raw
cycle checks from static floor/ceiling ranges, while retaining full link
traversal across category boundaries. Arithmetic, authored geometry, update
timing, ownership and live-query guards are unchanged.

The first small SDK test failed because its expanded four-edge fixture restored
a temporarily cleared dynamic count to the old value of one. That failed
producer and output remain retained. The test-only correction at `0d3ccd01`
restores the saved count and checks each negative's specific diagnostic. Both
canonical builds and the corrected SDK test pass. The test uses original map
loading, joint binding and dynamic updates to assert all four resolved edge
kinds, live vertex movement, invalid static geometry/cycle refusal, descriptor
preservation and cleanup. It also rejects a static seed that enters a non-root
dynamic cycle. These are synthetic original-SDK controls; their individual
dynamic coordinates are assertions, not printed numeric observations.

The changed Ready attempt on `0d3ccd01` passes the prior static checks, then
refuses `Source dynamic collision joint is not bound to its authored stage JObj`.
World 3 remains retained at zero ticks with 17 objects and 17 processes.
The raw log does not identify the joint or failed guard subpredicate. All 477
bound files and 98 fixtures match after execution; the owned unittest and Node
children exit naturally and are reaped, without signals. There is no Ready/GO
or checked Session close. Original joint binding and callback timing were
reduced before the following changed attempt.

The source reduction reused the existing Stadium plan: joint 0 belongs to
water map 9 and is deliberately removed and unbound during default startup.
The exact failed fifth-run subpredicate remains unlogged. Shared source
`66f917f7` accepts only a descriptor-correct, disabled, unlinked dynamic joint
whose owned lines are all disabled. Existing readiness calls revalidate that
state and distinguish an absent JObj from a valid source callback. Active bound
joints may retain original per-line disables. Descriptor inspection remains
available; executable floor queries validate raw alternate links before the
original getters, then require a finite, enabled, bounded, cycle-free chain.
Original binding, update order, arithmetic and source geometry are unchanged.

Independent review caught the original getters' internal alternate-neighbor
dereference before their return-value check; the correction and focused
post-adoption refusal controls were reviewed before compilation. The first
ordinary build compiled both changed C files but failed linking the original
`mpColl_804D64AC` counter used by `mpLib_80055E9C`. The retained failure is
separate from the corrected build. The one-line target dependency at
`c706f133` reuses the existing fighter-source library and its original counter;
both canonical builds then pass. The actual SDK fixture passes original
remove → deferred adoption → bind/update/enable → movement → remove → retirement,
plus disabled-line inspection, unsafe-query refusals, output preservation and
live descriptor/callback/owner refusal/restoration controls. This is synthetic
SDK evidence, without a real water transformation claim.

The sixth changed Ready attempt on `c706f133` completes construction at zero
ticks, then reaches `first-ready-before-close` at 124 source ticks. Source
inspection ties `ready()` to the original HUD-enabled flag; the subsequent
Ready and two-Mario identity checks pass before close is attempted. The run
retains audio processing and performs no draw. Ready observes world 3 with
59 objects and 73 processes. Close then refuses
`Stadium display teardown refused a mismatched live owner`, retaining world 3
at tick 124 with 29 objects and 53 processes. The exact failed display
subpredicate remains unlogged. All 531 bound files and 98 fixtures matched
before subsequent documentation edits; exact owned children exit and are reaped
naturally, without signals.

The catch prints `pre_OnStart_close_unsupported=1` unconditionally, including
this teardown failure; that field does not establish a pre-OnStart failure
phase. This result observes original HUD readiness, not a checked full Session
close, post-GO idle interval, second lifetime, or original RNG comparison.
The following diagnostic isolates that combined display-owner refusal while
retaining the same full-match setup and prior zero-tick controls.

The first-failure diagnostic at `8aa1e60a` preserves each existing guard,
short-circuit order and refusal. It reports the first failed capture group or
outer owner/data identity without evaluating later checks. Independent source
review caught and corrected two proposed control defects before execution:
a text-pointer fault masked the intended image-group negative, and a new helper
call needed its declaration. Those source drafts remain retained. The canonical
diagnostic build, six existing Python boundary tests and the actual SDK profile,
display-owner, map-2 and source-journal controls pass. New fixture assertions
check successful output clearing, earliest-clause and indexed-group reporting,
null/wrong-current refusal, and unchanged owner/output/service counters.

The seventh Ready attempt on `8aa1e60a` again reaches the original HUD Ready
predicate at 124 source ticks, then stops during checked close. Exactly one
rejection row names **`text-topology`** as the first failed capture group.
It also observes map-2 journal count 1, proc-dispatch count 124 and buffer-use
count 0. The later map-2 guard was not evaluated; those counters do not explain
this first refusal. The exact text node or list subpredicate remains unobserved.
Partial teardown retains world 3 at tick 124 with 29 objects and 53 processes.
All 583 direct bindings and 98 fixtures match before documentation changes;
the exact unittest and Node children exit naturally, are reaped and are absent.
Independent review also verifies the build, focused controls and raw records.

After the sixth and seventh attempts, the smaller original SIS text-lifecycle
reducer on `6eaf092f` passed two allocator lifetimes. It verifies the backing
heap lease separately from each text's SIS suballocation membership. Original
text creation appends a third node and triggers the unchanged
`owned-second-next` refusal; original removal of that tail restores the check.
Removing a captured baseline node triggers `baseline-head` before the retired
node can be dereferenced. Text and used-allocation roots clear at cleanup.
The canonical diagnostic build and six Python boundary controls also pass;
independent review confirms the retained results. This asset-free control uses
context -1, without camera, renderer or full HUD behavior.

The corrected source-order review also establishes that `Session.close` calls
HUD teardown before stage retirement. Therefore close-time HUD text mutations
cannot be ruled out merely from the later `World.close` order. The earlier
contrary inference is explicitly withdrawn in the retained review note.

The eighth Ready attempt on the same source again reaches the original Ready
predicate at 124 ticks. The exact first rejection is now
**`owned-second-next`, index 1**: the evaluated successor is `0x3b604fc`, where
the guard requires NULL. The diagnostic explicitly reports that operand's
lifetime as unverified. It does not dereference the unexpected successor or
establish who created it. The later map-2 guard remains unevaluated. Partial
close again retains world 3 with 29 objects and 53 processes. All 636 direct
bindings and 98 fixtures match before documentation changes; both exact child
processes exit naturally, are reaped and are absent. The preparation script's
initial review-schema lookup error is retained separately; no packet, output
directory or native run had been created before it was corrected.

Next, reduce this successor's ownership against the original SIS callers and
Session close order before changing the owner contract or running another full
Ready attempt. No text-list exemption or completed Session retirement is claimed.

The existing RNG observer can be reused afterward, but the Stadium notes require
C3's per-tick, per-owner ledger to be compared with the original. C8's broader
transformation comparison does not defer that C3 requirement; the roadmap now
makes this explicit. Original HUD readiness is now observed; complete retirement
and the remaining C3 steps stay unverified. No C3, original equivalence,
rendering, browser, physical-input, audible-output or timing acceptance follows
from these checks.

The same [boundary receipt](../evidence/issue251-stadium-profile-oninit-v1.json)
records the exact source, build, failed attempts, reducer and retained preparation
error under `ready_session_boundary`.
