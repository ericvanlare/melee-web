# Public runtime

The user-approved renderer startup fix is now live at
[webmelee.gg](https://webmelee.gg/). PR #18 merged with green exact-head CI;
the production artifact and apex each passed HTTP verification and ten real
browser UI checks. The runtime bytes match the accepted preview. This is a
functional alpha release, without a startup-speedup or expanded gameplay
acceptance claim. See the [production record](../history/RENDERER_PRODUCTION_PROMOTION.md).

A separate [loading-feedback preview](../history/PUBLIC_LOADING_FEEDBACK.md) adds
visible startup phases and bounded file-transfer batches. One local cold/warm
Mario/FD lifecycle pair passes with zero unexpected pipelines, 7.335/8.960 ms
native maxima and 24.010/27.305 ms browser gaps. Disc preparation measured
2.149/2.010 seconds; this does not establish a cold-driver startup-speedup claim.
The production renderer artifact remains unchanged by that follow-up.

The following startup measurements and rejected hosted attempts are retained
from the pre-promotion decision:

The shared startup/complete-catalog fix passes local cold/warm Mario/FD and
Falco/Battlefield with zero unexpected pipelines and native/browser maxima of
9.805/27.350 ms. Required MEMFS setup now belongs to the shared player owner;
the public alpha still has no IDBFS. Selecting all 508 verified descriptors
removes the 18 misses in the earlier 280-member selection, whose
[failure evidence remains preserved](../history/SELECTIVE_PIPELINE_ALPHA.md).
The public build, 758-test suite (31 skips), and ten public UI checks pass.
An immutable noindex staging preview is available, but the startup-speedup
release remains **NO-GO**: one hosted cold pair measured 3075.557 ms disc-ready
against PR15's 3078.824 ms, and two hosted input-selection failures prevent full
hosted signoff. Production is unchanged. See the
[scoped fix and fast verification loop](../history/RENDERER_STARTUP_FIX.md).

The [source-address prerequisite](../SOURCE_ADDRESS_CONTEXT.md) models ordinary
original SDK/HSD allocation identity and defined register-byte consumption,
without supplying missing original allocation history or changing gameplay.
After a conflict-free normal merge of main, fresh complete visible CPU runs
retain exact 1,199-tick 2P and 4,346-tick 3P core/CPU comparisons against both
retail golds; the 3,838-tick 4P run still first differs at tick 2,495. Camera,
subject and extra-draw differences remain open. Five synthetic component tests,
Release browser/trace builds, and the 673-test local suite pass (46 optional
target/fixture skips); an added browser-failure-observer test passes separately.
The [reconciled evidence](../evidence/cpu-register-reconciled-v1.json) preserves
exact identities and failures. This is reusable groundwork, not a CPU fix or
performance/content admission.

The initial public alpha uses an explicitly **audio-disabled** release profile.
The public compile/link and JavaScript graph excludes the identified GPL-derived
resampler and coefficient generator; normal development audio remains separate.
The candidate mounts the player directly with the original minimal prototype
layout. It still reproduces the opcode-63 CPU-action abort, and the development
No Contest path can exit with unsupported pending scene 0. These are retained
failures. See [the alpha validation record](../history/PUBLIC_ALPHA_VALIDATION.md).
The alpha is live at [webmelee.gg](https://webmelee.gg/) after tested legal-contact
delivery before and after DNS migration and the exact artifact audit. Final apex
HTTP/browser, HTTPS and canonical redirect checks pass. This does not assert
broader gameplay stability.

A public-only startup defect prevented the live alpha from consuming its bundled
renderer seed: its writable MEMFS cache directory was missing. The unreleased
candidate creates that directory without persistence or native changes. Four
local cold/warm Mario/FD captures at DPR 1/2 complete with zero live pipeline
creation; native interactive maxima are 7.33–8.80 ms. Hosted directory controls
also remove live discovery, but first startup/disc-ready costs reach 21–24
seconds and remain a renderer investigation item. The live deployment is
unchanged. See the [five-way investigation](../history/PUBLIC_PERFORMANCE_INVESTIGATION.md)
for failed prefixes, transition costs, exact profiles and the frozen candidate.

Ordinary VS CPU levels 1–9 run in the shared compiled runtime used by both
entry pages. The [CPU match development corpus](../CPU_MATCH_CORPUS.md) now
has repeatable two-, three- and four-player retail reference pairs totaling
9,383 source ticks. Native and browser match all 1,199 core ticks of the
two-player match; the browser also matches all 4,346 core ticks and CPU
observations of the three-player match. The four-player browser completes
3,838 ticks but first differs in CPU-generated input at 2495, where the source
reads uninitialized stick values. Shared repairs cover the earlier hitlag
caller carry, clank/entry-scale rounding and common taunt loading. Headless
draw-dependent behavior and expanded camera/draw-phase comparisons remain
failing. The 480/480/240-tick CPU/human port regressions remain exact; 604 tests
pass with 35 optional-fixture skips. These are development results, without
CPU holdout, multiplayer, pixel, audio, physical-input or performance admission.

The browser runs original fighters and stages through compiled WebAssembly. The
accepted first slice is two Marios on Final Destination; narrower raw-PAD runs
also selected and rendered Falco versus Mario on Battlefield, Fox versus Mario
on Yoshi's Story, and Marth versus Mario on Dream Land. The remaining
acceptance boundaries are:
source stage and item rendering, stock/respawn/outcome flow, and the optional
renderer cache are integrated, while clean cold-cache/full-match first-use
stalls, audible and physical controller verification, and full original-game
equivalence remain open. No
emulator is shipped; Dolphin is used only as a separate original-game reference.
The combined [performance and accuracy playbook](../PERFORMANCE_AND_ACCURACY.md)
defines the evidence levels, content admission workflow and failure-response
process used from this point forward.

Browser staging now uses bounded persistent CPU storage and transfers only used
ranges, preserving original GPU copy/draw ordering and completion backpressure.
Three complete 3,719-tick matches in one application keep Wasm capacity at
334,102,528 bytes with zero gameplay growth and stable teardown allocations;
the old mapping path reached 1,047,986,176 bytes on its third repetition.
The visible trace still matches both retail references exactly, and independent
Release cold/warm gates pass (native maxima 10.000/7.430 ms), with no live
pipeline creation, timing resumes or audio-queue failures. This closes the
measured repeated-workload allocation issue; broader memory and content
coverage remain open. See the [evidence ledger](../REPLAY_CORPUS.md#bounded-browser-staging).

Four additional complete retail reference pairs now add 13,701 source ticks on
Final Destination and Yoshi's Story with Fox, Falco and Marth. They include 43
damage increases and actual up-special execution. Shared screen-flash ownership,
shield/up-special rounding, projection-buffer storage and original ground
initialization are corrected in development. All four new visible gameplay
traces now match both original captures. The final one-ULP Marth position red
required shared pose and SDK quaternion-matrix rounding corrections, verified
first against captured scalar operands and outputs, then through all 2,901
ticks with source drawing. The reviewed startup seed now has 469 pipelines.
All eight complete regression games now match both original captures through
24,823 source-drawn ticks. Their 16 isolated cold/warm performance runs pass
with zero hard-gate failures, live pipeline creation or heap growth; the worst
native callback is 15.855 ms. Both reserved games then pass the frozen build
without runtime or seed changes: 6,219 additional exact source-drawn ticks and
four further cold/warm runs with zero hard-gate failures. This brings the
current build to ten complete games and 20 performance runs; broader gold,
pixel/PCM, physical-input and content admission remain open.
All 473 tests pass;
rebuilt fighter/stage lifecycle checks also pass after correcting a missing
rumble archive in the standalone Battlefield test bundle. See the [expanded evidence ledger](../REPLAY_CORPUS.md#expanded-development-corpus--eight-game-regression-gate).

The [first measured replay cohort](../REPLAY_CORPUS.md) now contains three
development workloads and one independently held-out workload, totaling 11,122
source ticks. Two new development reference pairs and the held-out pair repeat
exactly; all three match the visible source-drawn port through original endings
and teardown. Their six final Release cold/warm runs have zero hard-gate
failures (worst native callback 12.020 ms). The held-out game passed the frozen
runtime and seed without changes. This expansion fixed shared knockback FMA
rounding, added source-driven match-length discovery and measured coverage
selection, and brought the reviewed startup seed to 432 pipelines. A retained
headless red exposed original magnifier drawing's effect on later offscreen
damage; visible comparison is now mandatory. The full 453-test suite and the
updated 12-test coverage suite pass. All four workloads remain Fox/Falco on
Battlefield with sparse combat and no up-special; broader gold/content
admission remain open; the subsequently measured staging fix above closes that
cohort's observed live heap growth.

The [complete-game replay calibration](../investigations/COMPLETE_REPLAY_CALIBRATION.md)
now covers a derived 3,122-tick Fox/Falco Battlefield elimination match. Two
independent JITARM64 references repeat exactly; Release/headless and visibly
drawn port traces match every declared field through the original ending and
successful teardown. Shared Hermite, DI, joint-matrix and knockback rounding
boundaries now follow the pinned retail instructions. Effect-parameter setters
address their real table, fixing a shield-triggered overwrite caused by an
original link-layout assumption. That calibration's startup seed contained 427
pipelines. Final visible Release cold/warm runs pass all 3,122 ticks with zero
hard-gate failures (worst native callbacks 11.330/12.485 ms; browser intervals
21.365/22.010 ms). The final collector also matches the full repeated reference
trajectory in Interpreter64; its protected dequeue observation resolves the
retained controller-capture race. All 435 tests pass. This trajectory includes seven stock losses and six respawns but only
five damage increases and no up-special; broad corpus admission remains open.

The separately scoped [source Slippi profile receipt](../evidence/source-slippi-profile-prefix-v1.json) records the optional fixture profile, raw-input comparison through the recorded fourth-stock-loss boundary, default-profile regression, and required validation. The [native-initializer and scheduler-mask follow-up](../evidence/source-slippi-native-initializer-mask-v1.json) separately records default/native initializer diagnostics, ordinary pause/resume, the fresh recorded prefix and scoped source restoration checks. Results/rematch and actual browser multiplayer remain open. Slippi ingestion now normalizes finalized per-frame input/state records, handles
rollback history without mixing revisions, preserves exact field bits and
rejects incomplete timelines. The v2 input-only workload transport retains
source identity and records derived rules explicitly; it never treats UCF
post-frame observations as a vanilla oracle. The existing modern Fox/Falco
Battlefield fixture completes all 686 input frames and teardown in the source
runner. That is workload evidence, not browser performance or equivalence.
