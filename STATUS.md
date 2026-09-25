# Current status

## Repository public; main changes restricted to the owner

The [publication record](docs/PUBLICATION_CUTOVER.md) and
[cutover receipt](docs/evidence/publication-cutover-v1.json) record the authorized
public transition after the final audit. Only `ericvanlare` can merge into
`main`, through PRs with current required CI; direct updates, force pushes and
deletion are blocked. Private-era caches were cleared, and reporting and
contribution controls were enabled. This changes no gameplay admission or
hosted/native release scope.

## Audio and memory fixture complete; equivalence investigation paused

**Compiled / Source identified / Native traced / Retail compared** within the
[original audio and memory fixture](docs/SOURCE_LBAUDIO_MEMORY_STARTUP.md).
The [scoped receipt](docs/evidence/source-lbaudio-memory-startup-v1.json) records
original initialization and the post-execution memory-descriptor comparison.
The owner paused further equivalence investigation. The first session mismatch
remains match 0, tick 1776, P4 `input_hex`; live fighter identities and CPU
call-site integration remain incomplete. No browser progress or deployment.

## Original Synth startup joins shared audio ownership

**Compiled / Source identified / Native traced / Retail compared** for the
[bounded joined Synth fixture](docs/SOURCE_SYNTH_JOINED_STARTUP.md).
Original Synth owns AX initialization and its first deferred DevCom request
completes through the original AR/ARQ handlers. The
[scoped receipt](docs/evidence/source-synth-joined-startup-v1.json) limits the
original comparison to three AR reservation sizes, final stack and free count.
The later fixture above extends audio initialization through memory descriptors;
live fighter allocation binding and full-session equivalence remain open. This does not establish PCM, timing or browser behavior.

## Original AR and ARQ share checked DMA ownership

**Compiled / Source identified / Native traced** for the [AR service fixture](docs/SOURCE_AUDIO_AR_SERVICES.md).
Original size probing and deferred ARQ completion execute through checked cache,
interrupt-mask and SDK context services. The [scoped receipt](docs/evidence/source-audio-ar-services-v1.json)
records negative ownership controls. The later Synth fixture above joins this
owner; full-session equivalence remains open; this component does not establish PCM or timing.

## Original Synth parameters are derived independently

**Compiled / Source identified / Native traced / Retail compared** for the single
bank-size field in the [parameter probe](docs/SOURCE_SYNTH_PARAMETERS.md).
The [scoped receipt](docs/evidence/source-synth-parameters-v1.json) distinguishes
source-only checks, owned-DOL execution and the post-execution capture comparison.
No Synth execution, allocation addresses, PCM or session equivalence is claimed.

## Original AX startup reaches first AI DMA

**Compiled / Source identified / Native traced** for the [joined AX startup fixture](docs/SOURCE_AX_STARTUP.md).
Original AX units and the original DSP handler initialize the actual AX task and
enable its first DMA through checked modeled services. The
[scoped receipt](docs/evidence/source-ax-startup-v1.json) records validation and
negative controls. DSP firmware, PCM, full Synth/application startup and live
browser integration remain open; this does not establish session equivalence.

## CPU source-word ownership prerequisite

**Compiled / Source identified** for the [typed CPU carry boundary](docs/CPU_R5_CARRY.md).
The owned-input adapter derives the RNG seed identity independently; the fighter
identity in these tests remains synthetic. The [scoped receipt](docs/evidence/cpu-r5-carry-v1.json)
records generation checks and source compilation. Live allocation binding and
CPU call-site hooks remain open; the first session divergence is unchanged.

## Original DSP startup protocol boundary

The [DSP startup fixture](docs/SOURCE_DSP_INIT_PROFILE.md) executes original SDK
boot/task/handler routines with checked mailbox, interrupt and context services.
The [scoped receipt](docs/evidence/source-dsp-init-profile-v1.json) records synthetic
protocol validation. The later AX fixture above joins original AXOut; DSP
firmware, PCM and live browser integration remain open. Full-session equivalence is not established.

## Original AIInit component boundary

The [AIInit fixture](docs/SOURCE_AI_INIT_PROFILE.md) executes original SDK startup
against a bounded GameCube Audio Interface profile. Its startup snapshot is
separate from synthetic DMA programming and calibration checks; the
[scoped receipt](docs/evidence/source-ai-init-profile-v1.json) records validation.
This standalone result excludes the later AX fixture above, full audio startup
and live browser source bindings.
Full-session equivalence remains open.

## Original ARInit profile boundary

The [bounded ARInit oracle](docs/SOURCE_AR_INIT_PROFILE.md) executes original
size probes through checked DSP-register, cache and ARAM services. Its owned
run agrees on six original runtime relations and two derived input fields; the later allocation
sequence is synthetic. See the [scoped receipt](docs/evidence/source-ar-init-profile-v1.json).
This remains separate from DevCom, AI/AX, application startup and live browser
source identities. Full-session equivalence is still open. No deployment.

## First original DevCom request joins the validated startup heap

**Compiled / Source identified / Native traced / Retail compared** for the
standalone first type-3 request and its declared HSD audio-allocation fields.
The source queue also passes scoped deferred completion and node-reuse checks.
See the [boundary](docs/SOURCE_DEVCOM_STARTUP.md) and
[receipt](docs/evidence/source-devcom-startup-v1.json). Full synth/audio startup,
remaining ARAM reservations and live session integration remain open.

## Original HSD startup executes from independently derived boot roots

**Compiled / Source identified / Native traced / Retail compared** for the
standalone checked-Wasm allocation boundary: original source routines execute
XFB/FIFO and HSD heap/component initialization using owned-disc boot data.
The comparison and exact limits are in the
[scoped receipt](docs/evidence/original-startup-allocation-v1.json).
Original audio allocation effects, game-heap/scene ownership, live browser
source identities and register carry remain open. This is not full-session
browser equivalence. No deployment was performed.

## Native HSD component pools share world initialization

**Compiled / Source identified / Native traced** for the bounded component-pool
lifecycle: native scene owners share the original component initialization order
instead of reinitializing Shadow and ZList at individual entries. See the
[scoped receipt](docs/evidence/native-pool-initialization-v1.json) for lifecycle
checks, the diagnostic browser prefix and retained failures. This does not
provide live original heap identities or CPU register carry; full-session
browser equivalence remains open. No deployment was performed.

## Repeated VS allocation ownership reaches second match entry

**Compiled / Source identified / Native traced / Retail compared** for recorded
allocation fields and source operation order: native and checked-Wasm models
replay all 3,857,302 calls from cold boot through the first four-Mario CPU9 match,
Results, CSS/SSS and second VS entry. Two independent original captures are
byte-identical. Explicit demo-fighter wrappers distinguish Results ownership
from VS generations, and disk-backed replay retains every compared command and
result. See the [scoped receipt](docs/evidence/original-allocation-repeated-ownership-v1.json)
and [diagnostic controls](docs/ORIGINAL_ALLOCATION_HISTORY.md#repeated-vs-ownership-diagnostic-controls).
The second VS owner remains active at this boundary. Complete three-match
ownership, a live browser source-address provider, CPU register carry and
full-session browser equivalence remain open. Pixels, PCM and live performance
are separate gates. No deployment was performed.

## Source allocation replay derives first-VS fighter identities

**Compiled / Source identified / Native traced / Retail compared** for the
declared allocation fields and recorded operation order: native and checked-Wasm
models now replay the entire first-VS initialization prefix and derive all four
Mario CPU9 fighter identities. Two fresh original captures are byte-identical.
The replay compares asynchronous compaction manager transitions, relocated
handle identities and source-declared pools, including embedded pool members.
See the [scoped receipt](docs/evidence/original-allocation-compaction-v1.json)
and [remaining source-context boundaries](docs/ORIGINAL_ALLOCATION_HISTORY.md#first-vs-initialization-capture).
The [earlier passive-capture receipt](docs/evidence/original-allocation-passive-v1.json)
retains the previous first-compaction failure. Copied payload contents,
browser address/register context, full-session equivalence, pixels,
PCM and live performance remain unproven. No deployment was performed.

## Reproducible whole-session CPU register boundary

**Compiled / Source identified / Native traced / Browser exercised**: a fresh
three-match four-Mario CPU9 reference now has exact SI input recording. A control
replay and a read-only register-probe replay agree on every declared primary
observation, excluding host timestamps and run IDs. The bounded headless browser
comparison isolates a zero-knockback CPU-input difference; the probe confirms
the original seed-pointer and fighter-pointer register carry. Exact hashes,
comparison fields, first divergence, validation and retained failures are in the
[diagnostic receipt](docs/evidence/whole-session-cpu-register-v1.json) and
[register investigation](docs/CPU_REGISTER_COMPATIBILITY.md#reproducible-whole-session-register-diagnostics).

This establishes a reproducible diagnostic boundary. The live original allocation
context and shared compiled register-carry implementation remain open;
full-session browser equivalence, pixels, PCM and performance are not established.

## Whole-session original observer

**Compiled / Source identified**: the passive observer completed consecutive
original CSS → SSS → four-stock VS → Results → CSS captures. The
[capture receipt](docs/evidence/whole-session-observer-pr57-v1.json) binds the
source, build, four-player CPU workload, complete stream, decoder checks and
retained failures. This is experimental capture infrastructure; independent
repeatability, source-port comparison, pixels, PCM and performance remain
separate gates.

The [typed first-CSS profile receipt](docs/evidence/typed-profile-pr58-v1.json)
binds original GameRules/SaveData decoding to the same completed capture.
Profile consumption and browser equivalence remain separate checks.

## Whole-session replay producer and bounded consumer evidence

**Compiled / Source identified / Browser exercised**: PR59 adds an MWRC v8
exporter, typed initial-context import and strict scene/input ownership checks.
The original workload uses four level-9 Mario CPUs. Its bounded browser
comparison exposes a CPU-input difference consistent with the existing source
address/register-context limitation; whole-session equivalence remains open.
Builds, tests, exact comparison scope and retained failures are in the
[PR59 evidence receipt](docs/evidence/whole-session-replay-pr59-v1.json) and
[whole-session section](docs/VERSUS_RETURN_LOOP.md#pr59-whole-session-producer-and-bounded-replay-evidence-2026-09-24).

## Mewtwo capture and menu ownership repair

The Mewtwo Confusion victim command rows and menu rumble/GObj lifetime repairs
are validated at their declared boundaries. **Native traced**: the original
menu/four-stock lifecycle completes twice with cancellation and teardown.
**Browser exercised**: Release and RelWithDebInfo both select Mewtwo through
the original CSS, cancel SSS, capture/release Mario with Confusion and unload.
See the [merge review receipt](docs/evidence/mewtwo-crash-merge-review-v1.json)
for exact builds, retained failures and integration checks. This repairs a
crash; independent equivalence, full fighter admission and deployment are
separate gates.

## Non-disruptive agent browser checks

Routine browser checks default to headless installed Chrome; foreground
captures require explicit `--headed`. Agent guidance also covers temporary
scripts. **Browser exercised**, bounded functional utility: GPU output,
screenshots, input, diagnostics, player lifecycle and PCM inspection retain
their scope in the [browser automation guide](docs/HEADLESS_BROWSER_VALIDATION.md).
Initial Import now waits for renderer readiness from post-main native frames.
The [implementation receipt](docs/evidence/headless-browser-defaults-v1.json)
binds fresh builds, the full suite, passing migrated browser checks and the
macOS observation with no test-browser window or focus interruption. The
original [investigation receipt](docs/evidence/headless-browser-compatibility-v1.json)
retains the early-import failure. Headless results do not admit gameplay or
replace foreground timing, physical-device, audible-output or
original-comparison evidence.

## Ness and Peach development receipt

The Ness and Peach development candidate has source construction, Results lifecycle,
and bounded headless browser evidence recorded in the [PR61 receipt](docs/evidence/results-pr61-ness-peach-v1.json). The receipt also records the read-only four-door CSS observer and a bounded four-player CPU9 source prefix. Broad action, retail, original-equivalence, and timing/performance gates remain separate.

## Donkey Kong platform shield-drop repair

The Battlefield shield-drop crash from [issue #50](https://github.com/ericvanlare/melee-web/issues/50)
is repaired. At the terminal of a fully consumed single-datum FObj track the
original reaches `FObjUpdateAnim` with `op_intrp` still NONE and passes an
uninitialized stack word to the callback — the same undefined-output class as the
observed FD terminal single-CON case. The host now defines that terminal output
as the authored last value (`p1`), which is what each interpolation type's own
zero-duration path emits; parser state and flags stay untouched and every other
undefined state still fails explicitly. **Focused checks**: the endpoint
reproducer (`hsd_native_trace --terminal-{branch-linear,branch-spline,
pass-endpoint,stop-ceil-endpoint}`) passes after panicking pre-fix; the
Battlefield reducer that aborted at Pass frame 25 (`donkey-platform-pass-v2.log`
state) now completes Pass motion 244 for all five costumes with pause and
repeated teardown; both public and development menu contracts pass with
`CKIND_DONKEY` re-enabled; Donkey's both-orientation FD lifecycle passes
unchanged. **Suite**: local 1,129-test run passes with 55 skips (owned fixtures
for some skipped checks live in other worktrees). Donkey is re-enabled in
public character selection. The independent original Donkey Pass consumer
capture remains the open confirmation item; see
[Donkey Kong's measured scope](docs/DONKEY_KONG_PORT_NOTES.md).

## Production audio release path

PR #42 is merged on top of #49. The owner approved promoting replacement
audio to production independently of Results PR #44. The production package
now has an explicit `audio-player` identity and uses the same restricted Release
audio runtime as the combined listening preview. Its auditor binds the native
source/tool/seed identity, exact audio module inventory, production notices and
hosting policy. The legacy silent profile remains available for rollback.
See [production audio release](docs/AUDIO_PRODUCTION.md) for the commands and
verification gates. The combined preview's existing evidence remains scoped to
[its recorded browser and PCM checks](docs/evidence/audio-main-integration-v1.json);
it does not establish full-match performance or original hardware fidelity.

## Development audio replacement candidate

The scalar resampler and browser coefficient generator have new implementations
with recorded specifications and provenance. The coefficient output is unchanged
byte for byte, and 7,488,064 scalar comparisons preserve the previous output,
phase, history and source reads. This is regression evidence, not an original
hardware accuracy pass. The [replacement record](docs/AUDIO_REPLACEMENT_EVIDENCE.md)
also records an unverified original-DSP phase-writeback boundary and the retained
coefficient-data provenance that still needs release review. The separate GPL
Dolphin observer remains intact. The production release path is described above;
repository visibility is unchanged.

The owner-requested [audio listening preview](docs/AUDIO_PREVIEW.md) now includes
main `979fd09` (PR #49) and its per-scene asset loading. **Browser exercised**:
original CSS/SSS → Mario/Final Destination → ordinary pause/No Contest → CSS →
a second match entry retains nonzero PCM at the connected worklet. Four complete
asset scopes include the generated coefficients; outer pause/resume and Eject
pass. Both Release builds and the full local suite pass. The
[integration receipt](docs/evidence/audio-main-integration-v1.json) binds the
new hosted bytes and checks. The earlier owner listening feedback remains bound
to the preceding preview. Hardware PCM, broader gameplay and performance gates
remain open.

## September 20 production checkpoint

Feature additions are paused at PR #49. The candidate exposes sixteen public
fighters and seven stages; Donkey Kong's platform shield-drop crash is repaired
on this branch (see [Donkey Kong's measured scope](docs/DONKEY_KONG_PORT_NOTES.md)),
re-enabling him in public character selection pending the remaining
[issue #50](https://github.com/ericvanlare/melee-web/issues/50) verification.
Both public and development players now load exact scene asset scopes from a
validated local disc session. That checkpoint's public audio was disabled.

Both Release builds and the local 1,099-test suite pass (374.968 seconds,
41 explicit skips). The since-removed Donkey restriction had passed 26 focused
menu and release checks. The audited production candidate passes local HTTP checks,
all ten public browser checks, two ordinary-key Mario/FD pause/No Contest
round trips, Eject/reload and reimport. Public menu and Mario/FD scopes contain
32/27 inputs and 18,448,886/19,073,578 bytes. A fresh drawn Mario/FD control
matches all 240 declared updates and PAD-history fields against the independent
original pair and retires all native scene/file owners afterward.

Retained harness failures and an instrumented timing pause are recorded. Two
controls with reduced duplicate callback logging pass the same route and 120
further CSS ticks without a resume; the final control verifies hidden loading
panels. These lifecycle checks do not establish performance or complete-game
acceptance.
PR #49 merged as `979fd09` and its exact staging-tested package was deployed to
[production](https://bed694b0.webmelee.pages.dev). Both the immutable origin and
webmelee.gg passed exact resource/header/route checks and ten public browser
checks each. The owner also tested staging and reported multi-CPU loading lag
as follow-up work. See the [release record on PR #49](https://github.com/ericvanlare/melee-web/pull/49),
[release checkpoint](docs/PRODUCTION_CHECKPOINT_20260920.md) and
[hash-bound receipt](docs/evidence/production-checkpoint-20260920-v1.json).

The subsequent PR review's two P3 findings are corrected: reports reject
untracked original-source files, and Fountain's music tuple matches its authored
four words. All seven tuples and derived music sets pass owned-archive checks;
both Release targets and the refreshed candidate audit pass. See the
[review-fix receipt](docs/evidence/production-checkpoint-review-fixes-v1.json);
final full-suite and pushed-head verification are recorded on PR #49.

## September 21 production deployment

Latest main `dab94e2` (PR #52 Donkey re-enablement, PR #53 Captain Falcon
dive-catch admission, PR #54 repository readiness) is deployed to production as
the audited `audio-player` package:
[immutable origin](https://59cf5240.webmelee.pages.dev) and
[webmelee.gg](https://webmelee.gg), deployment
`59cf5240-7deb-4447-9d38-4435c85c519f`. The exact staged bytes were promoted
unchanged from the verified staging deployment
`3309b04e-87f1-4a1d-8f24-cf0129e00a52`. Hosted HTTP verification passed on all
four checked origins (31 resources, 5 aliases each), and both headed browser
checks (ten public-player and eleven audio/PCM cases including nonzero PCM
through CSS → SSS → Mario/Final Destination → pause/No Contest → second entry)
passed on the immutable production origin and the apex; several first attempts
hit the shipped timing-guard pause on a loaded host and passed on idle-host
retries with PCM flowing. The local 1,168-test suite passes with 74 documented
asset-dependent skips. The same-session `a8d318f2` (main `4e521f6`), the PR #51
audio release `aa3d4852` and the silent releases `bed694b0`/`516608f3` remain
rollback targets. Hosted checks do not establish full-match performance,
original pixel/PCM fidelity, physical controllers or broad gameplay admission,
and the Falcon dive-catch browser interaction remains a separate gate; see the
local deployment records under ignored `work/` and
[public deployment](docs/PUBLIC_DEPLOYMENT.md).

## Full-game integration branch

`codex/full-game-integration` tracks the remaining offline vanilla game through
[a versioned feature and source inventory](docs/FULL_GAME_PORT.md). The report
retains all source functions and missing features; it does not turn compilation
or declared feature status into an accepted-game percentage.

The development player now imports scene-specific assets from one validated
local disc session, closing source owners and releasing outgoing files before
loading the next scene. A real browser check completes Mario/FD four-stock
ending, Bowser/Mario Fountain pause and No Contest, both returns to original
CSS, unload and restart. Delayed handoffs keep source steps stopped and retain
zero outgoing file bytes. Menus use 18,452,982 native input bytes; Mario/FD uses
19,077,674 and Bowser/Fountain 21,401,641. Unload clears all imported files.
Wasm capacity still grows across the sequence; this is functional lifecycle
evidence, not a memory plateau or performance pass. A fresh drawn Mario/FD
replay matches all 240 declared updates and PAD-history fields against the
independent original v2 pair. The earlier missing Bowser voice-bank unload
crash and two driver/recipe failures remain retained; see
[scene asset ownership](docs/SCENE_ASSET_LOADING.md).
The public player now uses the same scoped import while excluding DSP
coefficients. Four-player residency and broader mode/lifetime checks remain open.
New additions are paused for a production checkpoint with sixteen public
fighters and seven stages. Donkey is re-enabled in public selection after the
platform shield-drop repair; the remaining
[issue #50](https://github.com/ericvanlare/melee-web/issues/50) verification
stays open.
Both Release builds pass. The 1,096-test run took 366.767 seconds with
41 skips and one stale artifact-count assertion; correcting that test yields
four passing checks in the affected module. The original failed suite log is
retained. Commit `3103b65` subsequently passed full GitHub verification in
6m 04s across all jobs. See the
[scene-loading checkpoint receipt](docs/evidence/full-game-checkpoint-asset-scope-v1.json).
The refreshed legacy native transition comparison fails on a rumble flag and
match-entry RNG; a HEAD-derived driver reproduces the same event rows with
current libraries. Its setup/source reconciliation remains open separately;
see [the retained transition limitation](docs/TRANSITION_EQUIVALENCE.md#september-20-refresh-limitation).

Ganondorf is the first added development candidate. Fresh native checks pass
both player orientations and all five costumes on Final Destination, including
original combat, pause and teardown. The browser reaches original CSS/SSS and
advancing Ganondorf gameplay. A reviewed 37-pipeline preload correction clears
the 30-case, 5,200-frame drawn sweep with no hard timing/audio/pipeline failures;
earlier cold failures remain retained. A following cold/warm pair passes all
10,400 source frames with the same zero-failure counters at 640×480, DPR 1.
An independently repeated original pair now matches all 603 declared native
state updates after binding the actual save unlock profile. The legacy unbound
recipe failure and headless camera mismatch remain retained; drawn comparison,
complete ending and broader acceptance are open. See [Ganondorf scope and remaining gates](docs/GANONDORF_PORT_NOTES.md).
Captain Falcon now passes native source lifecycles in both Captain/Mario player
orders and all six costumes on Final Destination, including specials, stock
loss/respawn, pause and repeated teardown. The integration preserves his
original English costume resolution, indexed vertex colors and particle palette
metadata. The browser completes all 30 action cases over 5,200 drawn frames;
the first diagnostic run fails timing/audio/pipeline gates. After a reviewed
35-descriptor preload correction, both cold/warm action sweeps pass all 10,400
frames with zero hard failures. Startup and broader performance remain separate,
and independent original comparison is pending; see
[Captain Falcon's measured scope](docs/CAPTAIN_PORT_NOTES.md).
Hyrule Temple passes two native stage-owner lifetimes, including scaled geometry,
original map callbacks, light identity overrides, music candidates and teardown.
Two original Ready/pause/No Contest match lifetimes also pass. The browser
reaches advancing Temple gameplay, retaining an entry timing failure; a reviewed
16-descriptor preload correction clears both cold/warm entry reruns without
timing resumes. A cold startup long task remains visible, and the full action
matrix and original comparison remain open; see
[Temple evidence](docs/HYRULE_TEMPLE_PORT_NOTES.md).
Luigi now passes native source lifecycles in both player orders and all four
authored costumes. The selected-player fixture observes ground/aerial Fireball
Articles, special-move entries, pause and repeated teardown. All 30 browser
action cases pass over 5,200 drawn frames, retaining timing, audio and four
live-pipeline failures. A reviewed six-descriptor preload update clears both
5,200-frame cold/warm sweeps with zero hard failures. A fresh pair after the
shared stage-light lifecycle correction also passes all 10,400 frames with
zero hard failures; see [Luigi's evidence](docs/LUIGI_PORT_NOTES.md).
Fountain of Dreams now reaches original map, reflection and star construction.
Its shared changes preserve animated-light storage, exact reflection image
identity and multiple map objects sharing one camera. Two 7,200-tick native scheduler
and teardown lifetimes now pass, including moving collision and animated
lights. Cold/warm browser entry now completes Ready/Go and 30 gameplay frames
after a reviewed 28-pipeline preload correction. The cold run retains two
browser long tasks; this is a functional-entry result, not a performance pass. See
[Fountain's scope](docs/FOUNTAIN_OF_DREAMS_PORT_NOTES.md).
Pikachu and Pichu are now enabled as development candidates, bringing the
branch to thirteen fighters. Native fixtures pass both player orders and all
four family costumes, including source jolt/Thunder creation and teardown,
Pichu self-damage, pause and No Contest. Both browser discovery sweeps complete
30 action cases over 5,600 frames; their failed cold timing/pipeline gates
remain retained. A reviewed preload correction clears all four fresh cold/warm
sweeps: 22,400 source frames with no hard failures, native target misses or
heap growth. Shared fixes preserve complete-null effect
rows and the signed self-damage command. See
[their measured scope](docs/PIKACHU_PICHU_PORT_NOTES.md).
Yoshi's Island 64 is the seventh development stage. Its native owner passes two
5,000-tick source scheduler lifetimes, including cloud collapse/collision
removal/reappearance, guest selection and teardown. Five Mario/Mario entry,
pause and No Contest lifetimes also pass. The original browser menu route
reaches advancing gameplay, retaining a cold entry stall and one diagnostic
resume. Fresh cold/warm entry passes after the preload correction without
resumes, gaps or audio underruns; one 69 ms cold browser long task remains.
This is functional entry, not a stage-performance pass.
The shared material loader now preserves the source base TLUT for
TIMG-only animation; see [stage evidence](docs/YOSHIS_ISLAND_64_PORT_NOTES.md).
Jigglypuff is the fourteenth development fighter. Native checks cover all five
costume lifetimes, including original hat archives and dynamics, crouch
animation variation, five aerial jumps and ground/air special states. The
original browser CSS/SSS route passes 31 action cases in each cold/warm run:
12,800 source frames with no action-window timing, audio, pipeline or heap-growth
failures. Shared fixes retain the complete stored dynamics table separately
from its active body count, the custom-part owner/cache and original crouch
Wait selection. Earlier loader, blue-hat and input-recipe failures remain
retained; see [Jigglypuff's measured scope](docs/JIGGLYPUFF_PORT_NOTES.md).
The Jigglypuff checkpoint passes both Release builds and the 1,077-test suite
in 346.034 seconds with 41 explicit skips. Fresh shared headless comparisons
still match 603 Ganondorf/FD and 240 Mario/FD declared source updates. Its
pipeline seed is unchanged; see the [Jigglypuff checkpoint receipt](docs/evidence/full-game-checkpoint-purin-v1.json).
Donkey Kong is the fifteenth development candidate. Native checks pass both
player orders and all five costumes, with first-lifetime ground/air specials
and raw cargo grab/walk/throw. Shared fixes preserve branch visibility,
source dynamics modes, mixed indexed/compressed texture animations and the
carried fighter's original command graphs. Both browser discovery rounds
complete 33 action cases and 7,000 frames; cold fails timing/audio/pipeline
gates while warm passes. After a reviewed 15-descriptor preload correction,
both fresh rounds pass all 14,000 action frames with zero hard failures.
A separate Battlefield shield-drop probe exposed a real crash at the
undefined terminal SPL0 animation output. That boundary is now repaired: the
host defines the terminal single-datum output as the authored last value (the
documented FD terminal-CON rule extended to the whole class), and the retained
shield-drop reducer completes Pass motion 244 on the upper platform for all
five costumes with pause and repeated teardown. Donkey is re-enabled in public
character selection. Independent original consumer capture, ceiling lifetimes,
original comparison and broader acceptance remain open; see
[Donkey Kong's measured scope](docs/DONKEY_KONG_PORT_NOTES.md) and
[issue #50](https://github.com/ericvanlare/melee-web/issues/50).
The Donkey checkpoint passes both Release builds and the full 1,080-test suite
in 358.963 seconds with 41 explicit skips. Fresh shared headless comparisons
still match 603 Ganondorf/FD and 240 Mario/FD declared source updates. The
receipt retains the failed cold discovery, the corrected pair, and the separate
unresolved shield-drop crash; see the
[current checkpoint receipt](docs/evidence/full-game-checkpoint-donkey-v1.json).
Bowser is the sixteenth development fighter. Native Final Destination checks
pass both player orders and all four costumes, including Flame creation and
cleanup, ground/air Fortress and Bomb, and real ground/air capture and throws.
The shared boundary now admits the victim's original command graphs and the
source visibility cleanup command. Both browser discovery runs finish all
30 cases; the cold timing/audio/pipeline failures remain retained. A reviewed
36-descriptor preload correction clears the fresh cold/warm pair: 11,201
source frames with zero action-window timing, audio, pipeline or heap-growth
failures and no entry timing resumes. Independent original Bowser comparison,
drawn capture interactions and broader acceptance remain open; see
[Bowser's scope and evidence](docs/BOWSER_PORT_NOTES.md).
The Bowser checkpoint passes both Release builds and 1,083 tests in 357.995
seconds with 41 explicit skips. Fresh shared headless comparisons still
match all 603 Ganondorf/FD and 240 Mario/FD declared source updates. The
[Bowser checkpoint receipt](docs/evidence/full-game-checkpoint-koopa-v1.json)
binds the builds, browser pair, review and retained failures. The Bowser
checkpoint's full GitHub Verify completed in 6m 16s across all jobs;
the preceding Donkey checkpoint completed in 6m 20s.
Mewtwo is the seventeenth development fighter on `codex/mewtwo-integration`.
Native Final Destination checks pass both player orders and all four
costumes, including ground and aerial Shadow Ball charge with fresh-B-edge
release and Article teardown, Teleport, Confusion and Disable lifetimes. The
source contract preserves the 0x88 `ftMewtwoAttributes` extension, the
two-slot Article table (Disable 0x6e, Shadow Ball 0x70 with twelve
serialized special words and ten animation rows), effect bank 13 with four
static rows and the authored four-TObj costume texture map: the match-stats
eye telemetry now requires the runtime collection to match each fighter's
authored texture map while keeping the two declared recorded slots and all
existing comparison fields. The versioned `mewtwo-visible-actions-v1`
inventory has 32 cases; a fresh-origin cold export supplied 12 new portable
pipeline descriptors (zero payload conflicts, all 846 previous records
preserved), producing the reviewed seed of one shader and 857 pipelines
(SHA-256 `f85858ff99f368d38ca16fa18727a12f778d57543e2898d29507f92585db2c2b`).
After that preload correction both fresh cold/warm rounds pass all 32 cases
over 6,400 frames with zero hard-gate failures, native/browser maxima
6.645/24.720 ms cold and 5.920/21.710 ms warm, and zero timing resumes or
heap growth. The local 1,127-test suite passes in 278.909 seconds with 60
documented optional skips. Independent original comparison, pixels, PCM,
physical controllers, complete matches and broader performance remain open;
the failed v5 cold discovery and its provenance, review and passing pair are
retained under `work/full-game/`. See
[Mewtwo's scope and evidence](docs/MEWTWO_PORT_NOTES.md) and the
[current checkpoint receipt](docs/evidence/full-game-checkpoint-mewtwo-v1.json).
After merging main (Donkey's re-enablement and the Captain Falcon dive
catch), an independent review corrected the telemetry bounds to the original
five-slot collector and the merged tree re-passes: 1,131 tests OK in final
suite form with 57 documented skips, both native orientations, and a fresh
cold/warm browser pair (all 32 cases, native/browser maxima 7.625/23.265 ms
cold and 6.910/25.460 ms warm) with zero hard-gate failures.
The preceding Pikachu/Pichu/Old Yoshi checkpoint passes both Release builds and 1,075
tests in 334.490 seconds with 41 explicit skips. Fresh headless comparisons
still match 603 Ganondorf/FD and 240 Mario/FD declared source updates; the
new content has no independent original comparison yet. See the
[Pikachu/Pichu/Old Yoshi receipt](docs/evidence/full-game-checkpoint-pikachu-old-yoshi-v1.json).
The preceding Luigi/Fountain checkpoint passed both Release builds and 1,070 tests
in 298.028 seconds with 41 explicit skips. The receipt names the exact browser
build, the later Article validation guard, reference scope and retained failures;
see [checkpoint validation](docs/evidence/full-game-checkpoint-luigi-fountain-v1.json).
Results PR #44 and audio PR #42 remain separate draft dependencies. This branch
work has not changed the deployed public alpha.

## CI verification turnaround

Three consecutive full PR verification runs completed in 6m 32s for a source
change, 5m 49s for a test change and 8m 53s for a cold dependency change. The
workflow uses standard Linux runners, partitions the existing targets/tests,
and reuses only validated compiler-cache entries. All 895 previously passing
baseline test IDs remain covered; deliberate build/test failures reject the
aggregate, and a new PR revision cancels obsolete work. Automatic PR and main
verification remain, with duplicate feature-branch push runs removed.
See [CI timings, coverage, cache boundaries and cost](docs/CI_COST.md) for the
measured evidence and retained outliers. This changes verification turnaround;
gameplay accuracy and runtime performance keep their separate acceptance gates.

## Historical GPU stall investigation

A new exact-historical-runtime diagnostic on Chrome for Testing 153.0.8010.36
reproduced a 104.370 ms native callback with 94.815 ms of staging waits. Its
retained Chrome event excerpt identifies browser UI raster pipeline work;
recovered native kernel evidence encloses the entire failure. The original
full Chrome trace/report were lost during analysis, with partial observations
and the recovered kernel recording retained explicitly. The second bounded
slot did not reproduce the stall and failed focus. This is diagnostic evidence,
not a fix or acceptance pass. See the [investigation and recovery limits](docs/BROWSER_RASTER_STALL.md).
The original failure remains causally unassigned. The project owner approved a
separate [current-runtime holdout gate](docs/HITCH_CAPTURE.md#approved-current-runtime-scope--2026-09-19)
on September 19, retaining that historical failure and every hard threshold.
Issue #33's approved current-runtime gate now passes. The first campaign failed
on two live pipelines and remains preserved with its final three slots unstarted.
PR #47 adds exactly the two recovered portable descriptors while preserving all
626 previous records. Its 7,347-update development regression matches declared
state and timer with zero live pipelines. Both Release builds, 1,006 tests
(44 optional skips) and full GitHub Verify pass; Verify took 6m 06s.

Two replacement inputs were reserved from header/input-only evidence before
tuning. Independent original reference pairs and browser comparisons match
6,432 Marth/Marth Battlefield and 8,561 Falco/Falco Final Destination declared
state updates and exact timers. All four frozen unprofiled cold/warm runs pass:
29,986 updates/draws, no native target misses, hard gaps, long tasks, audio
underruns/overflows, live pipelines, timing resumes, focus losses or browser
errors. Native/browser maxima are 13.695/29.000 ms on Apple M4, macOS 26.6.2,
Chrome 153.0.8010.50, 640×480 at DPR 2. Preparation takes 149.600–166.200 ms;
Marth retains 66,846,720 bytes of live heap growth and Falco zero. Both inputs
exhaust their frozen cap before original match ending; neither is a complete
original match. These are `per_tick` measurements, without original draw-cadence,
live-controller, pixel or PCM admission. See the
[failed campaign, correction and accepted replacement evidence](docs/CURRENT_RUNTIME_HOLDOUTS_20260919.md).

PR #47 is merged and deployed at [webmelee.gg](https://webmelee.gg). Both
production origins pass exact HTTP verification and all ten public browser
checks. The public alpha remains deliberately silent; its hosted functional
checks are separate from the audio-enabled development performance gate.
See the [current release receipt](docs/evidence/public-marth-pipeline-release-v1.json).

The current PR #38 development Release completed two cold/warm rounds on
Fox/Marth Dream Land and Marth/Falco Yoshi's Story: 37,920 source updates/draws,
zero hard hitch failures and one retained native target miss at 18.300 ms
(worst browser interval 30.675 ms). A concurrent audio build interrupted the
initial plan; its contended and incomplete attempts remain recorded, and a
separately frozen six-slot recovery supplied the affected cache pairs and
unfinished coverage. No historical failure was reclassified. See the
[complete timing inventory, memory/preparation evidence and remaining decision](docs/DEVELOPMENT_TIMING_20260919.md).

## Link and Young Link development candidate

Link and Young Link are enabled through the original CSS/SSS and shared native
runtime, merged in PR #32 at `2ee80ab`. Both retain their original
identities, five costumes, attributes, actions, articles, sword effects and SSM
files. Real-asset lifecycle checks cover both player orientations and all five
costumes, including reconstruction and teardown.

The physical four-stock Link/CPU2 Young Link recording on Yoshi's Story is
**retail compared under its recorded controller-queue schedule**: all 7,070
state updates and 7,064 draw boundaries match. The compared fields cover both
fighters, input/PAD/RNG, CPU decisions, camera, subject bones, HUD, magnifier and
match outcome. A fresh original replay independently matches 38,108 semantic
events. Source fixes address hookshot stack corruption and restore the original
inlined fused arithmetic; comparator fields and tolerances are unchanged.
The final reviewed integration replay again matches all 7,070 updates and
7,064 draws; the PR #32 integration suite passed 956 tests with 44 skips. That Release
state capture records 145.105 ms native / 153.620 ms browser maxima, including
140.830 ms of staging waits, and 342 audio underrun frames. It is not a
performance pass; the earlier port receipt remains separate evidence.

Performance, pixels, PCM, live scheduling and unexercised moves remain open;
Young Link human-controlled retail coverage is not implied by this match.
The PR #32 build was deployed at [webmelee.gg](https://webmelee.gg) as the silent
alpha. Its exact hosted bytes and public browser smoke passed. Both Link/Young
Link orientations also complete a bounded ordinary-keyboard functional smoke
on staging, with cold timing pauses retained under
[#33](https://github.com/ericvanlare/melee-web/issues/33); this does not broaden
character or performance admission. See the
[deployment receipt](docs/evidence/public-link-release-v1.json),
[port notes](docs/LINK_PORT_NOTES.md)
and [bounded evidence receipt](docs/evidence/link-young-link-replay-v1.json).

The new [GPU compilation diagnosis](docs/LINK_GPU_STALL.md) correlates a
482.785 ms Link staging wait with synchronous Metal compiler work for four
missing first-draw descriptors. The reviewed seed preserves all existing records
and adds 78 reviewed descriptors. Its full 7,070-update/7,064-draw state replay
is exact with zero live pipeline creation; both Release builds, 957 tests
(44 optional skips), package/HTTP audit and ten public browser checks pass.
The state capture retains a browser gap and audio underruns. Four separate
bounded public segments (both player orders cold/warm, 450 updates each) pass
without timing pauses, live pipelines or native target misses; native/browser
maxima are 11.370/25.245 ms. These silent functional checks do not establish
full-match performance. The historical a822 stall remains unresolved. The first
current-runtime holdout campaign failed; a separately frozen replacement
campaign passes with the two-descriptor correction and fresh inputs recorded
above. PR #38 is merged, and this
fix remains deployed at [webmelee.gg](https://webmelee.gg). Its original production
bytes and ten headed browser checks pass on both the immutable origin and apex;
see the [PR #38 release receipt](docs/evidence/public-link-pipeline-release-v1.json).

## Dr. Mario and Roy development candidates

The two physical development recordings now pass complete visible browser
comparison **under their recorded controller-queue schedule**: Roy/Doc on Final
Destination matches 6,965 updates and 6,959 draws; Doc/Roy on Yoshi's matches
11,077 updates and 11,067 draws. Fighter/input/PAD/RNG state, CPU decisions,
camera, subject bones, HUD, magnifier and match outcomes all match the original.
MWRC v6 carries independently observed nonempty input-queue snapshots as platform
inputs; it does not claim original CPU interrupt or live scheduling equivalence.

The first Yoshi's run exposed six tiny throw-position differences. Restoring the
original three fused multiply-adds in `ftCommon_8007E3EC` resolves all six without
changing comparator fields or tolerances. A 1.2-second scalar regression covers
original operands and the failing unfused control. Deliberate host stalls also
preserve the tested input-storage cases and both original Dolphin prefixes;
physical capture under stress is not claimed.

The final state runs measured 16.370/31.950 ms native/browser maxima on FD and
20.045/57.730 ms on Yoshi's. Yoshi's has one native budget overrun, one browser
gap and 22 audio underrun frames. Both runs still create first-use pipelines
(27/35) and grow the heap. Performance is **not admitted**. The historical GPU
stall, pixels, PCM and both unopened holdouts remain open. Release browser/native
builds and 765 tests (36 documented skips) pass. No deployment was made.
See [conditional replay scope](docs/RECORDED_QUEUE_REPLAY.md) and
[exact evidence and retained failures](docs/evidence/recorded-queue-replay-v1.json).

### Earlier startup-clock evidence and rejection

Dr. Mario and Roy load their own original data through the shared Mario/Marth
family adapters and are selectable through original CSS/SSS. Native lifecycle
checks cover both orientations, five costumes, specials, damage and reconstruction.

The user's physical Roy/CPU9 Dr. Mario capture now repeats exactly in Dolphin
(6,965 source ticks, 37,825 semantic events). The latest complete visible browser
replay matches every declared state domain: core fighter/input/RNG/PAD state,
CPU decisions, camera, subject/bone transforms, HUD, magnifier and match result.
Roy wins with three stocks, exactly as recorded. Repairs restore captured save
unlock state and original music-selection RNG, Roy's six authored dynamics modes,
source-free match preparation, and original fused joint/vector arithmetic.

The latest bounded replay also matches **all 6,959 original source draws**, with
all 6,965 state updates preserved. A passive original probe recovered the opening
PAD/VI phase without changing guest state; MWRC v5 carries compact clock context.
The periodic model predicts the later batches from source ticks 0–3, without a
recorded draw-index skip list. Legacy recipes and live input keep their existing
policy; live controller phase and other startup/VI configurations remain open.

This instrumented run measured 16.050 ms native / 26.240 ms browser maxima, with
zero pauses, overruns or audio underruns. The historical GPU staging stall remains
open. Cold/warm performance, pixels, PCM, other routes and both unopened holdouts
are not admitted. No production deployment was made. Release runtime/native
builds, the 757-test suite (36 skips) and subsequent focused binding/decoder/report
checks pass. See [clock replay evidence](docs/evidence/roy-dr-mario-clock-replay-v1.json),
[clock format and scope](docs/RETAIL_DRAW_CLOCK.md), and
[retained failures](docs/ROY_DR_MARIO_PORT_NOTES.md).

**Independent clock validation failed on Doc/Roy at Yoshi’s:** the fresh original
capture repeats exactly (11,077 updates, 11,067 draws, 58,668 typed semantic
boundaries), but the initial-only model puts one batch seven updates too late.
The original queue check at source 8,527 is delayed relative to the assumed
constant VI poll offset and consumes two samples. Equal final draw counts do not
establish exact cadence. Browser validation was deferred at this already-failing
gate; the earlier Roy/Doc result remains valid only for its recorded case.
See [independent rejection evidence](docs/evidence/doc-roy-yoshis-clock-validation-v1.json).
A subsequent valid original prefix identifies an audio DMA interrupt preempting
bookkeeping before the queue check; the delayed PAD processing is execution-time
dependent. The binder now rejects this incorrect periodic prediction before
packaging it. See [producer evidence](docs/evidence/original-audio-queue-delay-v1.json).

## Reference Capture roster unlocks

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
See the [upgrade procedure](docs/REFERENCE_CAPTURE_APP.md).

The physical Falco/Marth/Yoshi replay now matches every declared fighter field
across all 9,019 visible-browser ticks except 17 CPU-related input samples.
Four original fused operations in `HSD_MtxSRT` remove the tick-473 position
error; the first remaining core difference is CPU input at tick 901. Passive
original probes establish its RNG-seed and fighter-pointer register producers,
without using captured addresses as inputs or changing CPU integration. The
1,199-tick 2P and 4,346-tick 3P browser core/CPU regressions remain exact.
Camera, later subject rounding, draw scheduling and headless completion failures
remain open. See the [causal audit and scoped evidence](docs/PHYSICAL_REPLAY_SRT.md).
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
[application behavior](docs/REFERENCE_CAPTURE_APP.md).

Reference Capture 0.2.1 imports the pinned private prepared save into Application
Support and rejects external fixture locations before reading them. The original
fixture and existing capture bundles remain unchanged. Guarded environment
verification makes zero Documents-access attempts. One new operator-attested
four-stock Falco versus level-6 CPU Marth match on Yoshi's Story replays through
the preserved 0.2.0 tools after relocation: all 47,658 semantic events, 9,019
source ticks and 9,011 draws match through result and teardown. This is an
original-versus-original diagnostic pair, not port or performance acceptance.
Older recordings require their matching tooling identity; a changed capture
build is now rejected before launch. See the [bounded receipt](docs/evidence/reference-documents-migration-v1.json).

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
Dolphin replay. See the [bounded receipts](docs/evidence/reference-dolphin-replay-v1.json)
and [input-stream contract](reference-capture/dolphin/INPUT_STREAM.md).

The private [Reference Capture application](docs/REFERENCE_CAPTURE_APP.md)
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
see the [bounded receipt](docs/evidence/reference-operator-session-v1.json).
No gameplay fix or gold admission is claimed. PR #16 and its allocation evidence
remain unchanged and unmerged.

## Browser controllers

The browser controller candidate adds browser-standard mapping, a suggested
Mayflash 0079:1843 mapping for Chrome/macOS's 16-button/10-axis raw layout,
individual binding corrections, and complete setup for unknown layouts. The
suggestion adapts SDL definitions to the inspected USB descriptor and Chromium
indexing; digital L/R clicks remain provisional. Separate GameCube trigger
pressure/clicks and fast input-only browser regressions pass. The public player
defaults to controllers when recognized, with per-player keyboard overrides in
compact Controls settings. Session-local trigger-origin handling fixes activation
with the attached Mayflash's nonzero rest values. The user reports that the
physical Mayflash/OEM controller worked in the playable preview. PR #22 merged
as `72def75a7c58292b5f7f2d3b04f70ad21b665f33` after both exact-head native CI
runs and public checks passed. The fix is live at
[webmelee.gg](https://webmelee.gg/); its immutable production artifact is
[00ee2ca2.webmelee.pages.dev](https://00ee2ca2.webmelee.pages.dev). Hosted
artifact, disc/menu lifecycle, controller and recovery checks pass. Digital
L/R clicks, full physical gameplay and retail input precision remain unaccepted.

A separate follow-up shares the compact Controls component between the public
player and development `runtime.html`, including source choices, layout,
preferences, optional remapping and focus behavior. Development diagnostics and
audio remain separate.
See [controller scope and verification](docs/CONTROLLERS.md).

## Public runtime

The user-approved renderer startup fix is now live at
[webmelee.gg](https://webmelee.gg/). PR #18 merged with green exact-head CI;
the production artifact and apex each passed HTTP verification and ten real
browser UI checks. The runtime bytes match the accepted preview. This is a
functional alpha release, without a startup-speedup or expanded gameplay
acceptance claim. See the [production record](docs/RENDERER_PRODUCTION_PROMOTION.md).

A separate [loading-feedback preview](docs/PUBLIC_LOADING_FEEDBACK.md) adds
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
[failure evidence remains preserved](docs/SELECTIVE_PIPELINE_ALPHA.md).
The public build, 758-test suite (31 skips), and ten public UI checks pass.
An immutable noindex staging preview is available, but the startup-speedup
release remains **NO-GO**: one hosted cold pair measured 3075.557 ms disc-ready
against PR15's 3078.824 ms, and two hosted input-selection failures prevent full
hosted signoff. Production is unchanged. See the
[scoped fix and fast verification loop](docs/RENDERER_STARTUP_FIX.md).

The [source-address prerequisite](docs/SOURCE_ADDRESS_CONTEXT.md) models ordinary
original SDK/HSD allocation identity and defined register-byte consumption,
without supplying missing original allocation history or changing gameplay.
After a conflict-free normal merge of main, fresh complete visible CPU runs
retain exact 1,199-tick 2P and 4,346-tick 3P core/CPU comparisons against both
retail golds; the 3,838-tick 4P run still first differs at tick 2,495. Camera,
subject and extra-draw differences remain open. Five synthetic component tests,
Release browser/trace builds, and the 673-test local suite pass (46 optional
target/fixture skips); an added browser-failure-observer test passes separately.
The [reconciled evidence](docs/evidence/cpu-register-reconciled-v1.json) preserves
exact identities and failures. This is reusable groundwork, not a CPU fix or
performance/content admission.

The initial public alpha uses an explicitly **audio-disabled** release profile.
The public compile/link and JavaScript graph excludes the identified GPL-derived
resampler and coefficient generator; normal development audio remains separate.
The candidate mounts the player directly with the original minimal prototype
layout. It still reproduces the opcode-63 CPU-action abort, and the development
No Contest path can exit with unsupported pending scene 0. These are retained
failures. See [the alpha validation record](docs/PUBLIC_ALPHA_VALIDATION.md).
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
unchanged. See the [five-way investigation](docs/PUBLIC_PERFORMANCE_INVESTIGATION.md)
for failed prefixes, transition costs, exact profiles and the frozen candidate.

Ordinary VS CPU levels 1–9 run in the shared compiled runtime used by both
entry pages. The [CPU match development corpus](docs/CPU_MATCH_CORPUS.md) now
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
on Yoshi's Story, and Marth versus Mario on Dream Land. The complete
acceptance milestone below is still open:
source stage and item rendering, stock/respawn/outcome flow, and the optional
renderer cache are integrated, while clean cold-cache/full-match first-use
stalls, audible and physical controller verification, and full original-game
equivalence remain open. No
emulator is shipped; Dolphin is used only as a separate original-game reference.
The combined [performance and accuracy playbook](docs/PERFORMANCE_AND_ACCURACY.md)
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
coverage remain open. See the [evidence ledger](docs/REPLAY_CORPUS.md#bounded-browser-staging).

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
rumble archive in the standalone Battlefield test bundle. See the [expanded evidence ledger](docs/REPLAY_CORPUS.md#expanded-development-corpus--eight-game-regression-gate).

The [first measured replay cohort](docs/REPLAY_CORPUS.md) now contains three
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

The [complete-game replay calibration](docs/COMPLETE_REPLAY_CALIBRATION.md)
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

Slippi ingestion now normalizes finalized per-frame input/state records, handles
rollback history without mixing revisions, preserves exact field bits and
rejects incomplete timelines. The v2 input-only workload transport retains
source identity and records derived rules explicitly; it never treats UCF
post-frame observations as a vanilla oracle. The existing modern Fox/Falco
Battlefield fixture completes all 686 input frames and teardown in the source
runner. That is workload evidence, not browser performance or equivalence.

The first retail replay calibration now passes: two independent automated
Mario/Mario Final Destination captures repeat exactly for 240 neutral source
ticks, and the port matches their entry data, input vectors, declared fighter
fields, RNG and match clock. The v2 fixture additionally restores and compares
the semantic PAD configuration and all Master/Copy/Game histories on every tick.
A separate 240-tick retail camera-traversal audit finds no changes to these
declared fields during drawing for this neutral sequence. The calibration found
and fixed missing stage particle bank30
publication and the absent owned rumble data/interpreter boundary. Stage banks
30/64 share decoded assets; authored particle dependencies are checked before
entry. See the [capture procedure and evidence](docs/RETAIL_REPLAY_CAPTURE.md).
After these fixes, the visible Release Marth/Dream Land warm action inventory
completed all 46 cases and 6,168 source frames with zero hard-gate failures
(worst native callback 12.19 ms); an earlier incomplete sweep exposed a test
driver recovery gap at the original platform-edge teeter state, now corrected.
The modern Fox/Falco Battlefield donor now also has two independently repeatable
vanilla captures: all 686 source ticks consume the intended PAD vectors, and
both 686-draw audits leave the declared state unchanged. The port matches every
declared field after restoring the original MSL sine/cosine and arctangent
routines with their explicit fused-operation rounding. Two retained numerical
reds at air-dodge entry led to these shared fixes; no tolerance or per-frame
state correction was added. This is still **zero admitted gold Slippi fixtures**:
broad corpus/coverage and content admission remain open. The first donor now
also passes visible source-draw state comparison and Release cleared-origin/warm
performance checks, with zero hitches, audio underruns or live pipeline creation
over all 686 ticks. Worst native callbacks were 12.735/7.42 ms and match preparation
186.745/200.905 ms on an Apple M4, macOS 26.6.2, visible Chromium 152, 640×480
framebuffer. Driver caches were not controlled. The final pair had no live heap growth. An earlier warm run grew the Wasm heap
by 86,441,984 bytes without a timing failure; this remains measured memory work,
not a zero-allocation claim. That earlier calibration's seed contained 389 pipelines.
`check_browser_replay.py` independently joins recipe, state, timing and build
evidence while refusing a broader admission claim.

A successor public corpus audit supplies 1,005 UCF-off candidates on the current
fighter/stage surface. Independent full parsing verifies 76 complete source
identities, including 16 exact human P1/P2 games compatible with processed-v2
input export. Eight development games and two fresh whole-game holdouts are
reserved before runtime execution. These legacy recordings lack complete raw
controller samples; original vanilla captures must still establish expected
state. The earlier 952-file zero-UCF-off sample no longer constrains candidate
selection. See the [verified split and limitations](docs/REPLAY_CORPUS.md#independently-verified-ucf-off-candidate-split).

The UCF-off cohort now has all eight development donors executed; both fresh
holdouts remain unexecuted. The port supports their original
eight-minute stock countdown through the source timer and timeout paths; exact
donor settings and separate timer sidecars are mandatory gates. Reusable
construction-input calibration passes the production collector and original
reference controls. The first Fox/Falco Battlefield donor matches all 5,772
recorded ticks, ending on the last KO before the source ending; it remains a
bounded recording. Marth/Marth on Yoshi’s Story and Fox/Fox on Final Destination
match both original captures through complete endings of 6,525 and 7,481 ticks.

The first three workloads found shared matrix/vector lane-order, Dolphin Slash
rotation and linear-spline rounding differences. Their earlier ten-game source
regression and six cold/warm runs passed on the previous candidate; those reports
retain their original build identities. See the [timed cohort evidence](docs/REPLAY_CORPUS.md#final-timed-cohort-candidate-performance-gate).

The remaining five donors independently repeat their original setup and endings
and now match 20,915 exact visible source ticks on the final Release build. They
exposed and cover Counter's wind command, stage quake animation ownership,
camera descriptor addressing, and source tick/draw ordering. Fresh-reference
replay entry also rejects unknown prior heap history, which v2 recipes do not
encode. The earlier 13-game state regression still passes on the source-fix
build, for 71,735 unique visible ticks across 17 complete games and one bounded
recording. Together with three earlier controls, the final Release build matches 38,640
exact current-build ticks. Old reports are not reassigned to a new executable.
All 518 tests pass.

Profiling found content-dependent vertex-array registry scans and unnecessary
SDK heap walks during ownership checks. An exact live-owner array index and
cheap generation accessor remove those costs without changing source math or
draw order. The first five-game cold/warm sweep on that Release build passed
eight of ten runs; two intermittent frame-finalization spikes remain under
investigation. Expanded timing reproduced a third red, then four further
full-partition repetitions passed. Those passing retries do not close the reds.
The final ten cold/warm runs pass their scoped gates: worst native/browser
callbacks are 12.830 / 29.420 ms, with zero live pipelines, heap growth, timing
resumes or audio faults. Those measurements do not clear the earlier unexplained
reds. The reviewed seed contains 507 pipelines. See the [remaining-five evidence](docs/REPLAY_CORPUS.md#remaining-five-ucf-off-development-donors).
The candidate is not frozen; broader gold/content admission remains open.

The reusable [bounded hitch-capture loop](docs/HITCH_CAPTURE.md) now preserves
every abnormal callback, previous timing context and optional browser trace in
an immutable attempt ledger. Its first fixed twelve-attempt matrix is complete:
eight unprofiled attempts measured six native 16.67 ms deadline misses, including
one native 33.3 ms hard failure, and three separate browser hard gaps across
37,915 callbacks. Maxima are 94.115 ms native / 106.645 ms browser. Four profiled
attempts had no hitches and cannot close these failures. The cold red is in
begin-frame work; warm recording spikes repeat the earlier Dream Land source
frame 1746 and Yoshi source frame 279. SQLite/IDBFS sync during frame finalization is a concrete
code-path suspect, still awaiting event-level causal confirmation. All raw
reports and the unrelated favicon 404s that marked these attempts aborted are
retained. No source gameplay code or numerical behavior was changed for this
capture work. Both fresh holdouts stay unopened, followed by a separate required
whole-sequence reference/performance track for consecutive matches with retained
source heap state before any public 4×4 readiness claim.
All 550 regression tests and the affected Release build pass. Harness recovery,
trace loss, overflow and served-build checks are covered; this is validation of
the diagnostic loop, not resolution of the measured gameplay red.

The subsequent four-slot causal capture completed 18,960 development input ticks
with four complete traces and no retries. Warm Fox/Marth on Dream Land reproduced
callback 1,883/source frame 1,746: 20.580 ms native and a 37.610 ms browser gap.
Three actual cache `fsync` waits account for 14.280 ms within that callback;
correlated stacks show the renderer's SQLite transaction triggering an automatic
WAL checkpoint through Asyncify/IDBFS. This identifies an optional persistence
path to remove from live gameplay. The other three runs record focus loss;
none is acceptance evidence. The earlier 91.795 ms cold begin-frame stall remains
unresolved. The new instrumentation passes 552 tests and the Release build;
its frozen build, all failures and causal evidence are recorded in
[the hitch-capture notes](docs/HITCH_CAPTURE.md#causal-capture-results--2026-09-12).
The optional-cache fix is now implemented and verified on these two development
workloads: SQLite transactions remain queued during source ownership and drain
only after native teardown, with bounded coalescing and explicit save failures.
Both original A/B comparisons pass for 9,480 source ticks, including declared
state/RNG/PAD, timers, source draws and match completion. Both real exported
DB/WAL pairs pass integrity checks and reload. Four separate unprofiled cold/warm
runs pass across 18,960 source ticks / 18,961 callbacks: zero native 16.67 ms
misses, zero native 33.3 ms failures and zero browser 33.3 ms gaps. Worst native
callback is 12.845 ms; worst browser interval is 30.555 ms. A separate complete
profiled run records zero live cache syncs and a successful post-teardown flush.
All 554 tests and the Release build pass. See the [fix evidence](docs/HITCH_CAPTURE.md#deferred-cache-fix-and-verification--2026-09-12).
The earlier cold begin-frame red remains independently unresolved; these clean
runs do not classify it as external scheduling. Both fresh holdouts and the
subsequent retained-heap gate remain closed.

A four-slot fresh/reloaded-browser experiment now reproduces the remaining
cold failure: a 90.745 ms native callback includes 85.945 ms across 27 waits for
a staging buffer's GPU completion. There are zero CPU frame-slot waits or live
cache syncs. A 115.722 ms GPU-process task overlaps it, including a Dawn worker
with only 1.057 ms of thread CPU across 114.404 ms wall time. This localizes the
wait but does not identify the underlying GPU operation or establish external
scheduling. Two subsequent, separately bounded startup GPU traces do not
reproduce the stall; both experiments preserve a focus-loss failure as well.
All six diagnostic traces are complete for their declared windows. The capture
tool now supports a frozen ten-second GPU startup preset with deferred stream
reading. No gameplay or renderer implementation changed; the red remains
unresolved and both holdouts remain unopened. See the [results and next discrimination](docs/HITCH_CAPTURE.md#cold-begin-wait-and-gpu-startup-diagnosis--2026-09-12).

A subsequent fixed four-slot diagnostic-output painting experiment completes
15,568 source ticks/draws but does not reproduce the long GPU worker. It retains
43 native deadline misses, zero native hard failures, 28 browser hard gaps and
three focus-loss failures. Startup CPU traces implicate application work in
some smaller misses; they do not explain the old wait. A separate native GPU
profiled replay adds two deadline misses and one browser gap, with no focus
loss. The native recording's exported retention does not cover those failures,
despite successful attachment and file creation. The new canvas verifier's
logical/backing-size mistake is corrected; all original failed attempts remain
preserved. No performance fix or external-scheduling classification is claimed.
Both holdouts remain unopened. See the [experiment and coverage limits](docs/HITCH_CAPTURE.md#diagnostic-page-painting-and-native-gpu-capture--2026-09-12).

Scene preparation now waits nonblockingly for submitted GPU work to complete
before arming the source clock, preserving source ticks/draws and menu audio
ownership. A controlled delayed-completion test proves the old build started
source execution prematurely and the new build waits without extra draws.
Both full development replays still match both original references across
9,480 ticks, and all nine original menu/entry comparisons pass. All 558 tests
and the Release build pass. Four independent cold runs complete 18,960 ticks
with zero native 16.67 ms misses, native 33.3 ms failures or browser 33.3 ms
gaps; native maximum is 10.710 ms. Three pass fully; the fourth retains a
focus-loss failure. All four are already GPU-ready at their first arming poll,
so this protocol fix does not establish the cause of the old 115 ms GPU task.
That red stays unresolved and both holdouts stay unopened. See the
[readiness fix and bounded verification](docs/HITCH_CAPTURE.md#submitted-work-readiness--2026-09-12).

The typed content path now carries Falco, Fox, Marth, Battlefield, Yoshi's Story and Dream Land
source IDs, runtime manifests and focused development traces. Source stock icon IDs are
selected from the typed character/fighter identity rows. In a fresh Release
browser route, raw PAD input selected P1 Falco and P2 Mario in the original CSS,
selected Battlefield in the original SSS, and entered a four-stock match. The
rendered frame showed Falco, Mario, Battlefield geometry and the correct four
Falco and four Mario stock icons. A complete ordinary-input loop, uninterrupted
audio and retail-reference comparison have not passed, so the accepted
first-deliverable claim remains Mario on Final Destination. The integrated
source match trace completes Fox on Final Destination and Battlefield,
including all four costumes, laser, Reflector, Illusion and Fire Fox. A combined
Fox/Falco run proves shared effect-bank ownership. Yoshi's Story completes
repeated source lifetimes with four map objects, Randall state, moving collision
and real Shy Guy creation after the original 120-frame scheduler. A Release
browser route selected Fox in the original CSS, selected the upper Yoshi's Story
tile in the original SSS, and rendered the source Ready countdown with Fox stock
icons and Yoshi's Story geometry. The lower adjacent tile is Yoshi's Island and
remains unavailable. Uninterrupted audio and retail comparison for these
additions remain open.

Marth's exact 0x98-byte extension, five costumes, 327 actions, three authored
dynamic-bone chains, null Article table, two-entry effect table and source audio
bank are pinned by owned-asset tests. The integrated trace executes jab, Shield
Breaker, Dancing Blade, Dolphin Slash, Counter and an aerial across all admitted
stages and repeats teardown. Dream Land pins eight source map objects, its joint
and material animation consumers, collision, lights, shadows, flagged objects,
and exact bird/tree/wind/blink parameters. The Release browser selects this pair
through the original CSS/SSS and runs a versioned 46-case inventory covering
grounded normals, shield/roll/dodge/grab, aerials, twenty wavedashes and all four
Marth special families. See [Marth's notes](docs/MARTH_PORT_NOTES.md) and
[Dream Land's notes](docs/DREAM_LAND_PORT_NOTES.md).

The active first deliverable is **original in-game CSS → original in-game SSS
→ a playable four-stock Mario-versus-Mario match on Final Destination → original
CSS**, without a results screen. Real menu assets, original scene/input behavior,
transitions and repeat-match lifetime are required. The former HTML selector is
removed from the player flow. Full accuracy, physical controllers, sound and stable performance
remain required acceptance work under [the accuracy contract](docs/ACCURACY_CONTRACT.md).

## Active integration

The canonical `runtime.html` player uses original CSS/SSS assets, callbacks and
transitions. `native-menu.html` redirects to it; the asset inspector remains at
`viewer.html`. The runtime availability gate now includes Mario, Fox, Falco and
Marth, all of their source costumes, and Final Destination, Battlefield,
Yoshi's Story and Dream Land under the
four-stock, no-items, no-timer rules. Menu selection carries the complete typed
`StartMeleeData`, including source RNG and costume/tint settings, into the match.
The original menu preloader is not supplied by this resident-asset integration.

The match now calls original `fn_8016DCC0` for the supported start configuration
and the original VS `OnFrame`, replacing the prior end-only adapter. Original
stage markers are initialized before fighter creation. Original Entry, Ready/Go,
damage/stock/player-marker/magnifier interface, pause screen and pause camera,
No Contest, and the GAME!/Game Set ending run through scoped source owners.
The host returns to CSS only after the original exit request, skipping Results.
This does not establish full `gm_Scene_Vs_OnEnter` or retail scene-manager
equivalence across all modes.

The current source lifecycle trace passes two complete original CSS → SSS →
four-stock match → CSS cycles. It checks Ready/Go at **124 ticks**, blocked early
movement, original entry accessory lifetime, an in-range fireball whose damage
matches the HUD, three respawns and the winner, and **114 frozen ending ticks**.
It also passes source pause with frozen fighter state, wrong-player Start
rejection, same-player resume, original L+R+A+Start No Contest, SSS B cancellation
and re-entry, and unload at 0, 60 and 100 intro ticks. The source trace is
`work/native-active-telemetry-run.log`; these are shared-source lifecycle checks,
not original-game equivalence evidence.

The expanded native menu trace selects Mario/Falco through raw PAD, enters SSS,
and completes a four-stock match back to CSS twice on both Final Destination and
Battlefield. The content trace repeats all four stages with every Falco, Fox and
Marth costume. It exercises the Fox-family laser, Reflector, side-special,
up-special and complete jab/rapid-jab action families plus Marth's admitted
special/action set, pause and No Contest. It also checks Yoshi's Story's Randall/Shy Guy
lifecycle, Dream Land's eight-map lifecycle, and teardown before the next cycle.
Fox's Fire Fox launch now admits source command 38 through its recovered
seven-byte skip handler, and the trace requires the full charge, launch and exit
sequence rather than stopping at up-special entry.
These native traces establish the checked source lifecycle; they do not replace
browser input, audio or retail-reference evidence.

A versioned transition-equivalence harness now emits the same semantic JSONL
boundary records from the port and a read-only GALE01 revision-2 Dolphin
collector. It requires CSS → SSS, SSS B cancellation → CSS, a second CSS → SSS,
and match entry in exact order; compares all committed rules, four player records
and source RNG at every boundary; and rejects any retail HPS stop/start, AX driver initialization or
language-bank initialization before match entry. The port emitter passes its
audio-owner continuity invariants in two cycles on Final Destination and
Battlefield. A pinned four-stock Mario/Mario Final Destination retail capture
now passes the exact lifecycle and audio-continuity checks: both CSS/SSS trips
retain `menu01.hps` with zero audio restart/reinitialization calls, followed by
one owner change to `sp_end.hps` at match entry. The port now preserves the raw
menu payload through SSS exit and builds a separate match payload through the
original VS-entry operations. The deterministic FD replay matches retail's
rules, item mask, all player fields and RNG at all nine boundaries. See
[the transition gate](docs/TRANSITION_EQUIVALENCE.md).

Scene transitions now use an explicit preparation boundary for CSS, SSS and
match entry. The browser shows the phase and waits for the audio worklet's
disabled-state acknowledgement before owner/session construction; no source
simulation ticks or source draws run during the wait or construction phase.
The constructed scene is then drawn without source ticks until texture uploads
and Aurora's pipeline queue remain quiet for two callbacks. The live clock arms
on a later callback. Pipelines or uploads first encountered during a match enter
the same frozen render-preparation gate and resume automatically; unrelated clock
overruns retain the explicit hitch pause. Preparation time is reported separately,
and the scoped [scene-entry profile](work/scene-entry-profile.md) measured
106.555 ms for initial resource preparation, 83.140 ms for isolated SSS owner
construction and 160.100 ms for isolated match construction. Its 7,604 active
callbacks had a 24.200 ms worst callback, no callback above 33.3 ms and zero
audio underruns. This is scoped application/driver-cache evidence rather than
cold-cache or full-match acceptance. The subsequent MarioReady operation-55
representation defect is fixed, and the fresh native whole-match routes above
pass on both admitted stages.

The Release browser now bundles a verified Aurora seed from the original CSS,
SSS, Battlefield, Yoshi's Story, Dream Land, stock/respawn, Fox and Marth
first-use routes: one shader record and 344 pipeline descriptors. The seed
contains no disc assets.
Aurora merges it into the optional origin cache. Newly discovered pipelines
remain dirty until native teardown, when **Unload** or application reload
persists the database; IDBFS serialization no longer runs during gameplay.
On a cleared-origin run, first CSS entry changed from the
earlier 357.48 ms pipeline-settle sample to 22.31 ms preparation with zero
queued or created pipelines on first draw. With the expanded seed, CSS/SSS
again created no pipelines; Yoshi's Story match preparation measured 808.41 ms
(194.81 ms construction and 613.60 ms priming/scheduling), while the slowest
active callback was 16.89 ms and none exceeded 33.3 ms. The cold preparation
delay remains open. See [browser performance work](docs/PERFORMANCE.md).

The earlier representative Marth/Dream Land samples did not establish browser
admission. The new in-page `marth-visible-actions-v1` gate uses the strict raw-PAD
queue and expected source motion IDs for 46 cases: every normal and defense
option, all aerials, air dodge, ten wavedashes in both directions, all four Marth
special families, and a Dream Land scheduler window of at least 4,200 frames.
It exposed and fixed roll's checked source command opcode 31, recenters movement
through ordinary source input, and reserves 192 MiB of Wasm allocation headroom
while the source clock is stopped at match preparation so live play cannot pay
`memory.grow`.

The first cleared-origin Chrome capture failed with seven browser callback gaps
(worst 284.295 ms), 404 audio-underrun frames, live pipeline creation and timing
pauses while native callbacks remained below 17.64 ms. Eleven descriptors were
persisted only after visible source play and native teardown, increasing the
reviewed seed from 333 to 344 pipelines. After rebuilding and clearing the origin
cache, the exact 46-case run passed 6,070 source frames: worst browser interval
22.785 ms, worst native callback 19.38 ms, and zero callback gaps, long tasks,
native callbacks over 33.3 ms, audio underruns, live pipeline creation, automatic
pauses or focus loss. Wasm heap growth remains an explicit report field for
correlation with any timing failure. After teardown and a complete application
reload, the warm run passed the same 46 cases across 6,109 source frames with a
21.06 ms worst browser interval and 15.325 ms worst native callback; every hard
gate remained zero. This is browser performance evidence for
Marth versus Mario on Dream Land on the measured Chrome/macOS configuration;
retail equivalence remains limited to the separately named comparisons.

The final runner excludes recovery/recentering motions from the following
case's expected-motion check. A stricter supplemental warm run passed all 46
cases across 6,289 source frames with a 26.3 ms worst browser interval, 22.7 ms
worst native callback and every hard gate at zero. It recorded 113,508,352 bytes
of Wasm heap growth without a correlated callback, long-task or audio failure.

A correlated Battlefield cold-entry probe measured fresh application
construction at 182.380 ms, the preparation boundary at 200.095 ms and first
draw at 10.025 ms, with 399,360 texture-upload bytes and eight queued/five
created pipelines. A source-world reload in the same page measured
157.685/175.265/3.880 ms with no new uploads or pipelines; a full page reload
measured 160.485/178.040/4.870 ms and repeated the texture upload while retaining
the browser/driver pipeline cache. Diagnostics now distinguish application-state
reload from clearing the persisted renderer cache. Neither clears browser or
GPU-driver caches, so this explains the first-load shape without claiming a
clean machine/browser cold-cache pass.

The current Release browser separately passes original pause/resume and two
four-stock loops, each at **2,125 diagnostic input ticks**, with three respawns
and automatic return to CSS. Source pause was explicitly observed changing
from 0 to 1 and back to 0 before the first stock run. An earlier overlapping
Start/stock diagnostic attempt stayed source-paused and failed its tick bound;
it is not counted as a pass. Stock diagnostics now reject host/source pause,
Ready/ending transitions and queued Start input, preventing that overlap.
These browser runs use labeled raw-PAD diagnostics, not physical input.

Native descriptor support now retains original interleaved NBT geometry and
bump texture flags for the pause screen and common entry accessory. It uses the
original HSD/GX path, with unsupported forms still rejected. The common root16
accessory has a world-owned descriptor that outlives its original fighter/effect
consumers. Eye telemetry distinguishes an authored base image from an animated
table entry; this changes observation only, not the game's texture commands.

The current local regression suite passes **278 tests without skips** (265.418
seconds). Default and Release browser, native-menu, content-match, Battlefield,
player-context and fighter-runtime targets build.
The focused browser run used the Release build. The integrated change also
received a primary review of ownership, input handoff, construction, callback
timing and teardown paths.

Ordinary keyboard Start and SSS B cancellation work in the browser. Complete
ordinary keyboard play, physical-controller acceptance, original-game
visual/audio comparison and clean cold-cache/full-match timing acceptance remain
open. The inspected Falco/Battlefield match reported an active callback below
10 ms and none above 33.3 ms, with preparation reported separately; automation
disrupted that run and produced audio underruns, so it is not uninterrupted
performance or audio evidence. First-use graphics previously caused explicit
timing pauses. The render-preparation gate still needs a clean browser cold-entry
and death/respawn timing pass. The native player retains the strict pause policy
and nominal 60 Hz clock; neither establishes original timing
equivalence. Catch-up input
sampling and replacement DSP coefficients also remain open accuracy gaps under
[the accuracy contract](docs/ACCURACY_CONTRACT.md). The optional IDBFS cache saves
renderer resources after unload; disc assets remain local and unpersisted.

The bullets below include earlier checkpoints; their narrower evidence must not
be mistaken for newer browser or full-loop acceptance.

- Native-menu preparation now passes both complete scene tables: CSS nine and
  SSS twelve model groups with camera/lights/fog, material animation and original
  PObj morphing descriptors. Every referenced CPU morph scalar is compared bit
  for bit against the original big-endian array. Original HSD loading/animation
  and teardown pass across two SDK worlds. Original SIS loading/layout/release
  passes for all 85 CSS text entries, plus scaling, spacing and style-stack checks.
  The original SSS OnEnter, 120 neutral raw-PAD/scheduler/audio ticks and OnExit
  also pass twice with original menu music. This diagnostic does not render or
  confirm a stage; scene preloading remains an explicit unsupported path.
  Original card archive loading and camera/model/animation consumers also pass
  across two worlds with checked scene-heap ownership. The full regression suite
  passed 243 tests; subsequent focused lifetime/card checks and the Release build
  pass. Original CSS entry, 120 neutral raw-PAD/scheduler/audio ticks and exit
  now pass across two worlds as well. Its cursor code accesses declared source
  globals directly, removing a cross-global GameCube layout assumption. Original
  SSM bank startup, cancellation, menu-to-Mario-to-menu switches and actual PCM
  publication pass twice with all seven exact local bank paths. Source loading
  still owns queues, accounting and sample publication; host bridges provide
  checked bytes, native descriptor fields and scoped auxiliary storage. The full
  regression suite passed 245 tests before the new focused bank/CSS checks.
  That checkpoint did not yet render menus or enter a match; the later native
  preview and source-loop checks above extend it. The original scene preloader
  remains open. The HTML selector remains disposable scaffolding.
  Shared typed archive handles and a checked CSS/SSS lifecycle boundary are
  integrated. Texture indices are checked before original table access; unused
  authored values are preserved. The accuracy contract records catch-up
  input/audio, nominal cadence, approximate DSP coefficients and missing
  reference/device evidence as open gaps; no tournament-acceptance claim is made.

- Temporary browser selection scaffolding now exercises character selection →
  stage selection → original stock match → character selection, without a results
  screen. This is **not the original in-game CSS/SSS** and will be removed when
  those scenes, assets and behavior are ported. All 26 source character entries
  are unlocked; only Mario is available, with FD as the only stage. An immutable
  configuration boundary separates unlocks, availability and original source IDs.
  Real-browser checks passed one-stock launch, selection preservation, invalid
  stock rejection, stage back-navigation and repeated automatic outcome returns.
  The original stock diagnostic reached 1,985 ticks, three respawns and P2 winner
  on both cycles. Physical menu/controller acceptance is still pending.

- Render scale now controls the internal framebuffer through Aurora's VI API,
  independently of Retina presentation density. The former 1× window setting
  still incurred approximately 1280×960 internal rendering on this display;
  explicit 1× now reports 640×480 and 2× reports 1280×960. This reduces default
  pixel workload without changing the 60 Hz simulation. Long-frame diagnostics
  also defer formatting their history until the diagnostics panel is open.
  A warm 1× stock cycle measured 17.68 ms worst interval, zero intervals above
  33.3 ms and zero audio underruns. A final exact-2× stock cycle measured
  17.84 ms worst with zero long frames or underruns. These short warm runs do not
  establish a rendering-speed comparison. This does not resolve the user's reported
  175.14 ms worst interval / seven long frames in 3,207 frames, nor establish
  cold-cache performance. All 238 regression tests and the Release build pass.

- A fresh-origin stock run without persisted application pipelines passed at
  32.46 ms worst interval with no audio underruns; Cape, Super Jump Punch and
  Tornado also stayed below 33.3 ms. The browser driver cache was not reset.
  A longer run after fireballs reproduced four gaps (74.58 ms worst) around
  tick 3,740, with zero texture upload/new pipelines and 64 audio-underrun frames.
  The first prior frame spent 49.715 ms in end-frame; later gaps exceeded native
  work. No further long frames appeared through tick 21,188. This does not prove
  that first-use stalls are solved, and does not justify speculative texture
  warmup. An opt-in queue-completion sampler now distinguishes asynchronous
  queue/callback latency from native frame work; it is not GPU execution timing.
  Its isolated stock check passed at 1,985 ticks: 17.81 ms worst interval, zero
  long frames/underruns, and 33 successful queue samples (15.83 ms p95, 16.68 ms
  worst). Enable/disable, unload and restart discard stale samples correctly.
  All 237 regression tests and the Release browser build pass.

- Direct browser ISO/GCM/CISO import validates GALE01 revision 2 and the original
  executable, selects the English audio paths, extracts the font from that
  executable, and generates Dolphin's free replacement DSP coefficients.
  All fifteen inputs from the owned CISO match the existing local bundle byte for
  byte (13,068,490 bytes). No disc or extracted data is uploaded or persisted.
  Synthetic tests cover sparse blocks, malformed filesystem tables and bounded
  reads. Extracted-folder import is now a developer diagnostic. The real browser
  completed the 1,985-tick stock diagnostic (three respawns, P2 winner) after
  direct CISO loading: saved-cache worst interval 18.43 ms, no intervals above
  33.3 ms and no audio underruns. A rejected image disabled launch; reimporting
  the valid image restored launch and the ground/air fireball check dealt 6%.
  This validates import/restart, not cold-rendering acceptance. The complete
  regression suite passes 237 tests with no skips; the Release browser build passes.

- Normal keyboard play exposed an overly strict timing policy: more than eight
  elapsed source ticks forced a manual pause. Gameplay now retains bounded timing
  debt and runs at most eight original 60 Hz ticks per browser callback, catching
  up across callbacks. More than one second of debt still pauses explicitly.
  Browser audio re-primes during catch-up instead of accumulating late PCM; the
  source audio engine still processes every simulation tick. Repeated injected
  200 ms and 800 ms delays recover without manual resume or audio overflow.
  These are recovery checks, not evidence that rendering hitches are fixed.

- One shared asset/world owner now serves the browser and Node regression harness.
- Two original fighters run together. Raw PAD input uses the original HSD queue,
  clamp/scale processing, player activation and fighter processes. A 60-frame
  walking trace moves only the selected fighter and survives world restart.
- The browser imports fifteen local runtime files, starts/stops/restarts the world,
  and runs the original standard/fixed camera initialization and controller.
- All nine initial FD objects and native background graphs now run original
  Ground startup and rendering passes. The stage advances at tick1801 and passes
  3600 scheduled ticks and teardown in two worlds. A longer probe found that
  the first-animation lookup lost its result across longjmp. The corrected
  volatile pointer and typed callback restore the lookup. A terminal single-CON
  compatibility rule removes an original uninitialized visibility output while
  preserving the observed hidden result. Two worlds each pass 36,000 frames,
  every background state, cycle restart, archive immutability and teardown.
  Historical checkpoint: the release browser continued past 16,400 ticks after
  resuming a 150 ms interval pause at tick13,017. The initial 788×592 run had
  no interval above 33.3 ms through tick11,940; that earlier pause prevents it
  from serving as stable-60-fps evidence. The current saved-cache measurement
  below is the applicable timing result.
- Original SEM/synth/AX source produces SFX PCM with original SDK reverb/delay.
  The integrated HPS stream runs the original three-slot scheduler through the
  FD intro and loop, with identical complete PCM hashes across two 100-second
  runs. Browser AudioWorklet transport is connected with a 1536-sample prefill
  (one additional source-frame of latency) and per-frame transport gating.
  Saved-cache runs report zero audio underruns; audible output and
  physical-controller verification remain pending. Replacement DSP coefficients are
  identified explicitly; full hardware DSP equivalence is not established.
- Source action readiness now covers 160 Mario submotions. All47 common effect entries and both Mario effect entries are hydrated.
  Original jab, jump/landing, shield and damage traces pass across two worlds. Actual damage,
  stocks and shield values are exposed directly for observation.
- Original run-off, stock loss, respawn platform and grounded return pass in two
  worlds after hydrating common root8. The isolated edge probe omitted stage
  updates, leaving the collision generation counter unchanged and selecting the
  wrong source floor-check path. With full stage/camera startup, the precise
  run-off phase captured from the original falls continuously without regrabbing
  the floor. Regular post-respawn running also passes. The full-stage stock probe
  passes three respawns and the original elimination outcome in two worlds.
- Normal browser launch now reads the authored Final Destination player markers
  through the original Ground API: `(-60, 10, 0)` and `(60, 10, 0)`. The
  close-range `+/-20` positions remain explicit diagnostic fixtures.
- B-button failures reached missing Mario article publication and original item
  common-data/allocator startup. Ground/air B simulation now passes in two
  worlds. A subsequent browser-only particle crash exposed raw GameCube FIFO
  stores and a mismatched item material callback. Both are repaired: the rendered
  ground/air B sequence reaches impact. Correcting the shared reciprocal-square-root
  intrinsic restores fireball bounce velocity and P2 damage (6%); the full browser
  subsequently exposed a burn-effect table copied as two instead of five bones.
  All five are now owned; the browser ground/air B sequence completes twice with
  6% P2 damage, and restart/unload succeed.
- Corrected the source secondary-costume-color setting: its recovered function
  name says ControllerIndex, but gm_16AE supplies sub_color. Controller routing
  remains owned by the raw PAD queue.
- Original raw-input jab, shield, grab and all four throw directions pass in two
  worlds. Cross-fighter animation streams, row identities and commands remain
  owned after the source fighter store is destroyed. Importing the shared
  FTKIND_NONE part-remap row fixes the grab/throw crash without changing roster size.
- Sixteen additional raw-input move cases cover tilts, smashes, dash attack,
  all aerials, air dodge, rolls and spot dodge in two complete passes with a fresh
  full-stage world per case. Air dodge exposed missing original crowd-manager
  startup; match creation now owns that manager through match teardown.
- The release browser stock diagnostic uses raw movement/jump input and completes
  four stock losses, three respawns and the normal P2 winner message at tick1985.
  A cold run pauses during the first death/respawn rendering. Restarting with
  the same renderer resources completes the identical1985 ticks with no pauses,
  no intervals above33.3 ms and a worst interval of21.26 ms at640×480.
  The saved renderer-cache run completes the same 1,985 ticks, three respawns
  and four losses with a 22.23 ms worst interval and no interval above 33.3 ms.
  First-use rendering preparation remains open; warm timing is not cold acceptance.
- The renderer-only `/melee-render-cache` is optional IDBFS state. It is mounted
  separately from game files, saved after unload when the pipeline queue is idle,
  and loaded 81 cached pipelines before assets in the latest browser cycle. No
  game assets are persisted.
- Current saved-cache timing was captured in Chrome 152's IAB at DPR1 on the
  reference machine at 1280×960. The selectable 2× SDL render scale remains
  available for scale-specific checks.
- Earlier parallel checkpoints used GPT-5.6 Luna xhigh; the lead retained
  integration and difficult blockers.
- The integrated runtime is checkpointed on the private remote. A clean Ubuntu
  GitHub Actions run builds the browser runtime and passes the source/ABI checks.
- A 102-frame retail comparison of a full jump/landing matches source motion IDs,
  non-idle animation frames, and vertical-velocity bits on every frame. The
  original match counter removes duplicate scheduler samples. Retail uses the
  unlocked Stadium side platform; the port uses FD, so this is a scoped vertical
  movement comparison, not full match equivalence. A second retail capture repeats
  all 102 frames, and the updated intrinsic build still matches the comparison.
- A bounded four-stock Final Destination trace from the original GALE01 rev2
  collector uses four stocks, items NONE, Mario yellow at -60 and Mario red at
  +60. Across 541 samples (original game frames 12..552; port after 120 neutral
  settle ticks), the reusable comparison reports exact position, velocity,
  motion, ground/air, stock and damage bits for both fighters, including one
  loss and one grounded respawn. Animation idle phase and RNG are excluded from
  this bounded comparison; it is not a full-match equivalence claim. An extended
  754-sample recipe also matches all compared fields through a P1 jab and P2
  3% damage, including the exact action-entry and impact ticks.
- Local suite: 234 tests passed without skips after refreshing the affected
  source trace binaries. The strengthened projectile regression passes actual P2 impact,
  expiry and teardown in two worlds. All six ground/air side/up/down specials
  also pass source action entry, finite state and two-world teardown checks.
  The browser now also completes ground/air Cape, Super Jump Punch and Tornado
  action checks. First-use Cape and Tornado presentation intervals reach about
  55–58 ms; a saved-cache Cape run has no interval above 33.3 ms or audio
  underrun. Isolated warm Jump Punch, Tornado and fireball restarts each stay below
  19 ms worst interval and add no audio underruns; fireballs still deal 6% to P2.
  Visual effect fidelity and other-special opponent impacts remain separate checks.

## Earlier runtime foundation evidence

The real-data gate passes **four fighter lifecycles across two complete worlds**:

- Original Player_80031AD0 calls Fighter_Create with actual Mario assets.
- Original Fall/physics/collision settles into grounded Wait after 10 ticks.
- Every registered fighter process then runs for 120 neutral Wait ticks.
- Each eye changes image and palette seven times through original commands;
  both native texture tables and eight inserted metal DObjs remain valid.
- Original GObj disposal calls Fighter_Unload, clears player entities and camera
  subjects, and restores scoped player/PAD/RNG/counter state.
- Both SDK worlds shut down and restart. All seven retained DAT archives remain
  byte-identical; original reflection code mutates owned display-list copies.

These are source execution and ownership checks under Node/Wasm. They do not
establish original-game simulation equivalence, browser gameplay, sound output,
or full-game performance. See [reproduction and scope](docs/FIGHTER_RUNTIME.md).

Historical checkpoint: **192 tests passed with no skips**, the browser/gameplay
and full fighter targets built, and both gameplay and owned-asset fighter runners
passed. That suite count belongs to the earlier checkpoint. Current verification is
recorded in Active integration above.

## Runtime foundation

- Pinned Melee, Aurora and Emscripten; checked generated gameplay patches preserve
  the original int-bool ABI and packed flag aliases. Pristine dependencies stay intact.
- Actual full common initialization consumes owned supported PlCo roots, native
  material owners and stage lights. Missing roots retain explicit readiness.
- Typed Mario ftData, all action rows, independent source action-loader bindings,
  common attributes, hurtboxes, visibility tables, costume and metal graphs.
  Wait/Fall scripts use decoded native operands; unsupported scripts fail.
- Original item registration consumes the real ItCo.usd registry; Mario effect
  banks, native effect models/animation graphs, item materials and particle
  rendering load from the authored data. Ground/air fireball impact, expiry and
  restart are covered by browser and source checks.
- Final Destination uses real collision geometry, original priority-4 collision
  updates, light overrides, marker transforms, camera/blast bounds, ground
  parameters and full-stage render objects. The browser cycle includes all stage
  transitions and the normal authored player markers.
- Source camera subjects, player settings, PdPm bonus thresholds, original font
  atlas and scoped RNG/PAD state have explicit owners and restart checks.
- Native descriptor owners retain borrowed data through original destruction.
  Metal aliases are freed through the original ID allocator before world reset.
- A bounded owned I/O queue has copy/completion/cancellation tests. It is not yet
  wired into the original archive loader. Unavailable device services abort with
  named errors instead of reporting success.

## Browser foundation

The inspector renders original HSD transforms, skinning, materials, texture
layers and polygons through Aurora GX/WebGPU. The runtime now also renders the
authored Final Destination stage, Mario effects and ground/air fireballs through
the integrated stage and item services. Mario/Fox Wait and walking clips and the
bucket/fan/bullseye fixtures were visually checked in earlier milestones. The
static inspector still uses its own inspection cameras and lights.

Browser keyboard input reaches Aurora PAD with distinct raw/clamped diagnostics
and focus neutralization. Physical controllers remain untested. The runtime
fighter now uses this browser input/presentation path; visual correctness remains open.

## Next

Close the retained intermittent frame-finalization reds using the complete
submission timing now saved with replay evidence. Keep the eight development
donors as regressions; freeze a candidate before executing either fresh holdout.
Passing development repeats alone do not admit gold or clear an unexplained red.
Ordinary controller/menu acceptance, visual and audio comparisons, input/audio
latency, and arbitrary prior-match context remain separate work. Broaden the
roster only after the reusable lifecycle and accuracy boundaries are verified.
See [the acceptance roadmap](docs/ROADMAP.md).
