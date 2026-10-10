# Pokémon Stadium diagnostic profile and OnInit boundary

**Compiled / Synthetic controls / Source identified / Native traced / two same-process functional lifetimes passed**

**Current boundary:** the separately reviewed native source CSS→SSS setup observation
on `a29f8d69` records the first-CSS seed and the distinct SSS-selected seed,
all six source player rows, four compatibility rows, and checked zero-tick
retirement. It did not construct a match or reach Ready/GO. A separate native
no-draw canary on `a92352ea` traversed the original CSS→SSS route and observes
the source Stage gate clear and GO in world tick 84, the next complete tick 85,
HUD Ready at tick 124, and checked world/Session close.
Its trace does not emit the typed setup receipt or bind the first-CSS seed
context to match construction, so it cannot be paired with the separate setup
receipt. A separate native functional run on `ee2f3ab9` completed one
neutral-input, no-draw lifetime through exactly 3,500 post-GO world ticks and
checked full close. The later corrected producer `8f785fe5` completes two such
lifetimes sequentially in one Node process, each with PCM processing and full
checked Session release before recreation. All 7,000 step records, 256 current
bindings and 98 fixtures were independently verified. This is functional
recreation evidence; original setup/per-owner RNG comparison and C3 remain open.
The failed pair on `266c9c44` and shorter refusal on `9a1909d1` remain retained.
The ordinary Results regression also passed on the exact `8f785fe5` ordinary
producer after the archive ownership changes, including two original Results
entries and checked Session end. The earlier `ee2f3ab9` pass and initial
missing-`LbRf.dat` preparation failure are retained.
The earlier original failures remain retained. The Items-lock observer on
`1f146dde` passed its canonical original compiler. Its changed capture observes
Items lock 1→0, the existing Up reaching row 31/value 3 and frequency 3→2→1→0,
then live CSS and the verified SSS constructor return. It stops at SSS readiness:
the authored Random index 30 has no stage-kind slice, while the inherited owner
classifier requires one; the strict Stadium validator also rejects index 30.
All 109 inputs remain unchanged; the driver exits 1 naturally and reaps its
terminated Dolphin child (exit 0), with both absent. The streams are incomplete;
no SSS steering/confirmation, VS setup or GO was reached. The three test-only
harness repairs are integrated at `ee159395`; their retained candidate controls
pass 34 tests with one optional skip, and the canonical focused run
also passes 34 tests with one skip (3.697 seconds). The full suite on exact `ee159395` passes 2,374 tests in 520.540 seconds
with 161 skips and zero failures/errors; PID 53748 exits naturally and is absent.
The prior `1f146dde` failure remains retained. This validates the test-only
repair commit, not the proposed reader correction or C3 acceptance.

Both Stadium-only reader corrections were integrated at `5f1faf8b`. Their
45 focused controls passed on source base `704ff6a4` with the exact candidate
files later committed there; this was not a clean-head focused run. The full
suite passed on clean exact `5f1faf8b` with 2,379 tests and 161 skips. At the
later `91fc1fb9` checkpoint, the five-file source-position observer passed 50
focused controls with no skips on clean base `24c15ec2` plus the exact candidate
before commit; the independent source reviews passed. The native observer
compiled successfully with guest memory writes disabled, and the full suite
passed on clean exact `91fc1fb9`: 2,384 tests, 161 skips, zero failures/errors
in 534.888 seconds. The initial suite receipt parser selected a nested test
footer; the corrected receipt binds the final discovery-suite footer.

The actual original-menu capture on `91fc1fb9`, using the unchanged recipe and
caps, still fails the finite SSS policy before VS or GO. The observer remained
valid but incomplete with 2,193 records (last sequence 2,192); its input stream
has 3,890 events and is incomplete. All 140 launch inputs were verified. Sol's
read-only reduction now confirms 74 cursor callbacks: 60 complete same-call
target tuples, 14 cursor-only tuples where the original routine hit an earlier
row, and zero hidden-CAF calls. The retained offline driver reproducer fails at
sequence 2,105 after 11 actions. The original setup/per-owner RNG comparison
and C3 remain open. The two successful 8f functional lifetimes and ordinary
Results regression remain bound to their original producer identities.

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

The smaller zero-tick observation on `93151c83` uses the actual original menu
handoff and match constructor. Two original font-1 text appends precede HUD
startup; the original shared font-2 nametag is the third append, and its
semantic assignment agrees with the append's integer context ID 0. HUD shutdown
returns without a matched text-unlink event. Normal `Session.close` then refuses
**`Partial Stadium OnStart ownership must remain reachable`**, at zero match
ticks. The text-topology guard is not evaluated. These events do not identify
the earlier Ready run's successor or prove current allocation membership.

The diagnostic build and six Python checks pass. The smaller original allocator
control qualifies append and matched-unlink logging across two lifetimes;
matched unlink is observed before frees, so it does not prove completed free.
All 695 direct bindings and 98 fixtures remain unchanged before docs edits.
Independent review verifies those inputs and all 17 raw files. The unittest
passes its observation assertions, while native exit 1 retains the failed
lifecycle; both exact children exit naturally, are reaped and are absent.

The Ready text observation on `2ba901d7` reaches the same original Ready
predicate at 124 ticks and preserves the first `owned-second-next` refusal.
In this run, the successor pointer matches the original HUD nametag append and
semantic assignment; no matched text-unlink event occurs across HUD shutdown.
A separate snapshot of the SIS backing at construction still matches its world,
heap, allocation generation, epoch and requested size at the refusal. A bounded
walk of the current used list finds the exact 160-byte text payload. This is
current allocator membership plus observed event order, without a per-text
allocation generation or proof of the remaining renderer/context ownership.

The diagnostic build, six Python controls, retained zero-tick parser regression
and actual allocator controls pass. The controls cover stale backing identities,
duplicate snapshots, present and removed payloads, and an invalid used-list head;
the payload-size-mismatch reporting branch is not exercised. Independent review
verifies all 754 direct bindings, 98 fixtures and 17 raw result files before docs
changes. The unittest passes observation assertions; native exit 1 still records
failed partial shutdown. Both exact children exit naturally, are reaped and are
absent. The later map-2 guard remains unevaluated.

The scheduled-retirement correction on `34e2da83` retains strict initialization
checks and adds a checked path for a started source session. Successful original
SIS frees permanently record destruction of the two owned texts and one owned
context, so a replacement at the same address cannot restore ownership. The
unchanged original font-1 drain must destroy all three exact owners while
preserving current foreign text/context bytes and order. Foreign renderer and
camera pointers remain opaque; no nametag exemption or runtime global drain is
added.

The first small allocator run fails before its initial positive snapshot. The
observation-only follow-up on `d378d99e` identifies a valid 160-byte retained
text block reused for a 16-byte context request. Original non-head best-fit
allocation does not split that block; this is a test-setup failure, not allocator
corruption. Both failures are retained. After bounded source review, `157430b4`
uses original all-text/context cleanup only between verified-empty test-owned
scenarios and qualifies the intact fixture before injecting faults. Production
checks and allocator behavior remain unchanged.

The corrected actual allocator reducer passes two lifetimes, with owned selective
retirement at all three list positions, unchanged foreign contents/order, and
refusal of stale backing, extra matching objects, changed renderers and cyclic
lists. It also removes and recreates texts and renderers at the exact same cells,
restores the original list order, and still refuses the destroyed identities.
Independent review verifies the raw results and all 32 frozen/live producer
files. The diagnostic build and two strict initialization/Ready component tests
pass; six focused boundary checks passed on `34e2da83` before the observation
and fixture-only corrections. These are component results.

The unchanged original Ready-session test on `157430b4` again reaches Ready at
124 ticks. Its current SIS snapshot succeeds, then the first refusal is
**`map2-graph`**: the initialization guard requires zero map-2 proc dispatches,
while this run records 124 dispatches and zero buffer uses. Later graph predicates
are short-circuited and remain unverified. Selective SIS retirement has not run
in this session; partial close retains world 3 with 29 objects and 53 processes.
All 800 direct bindings, 98 fixtures and 17 raw files were independently verified
before docs edits. Both exact children exit naturally with failure, are reaped,
and are absent.

The failure-only map-2 observation on `b63dfb45` reaches the same Ready boundary
and refuses the unchanged initialization guard. Its bounded snapshot identifies
the exact stage object and rooted original callback, plus the current owned
fallback allocation: 327,680 bytes, world 3, heap 0, allocation generation 3004.
The pointer matches, phase is 0/default 5, the timer is 3,607, no async load is
active, and the archive pointer is null. These are current-state observations;
the allocation was not captured at OnInit, so continuity is still unverified.
The nonzero `xC8` value is retained without inventing an initialization invariant.
Selective SIS drain and complete Session close still have not run past this
refusal. Independent review verifies all 847 bindings, 98 fixtures and 17 raw
files before docs edits. Both exact children exit naturally with failure, are
reaped and absent.

The full-suite checkpoint at `78184927` found three regressions. Two extracted
cold-owner test harnesses lacked the new Ready-context declaration; their
refusal-only test seam now compiles, with four focused tests passing. The SDK
generator control compared retained reusable object/proc cells with a cold
baseline. An explicit two-GObj/one-proc fixture now establishes that cache before
the measured baseline. Both real generator lifetimes retain exact heap return
at 8,331,264 bytes, zero active objects/processes, unchanged backing leases,
freed generator data, private-root restoration and borrower refusal. Separate
cold-pool controls retire and recreate two complete owned worlds. The earlier
cold difference remains retained; no delta allowance or production allocator
change was introduced. Canonical diagnostic build and focused controls pass;
the full suite on `b63dfb45` passes 2,345 tests with 167 skips and no failures.

The checked started-idle path on `ec9a38a9` captures the actual fallback
allocation at original OnInit and preserves its world, heap, size and generation
through final free. Strict initialization remains separate. Pending loads,
transformed states, missing leases and borrowed buffers without a lifetime token
refuse. The actual SDK allocation control passes invalid-state checks, erased
allocation and same-address reuse refusals, unchanged successor bytes/lease and
complete bootstrap shutdown. Its map/GObj/proc graph is synthetic. The first
control failure remains retained; review found an omitted fixture display
identity, which was corrected without changing a runtime predicate.

The unchanged original Ready-session test reaches Ready at 124 ticks, passes
both started display preflights and advances through checked world retirement.
The world, objects and processes are absent and the source-memory tracker is
inactive. The test then fails **`C1 context preflight left Toy aliases past
MenuWorld close`**; the 32 MiB Session arena remains allocated. This is progress
past the map-2 refusal, not successful Session closure. Source inspection
identifies the non-null Toy archive pointer
`_Toy_sbss_804D6ED0`; its address, liveness and first rebinding site remain
unobserved. Both owned children exit naturally with failure, are reaped and absent; all 893 bindings, 98 fixtures and 17 raw files
match before documentation edits. Independent review confirms these results
and the checked retirement control flow.

The canonical diagnostic build and actual focused controls pass at `ec9a38a9`.
Six boundary tests passed on `a46297cf` before the fixture-only correction.
The 2,345-test full-suite pass above belongs to `b63dfb45`; the current source
still needs its full regression run before integration.

The pointer-only diagnostic on `3519a152` narrows that failure. Original menu
reset clears the archive alias before construction. Original `Toy_803124BC`
then performs its lazy archive load during match construction, before the first
source tick. The same numeric pointer is present at Ready, after checked world
retirement and after the preserved-selection check. No original Toy reset event
occurs during match close. This identifies the acquisition function and interval;
it does not establish its caller, ownership or allocation liveness. The ordinary
Ready test still fails the unchanged alias guard and retains the 32 MiB arena.
Independent review verifies all 940 bindings, 98 fixtures and 17 raw files before
docs edits; both exact children exit naturally with failure, are reaped and absent.
The canonical diagnostic build and focused controls pass. Next, identify the
construction caller and original archive/scene reset owner before a correction.
Do not clear unknown aliases or weaken the check.

The checked Toy scene cleanup on `dc88442c` resolves the retained alias
failure. Source inspection follows full-stage construction through
`Ground_801C5878 → tyDisplay_8031C2CC → Toy_803124BC`; the runtime observation
identifies the original loader interval, not a captured call stack. Match close
now uses the original `Toy_803127D4` reset after Ground consumers retire and
before SDK heap shutdown. The preflight requires the current owning trophy
scope/generation, exact source archive and locale roots. It rejects unknown or
foreign aliases without freeing their storage. Archive registry retirement
remains with its existing owner.

The diagnostic build and focused synthetic archive controls pass. The separate
actual Toy controls pass: an exact bracketed constructor refusal; no-acquisition
alias refusals and zero-tick close; a Ready lifetime; and an unarmed OnInit
partial-session destructor with a wrong-locale refusal. The refusals preserve
the checked state; controls restore only their own injected values before normal
close. This is not constructor-exception or armed-cancellation coverage.

The unchanged ordinary Ready test also passes independently. Construction starts
at zero source ticks in world 3; the original HUD Ready predicate is reached at
124 ticks. Checked close observes zero world, objects and processes, an inactive
source-memory tracker and cleared Toy aliases. The preserved-selection check
passes, then complete Session close reports arena identity, generation and bytes
all zero. Independent review verifies 1,000 direct bindings, 98 fixtures and 17 raw
files before further changes; both owned children exit naturally with code zero,
are reaped and absent. The composite Toy controls have their own separately
verified 988-binding result and do not substitute for this ordinary test.

The earlier full regression on `95cf1f5f` passed 2,345 tests with 167 skips.
The cleanup source `dc88442c` separately passes 2,346 tests with 168 skips
and no failures; the exact source remained clean and unchanged. Its ordinary
`runtime` Release build also passes with Stadium admission still disabled.
The subsequent test-only extension below reuses this source Session. The original
GO callback precedes the HUD-ready flag; exact GO alignment remains to be
observed before counting the C3 interval.

The separate one-post-Ready recipe on `880e5bf6` passes. With neutral PAD
and the existing 32 kHz audio-render cadence, source world ticks advance
124 → 125 and gameplay source frames advance 0 → 1. Current world generation
and source-memory context remain unchanged, HUD Ready remains true, and the
match is neither ending nor complete. No draw occurs. The existing checked
close then retires the world, followed by final Session arena identity,
generation and bytes all zero.

This result has its own diagnostic build and six passing focused OnInit controls.
Independent review verifies 1,045 bindings, 98 fixtures and 17 raw files, and
both exact children naturally exit with code zero and are reaped/absent.
The full-suite and ordinary-runtime build result above belongs to `dc88442c`;
those checks have not been rerun on the subsequent two-file test harness
extension. Current-head integration validation remains required for the final
milestone. This is one tick after HUD Ready, not a measured first post-GO tick.
The subsequent source review places GO inside the original Ready scheduler
callback. A diagnostic observer now distinguishes its containing tick from the
immediately following complete tick, with separate HUD callback and gate-clear
bracket observations. It preserves the fixed construction journal and source
callback order.

The first actual observer attempt on `3f26515a` stops immediately after successful
construction at world 3, tick zero: its exact live-display-owner arm check
refuses, before any GO event or source tick. A getter-only diagnostic on
`b8170af4` preserves every predicate and identifies the first rejection as
`map2->proc->child != NULL`. Earlier current-owner, phase, eight-event journal,
fallback allocation, four stage roots and head-process checks pass; later
predicates are unevaluated. Both attempts retain their armed pre-OnStart match
and 32 MiB arena. Their processes naturally exit with failure and are reaped;
that is not checked world or Session retirement.

Both exact diagnostic builds and their six focused controls pass. Root verifies
1,092 and 1,137 bindings respectively, each with 98 fixtures and 17 raw files;
the first failure also has an independent raw-result review. Original source
construction installs `Ground_801C1CD0` at priority 1, then `Ground_801C1D38` at
priority 4, before Stadium adds its priority-4 callback. SDK process insertion
links these through `child`, contradicting the new observer's child-null
assumption. The two child addresses were not captured. The later successful GO canary
resolves the child-null predicate operationally for its completed source route,
but does not identify or preserve those node addresses. The two earlier failed
attempts remain retained at their original scopes; no arbitrary child
acceptance or original callback change was made.

The earlier zero-tick observer refusals remain retained. A later bounded process-chain correction enabled the separate port-side GO canary below; it does not
make an original capture or comparison. C3 requires its own per-tick, per-owner
RNG ledger against the original, in addition to the original setup/state match.
C8's broader transformation comparison does not replace this C3 requirement.

The same [boundary receipt](../evidence/issue251-stadium-profile-oninit-v1.json)
records the source setup, GO canary, earlier failures and remaining limits under
`ready_session_boundary`.

## October 10 UTC: native setup and GO canaries

**Native traced setup observation / Native traced GO canary / C3 not passed**

The separate source-menu run on `a29f8d69` completed original CSS→SSS selection
of Stadium and checked menu/world/session retirement without entering a match.
Its typed setup record distinguishes first-CSS seed `1` from SSS-selected seed
`1425827233`; it retains six source-player rows, four compatibility rows,
Human Mario slots `0/1`, source colors `1/0`, normalized four-stock selection,
and the final 822-byte PAD history wire. The raw, normalized and post-VS rule
payloads remain distinct; the receipt records where they differ. Independent reviewers
checked the frozen result and verified the packet bindings,
fixture inventory, raw trace and producer identities. This is setup provenance,
not a complete replay context or a matched original comparison. Full transient
SaveData bytes, PAD queue slots/qcount and complete CSS-to-SSS input history are
unobserved; unknown callback identities and raw ABI pointer/padding bits are not
normalized or inferred.

A separate native no-draw run on `a92352ea` observes four ordered events:
StageBefore and StageAfter at world tick 84 (gate `1→0`), GO at tick 84, and
HUD enable at tick 123. It records the next full post-GO tick as `85→86`, then
Ready at 124 and checked close with world, objects, processes and final Session
arena all zero. PCM processing stayed enabled. The source-frame value remained
zero across both observed scheduler ticks and is reported as observed; no
frame-ordinal increment is inferred. This is a single neutral-input diagnostic
lifetime, not an original comparison, RNG-equivalence result, or long C3 run.

The typed setup observation and GO canary came from separate runs. The GO run
itself traversed CSS→SSS, but did not emit the full typed setup row or bind the
first-CSS seed context to match construction. The original observer work is also
separate: its compile-only result and first pre-CSS capture failure are recorded
below. Neither is paired setup or original-match evidence.

A separate functional native run on `ee2f3ab9` continued the reviewed GO path
through one neutral-input, no-draw lifetime. The GO-containing world tick was
`84→85`; exactly 3,500 complete ticks followed, from `85→86` through
`3584→3585`. HUD Ready occurred on post-GO ordinal 39 at world tick 124, leaving
3,461 ticks after Ready. The source cursor stayed at 0 through that Ready step,
then advanced `0→3461` one per post-Ready tick. Normal PCM rendering ran once per
tick. The match did not end or complete, and no draw occurred.

Checked close observed world, objects, processes, current object/proc and Toy
aliases at zero, an inactive source-memory tracker, and final Session arena
identity, generation and bytes all zero. Independent review verified 1,238
packet bindings, all 98 fixture files, 17 raw files, 32 producer files plus
their live copies, and all 3,500 step rows. Both owned children exited naturally
with code zero and were reaped. This is one functional lifetime only; no
same-process second lifetime or recreation was observed.

The source notes derive the earliest default-form transformation at post-GO
ordinal 3,602 (`xD8 = 3600 + HSD_Randi(200)`, followed by the source's two-tick
trigger). The run ended at ordinal 3,500, 102 ticks before that source-derived
lower bound. It did not read a live countdown or transformation state. Crowd
events, scratch title/SIS slot, stock-out and standings remain unobserved.

Observer v3 compiled from source `2e354aada` with overlay SHA
`79ee07c6d0350a1759ac917e85307e6a28f3fa8070cd9afcfe25228bedcaf59a`; the
binary SHA is `a583e684e52fd0112cd532e1ddd731d330fde4ed9b882ab2a95002a4cd9d77f0`
and it declares `writes_guest_memory=false`. The integrated driver/source controls
passed 57 focused tests on `fcd0051d`. The first capture attempt stopped before
CSS: while the observer awaited CSS it suppressed ordinary PAD rows, but the
driver needed a menu row to navigate from boot to CSS. The retained input stream
has 28,646 events and is incomplete; the observer trace contains only
handshake/start/end and zero menu boundaries. The retained early-stop record
shows SIGINT sent to the directly owned driver before the 180-second cap; the
driver returned -2 and terminated/reaped its direct Dolphin child (exit 0). All
23 launch inputs remained unchanged. No CSS, SSS, GO, original match or
comparison was observed. This is a preparation failure, not original match
evidence; the subsequent corrected pre-CSS attempt is recorded below.
The original per-owner RNG comparison and all C3 gates remain
open. Ordinary public Stadium admission remains closed.

The corrected pre-CSS observer on `c2d5086f` passed ten focused controls and its
canonical headless compiler build. A changed capture now observes boot PAD/menu
progress, but stops before CSS at `items-frequency-row:observed`. The declared
Up pulse was consumed while the expected row transition was not observed. This
stream does not expose the Items-lock value; source and a separate retained
reducer suggest the lock as the next hypothesis, not an observed fact for this
run. All 33 launch inputs stayed unchanged; the driver and its owned Dolphin
child were reaped. The reviewed `items-frequency-row-lock-proposal-v2` is integrated at
`1f146dde` with 13+6 focused controls and a passing canonical original compiler.
The next changed capture observes Items lock 1→0 and the authored row/frequency
progress, reaches live CSS and the verified SSS constructor return, then fails
at the receiver's SSS readiness predicate. Of 650 SSS polls, 38 precede the
published index and 612 expose Random index 30 with no kind slice. The inherited
Sheik owner classifier returns no owner for this valid shape; the Stadium
validator independently rejects it. The raw stream is not an original setup or
GO result. Its 109 inputs are unchanged; driver 53395 exits 1 naturally and reaps
Dolphin 53396 after termination (exit 0), with both absent. The existing receipt
binds `root-original-stadium-go-failure-1f146dde-v3.json` and
`sol-original-stadium-go-result-sss-reduction-review-v1.json`.

Offline validation also finds an earlier reader gap: raw sequence 2 lacks
SceneKind tag 40 before the first source scene exists. The native observer
authors that absence; 107 such boot polls precede accepted CSS entry. The
correction must accept absent scene observation only before that entry while
retaining the complete original PAD contract and rejecting premature owners or
match boundaries. Post-CSS scene and owner requirements remain strict.

At the `ee159395` checkpoint, the source-only proposal was to reuse the
authored distinction between normal indices 0–29 with a table-row kind and
Random index 30 without one, while retaining exact Stadium index 18/kind 3 and
consumed PAD plus neutral release for confirmation. The 34-test candidate and
canonical focused runs and 2,374-test suite recorded there remain historical;
the `1f146dde` failure remains retained. The reader correction was not yet
integrated at that checkpoint; the later `5f1faf8b` result follows.

At the `5f1faf8b` checkpoint, the 45 focused controls passed with no skips on
source base `704ff6a4` plus the exact candidate files later committed there; the
result is not a clean exact-5f focused run. Separately, the canonical full suite
passed on clean exact `5f1faf8b` with 2,379 tests and 161 skips. That capture
used the earlier native source and retained its distinct 113 SSS polls and 77
SSS PAD-consume records; those boundary counts are not interchangeable. Its
stream and 1,989-sequence replay remain historical evidence.

The later exact `91fc1fb9` source-position observer capture still stops before
VS setup or GO at the same finite index 18/kind 3 qualification. The observer
is valid but incomplete (2,193 records); the input stream is incomplete (3,890
events), with 140 launch inputs verified. Sol's measured reduction found 74
cursor callbacks, including 60 complete same-call target tuples, 14 earlier-row
cursor-only hits, and no hidden-CAF calls. The actual cursor path never entered
the measured target rectangle; decoded host geometry is explanatory only, and
the raw float bits remain in the retained evidence. The existing-driver offline
reproducer retains the refusal at sequence 2,105 after 11 actions. These are
source-observation and replay results, not a successful SSS selection, VS/GO,
input-policy success, or original setup/RNG comparison. The full current
receipt and scoped evidence identities are in the
[issue 251 boundary receipt](../evidence/issue251-stadium-profile-oninit-v1.json).

The next proposal is a bounded driver-only feedback loop using a fresh completed
same-call position tuple and its consumed PAD input, with at most four
same-direction callbacks after a validated earlier-row hit omits the target tuple. Existing movement,
ownership, input-consumption, neutral-stability, stage 18/kind 3, CSS→SSS→VS
and GO guards remain in force. This proposal has not been integrated or
captured. Original setup/per-owner RNG comparison and C3 remain open; ordinary
public Stadium admission remains closed.

The `a29f8d69` full suite passed 2,350 tests with 171 skips and zero failures.
This remains the historical production baseline. The later exact `ee159395`
full-suite PASS above covers the test-only repair; the original prefix gaps and
remaining milestone gates stay open.

These results do not establish C3, original equivalence, rendering, browser
behavior, physical-input, audible-output or timing acceptance.

## October 10 UTC: native source Results-route regression

**Native functional source route / checked Session end / C3 not passed**

The first `results-mario-v1` attempt on `ee2f3ab9` reached CSS→SSS and the
selected Final Destination route, then stopped before Match construction on the
exact error `Missing source match fixture: LbRf.dat`. It made no Results or
elimination observation. The failure and its 171 unchanged input bindings
remain retained. A source-only audit of the complete selected Match and typed
Results recipe identified `LbRf.dat` as required and corrected the recipe
inventory to 124 files. That audit is an input-closure review, not runtime
evidence or a general asset manifest.

The rerun used the corrected 124-file fixture. It completed a No Contest Match
through Results and back to CSS/SSS, then a recreated four-stock Mario/Final
Destination Match through the source GAME ending, Results and CSS. The retained stdout
contains two actual Results-entry PAD records of 822 bytes each; the recreated
Match reached Ready in 124 ticks and observed 114 frozen GAME ticks. All 172
immutable bindings matched before and after. The existing recipe's checked
`gameplay_session_end` succeeded and its success marker was emitted. It does
not emit a numeric post-close arena snapshot; no numeric zero-arena claim is
made. The producer is bound to `ee2f3ab9`; production source
is unchanged from the `a29f8d69` regression baseline.

This is a functional native source-route regression with scripted PAD and a
fixture seed. It does not establish original-game equivalence, rendering,
browser behavior, physical-input, uninterrupted audio, PCM equivalence or
timing. The run did not traverse Prize. It does not pass C3. The independent
result review and exact raw receipts are bound in the
[portable receipt](../evidence/issue251-stadium-profile-oninit-v1.json).


## October 10 UTC: same-process Stadium recreation failure and reduced slot diagnostic

**Native functional first lifetime / second-lifetime preparation failure**

The separately gated pair on `266c9c44` completes all 3,500 post-GO ticks in its
first lifetime and checks world and Session release to zero. The second Session
receives allocation generation 2; reuse of the same arena address is valid.
Its CSS/SSS menu world reaches generation 5 and verifies SIS retirement, then
preparation stops at `mask=64: ordinary grDatFiles slot occupied`, before the
second Match is constructed. No second Ready/GO, completed interval or checked
second Session close is observed. The raw diagnostic does not identify the
occupied slot, value or owner. This original failure and its exact producer remain
retained.

Independent review verifies 1,288 packet bindings, 98 fixtures, 17 raw files and
all first-life step rows. Both owned processes exit naturally with code 1 and
are reaped without cleanup signals. Their exit is not evidence of second Session
teardown.

A shorter reducer on `9a1909d1` sampled all four authored `grDatFiles` rows at
eight lifecycle boundaries. All rows were zero at first context preflight and
immediately before `Stage_802251E8`. Immediately after that original call, slot 0
held `unk0=0xa6b07e8`, `unk4=0xa6512d8`, `unk8=0`; the tuple remained unchanged
through Ready 124, checked Match close, Session close and second preflight in
world generation 5. Slots 1–3 stayed zero. The ordinary table `0x184b80` stayed
unchanged; the native table was `0xa6ae868` during the first world and its
published value became zero after close. The effective lookup then reverted to
the unchanged ordinary table `0x184b80`. The unchanged strict guard still refuses
with mask 64 before the second Match is constructed.

These are observed pointer bits, not proof of pointee liveness, ownership, alias
equivalence or safe retirement. Independent raw review checked 157 immutable
inputs, 98 fixtures, 8 raw files, 32 producer files, 6 generated sources and all
8 snapshots. The native process exited naturally with code 1 and was reaped; no
signal was sent. The reducer establishes where the slot first becomes populated
and that it persists, but not which owner should retire it. Preserve the strict
refusal and original source order; no blanket clear or unchanged pair rerun follows.

The subsequent ownership audit found that the original ordinary preload row must
retire before its checked StageMap archive scope. The diagnostic-only correction
on `8f785fe5` captures all four row tuples and both archive identities, checks the
complete table and downstream close preflight, then calls the original release
routine only for populated owned rows. Foreign or replaced owners and extra
consumers refuse before any release. Host controls and a composed real-SDK
StageMap/registry/original-preload control passed; the latter uses a synthetic
typed catalog and does not prove full E8 timing or allocator address reuse.

The short game reducer then passed: it reached Ready, closed the first Match and
Session, and completed the second strict SSS preflight and close. All four slots
were zero after Match close and at every subsequent sampled boundary. Independent
review verified all nine snapshots, 36 slot rows and 156 unchanged input bindings.
The directly owned Node child exited naturally with code zero and was reaped
without signals. The previous failed producers and observations remain retained.

The subsequent same-process pair on the same `8f785fe5` producer passed.
Session allocation generations 1 and 2 and gameplay world generations 3 and 6
each completed exactly 3,500 post-GO neutral/no-draw ticks with PCM processing.
Both observed GO endpoints were 85, Ready was 124, the interval ended at world
tick 3,585 and the final source frame was 3,461. Those equal timings are observed,
not assumed across lifetimes. Each close reports world, objects, processes,
current context and Toy state zero, followed by Session identity, generation
and bytes all zero. The numeric Session address was equal with a newer
generation; no allocator-address reuse inference is needed.

Independent review verified all 7,000 step rows, 256 current immutable bindings,
98 fixtures and 17 raw files. The directly owned unittest PID 14516 and Node
PID 14522 exited naturally with code zero, were reaped and were absent; no
cleanup signal was sent. The exact tracked pair assertions and child/supervisor
ownership remained unchanged. The evidence receipt binds
`root-stadium-functional-idle-pair-frozen-result-v2.json` and
`sol-stadium-functional-idle-pair-result-review-v2.json`; prior failures remain
under their historical producer identities.

This passes two functional source lifetimes only. Timer/transform state,
screen/title/SIS, stock-out/standings and original setup/per-owner RNG comparison
remain unobserved or unpaired. No draw/PCM equivalence, browser, physical-input,
audible-output, timing or C3 acceptance follows. The 91fc source-position
observer now provides valid, retained SSS cursor/target samples, but the original
route still fails before VS/GO. Sol's final reduction confirms the missed target
path and the existing-driver replay reproduces the failure at sequence 2,105
after 11 actions. The next bounded work is an external driver-feedback proposal
using fresh same-call observations and consumed input; it is not integrated or
captured. The full retained evidence and exact scope are recorded in the
[issue 251 boundary receipt](../evidence/issue251-stadium-profile-oninit-v1.json).
Ordinary public Stadium admission remains closed.

The ordinary Results regression was also repeated on the exact `8f785fe5`
ordinary Release producer with Stadium diagnostics off. It passed both original
No Contest and Mario/FD four-stock elimination Results paths and returned to CSS.
The trace observes Ready at 124 ticks and the GAME ending transition across 114
frozen ticks; stdout retains two typed Results PAD records. The checked Session
end returned success. This recipe does not emit numeric allocation-zero fields,
so no such post-close snapshot is claimed. All 172 pre/post input bindings and
124 fixture files matched; owned Node PID 16325 exited naturally with code zero,
was reaped and was absent without cleanup signals. The unique
`root-ordinary-results-8f785fe5-frozen-result-v1.json` and independent review bind
this functional result. Subsequent `1f146dde` observer/driver edits do not change
its recorded producer identity or establish a new original comparison.
