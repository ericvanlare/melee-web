# Versus return loop

[Issue #34](https://github.com/ericvanlare/melee-web/issues/34) extends the old
Results-skipping playable slice. Its acceptance remains open: the original
Results scene, whole-session retail comparison and retained source-heap
equivalence are separate from the keyboard crash repair below. The owner has
accepted keyboard-only input validation for this PR; physical-controller
hardware acceptance remains separate under #35.

## No Contest input ownership, 2026-09-19

**Source identified / browser exercised.** A fresh Release build from
`7d02d4c9bb12b7aa66430d8a019045dc46470b35` reproduces
`CSS exited with unsupported pending scene 0` in visible Chrome
153.0.8010.48 on macOS ARM64. The ordinary development UI imports an owned USA
revision-2 CISO, selects B0XX keyboard P1 with a CPU opponent, and enters
Mario/Final Destination through original CSS and SSS. Start pauses the source
match; after its debounce, L+R+A+Start exits No Contest and fails on CSS return.
The keyboard mapping is Start=`7`, L=`q`, R=`9`, A=`m`. No raw-PAD diagnostic
or source selection write is used.

The original `gm_801A4D34` flushes the PAD queue at scene entry while retaining
HSD input history. The host instead reset all three HSD history banks on each
entry. A held quitting chord consequently became a new press in CSS.
`mn_8022F218` detects the newly triggered L+R+Start combination;
`mnCharSel_OnFrame` requests `GM_MENU` without setting its ordinary pending
CSS/SSS field. The pending value `0` was the downstream symptom.

The host now retains typed PAD configuration and Master/Copy/Game histories
across CSS, SSS and match ownership changes. New owners retain their own queue
and rumble pointers; external contexts still restore when an owner closes.
The final match history is captured before teardown restores the external
context. This preserves held/released edges instead of masking shortcut keys.

**Native traced.** The linked PAD regression runs the original HSD renewal,
controller map and CSS shortcut detector. Its reset-history negative control
produces the false edge; retaining the captured histories prevents it while a
real release/repress still triggers. The real-asset menu-host trace also passes
on stage kinds 32 and 31 (Final Destination and Battlefield), including the
first held CSS sample, subsequent release and a new CSS/SSS match selection.
The linked PAD regression is required in the existing Linux fighter CI job.

The input-fix version of `tests/versus_return_browser_test.mjs` exercises three successive ordinary-input
matches in one document. All three pause/LRAS returns pass, and the second and
third match start successfully. The first quitting chord is held for 120 ms;
the other two remain held for 1,000 ms through the return. CSS remains stable
after release. The harness preserves a failure, never retries a preparation or
automatically resumes a timing pause, and takes its screenshot after the route.

The initial browser attempt failed before CSS: the imported pipeline seed was
still draining when disc import hit its renderer timeout. That failure is
retained. The next attempt explicitly observed the existing renderer-idle
signal before import and reproduced the input crash. This harness sequencing
change does not relax a runtime timeout or establish a performance pass.

Local evidence remains in ignored `work/issue34/`:

| Artifact | SHA-256 |
| --- | --- |
| Initial preparation failure, `repro-1/report.json` | `bbdd57a9559e29a7c7c2c741dbee6bd8181a93f3261f28304dee0af95d8cea5c` |
| Original No Contest failure, `repro-2/report.json` | `bfa5ae2992fafa826519e2ec7e4e712317bfdd3642a908a654a3920a7cee570b` |
| Three repaired returns, `three-returns/report.json` | `0f5b6706dc19f3bdf275d1a6c0e3f51f6da59da839ccba996d8fb1ca3168eb78` |
| Baseline Wasm | `7a03f99991e4c4082b5b499ee06db7ecf9c447c52d3b53eed26bd23a34c44f26` |
| Input-fix Wasm | `0bdc2f2f559c3b5ed68e7327c6da7d99ef14ea03dd6c37ac316a111546823f9f` |

The CSS allocator snapshots after the three returns contain 313,468,104,
314,554,440 and 314,558,880 live bytes. Linear memory remains 400,949,248 bytes
after the first match. These observations retain the first-use growth and the
4,440-byte final increment; three samples do not prove a leak bound. The source
SDK world is still reconstructed between scenes, and these runs do not certify
retained original heap history, retail equivalence, Results, physical-controller
behavior, audio equivalence or timing. The separate [GPU/holdout gate](
https://github.com/ericvanlare/melee-web/issues/33) remains required before the
whole-loop cold/warm acceptance measurements.

## Retained source arena, 2026-09-19

**Native traced / browser exercised.** The ordinary player now owns one 32 MiB
source backing allocation from import through the successive CSS/SSS/match
worlds. Teardown still destroys the original objects and SDK heap. The next
world recreates the heap in the same arena, retaining payload bytes; it does
not zero the arena or reuse mutable scene objects. Final unload releases the
allocation. Existing isolated replay recipes retain their fresh-heap scope.

The native allocator regression writes through the original SDK allocator,
destroys/recreates its heap, and observes the retained bytes. It also rejects
size changes, foreign SDK ownership and final release while a world is live.
The native menu trace retains the same arena across CSS/SSS, No Contest and
normal four-stock completion, including the next Mario/Falco setup.

A separate visible Chrome 153.0.8010.48 run completes three ordinary-keyboard
Mario/FD No Contest returns in one document. All six CSS snapshots report the
same allocation identity and generation, and 33,554,432 source bytes. The
post-match live allocator totals are 313,461,296, 314,526,248 and 314,547,576
bytes; Wasm memory remains 400,949,248 bytes after the first match. This retains
the 21,328-byte final increment and does not establish a leak bound or retail
allocator equivalence. The original Results scene and whole-session comparison
are not exercised by this browser run.

| Local artifact under `work/issue34/` | SHA-256 |
| --- | --- |
| `retained-arena-three-returns-1/report.json` | `6b23fcc8727c1ab7c42016922cb68621970f91aa13c4c47ef222a37d39750581` |
| `retained-arena/gameplay_menu_browser.wasm` | `b60009ae2a430dae0ae421f02716233da4ad5e8f0b12b3dcea3c811dda667147` |

The native ownership evidence is in `session-retention-trace.log` and
`retained-menu-native-fd.log`. The browser bundle is frozen independently of
the subsequent Results implementation work.

## Original Results construction checkpoint, 2026-09-19

**Source identified / native traced.** The frozen checkpoint's explicit
`results-mario-v1` native recipe enters the original Results scene with complete source
`MatchExitInfo` for both No Contest and normal four-stock elimination. It runs
the original Results GObj processes, demo fighters and audio requests, confirms
with Start, tears down, returns to CSS and constructs another match in the same
source arena. This is a scene smoke: that version invokes Results explicitly;
the VS mode's routing/profile callbacks, source draws, browser presentation and
retail comparison are not established by it.

The scene consumes owned US Results/SIS/trophy archives and Mario's authored
nested demo animations. Original item/effect/fighter initialization remains in
the scene. Prepared native descriptors are published at the matching source
loading boundaries. The original dummy stage's default collision map and GObj
now share the existing collision destructor, so their arrays and islands are
released before another match constructs collision.

Portability repairs replace linker-adjacent Results globals with one typed
aggregate, use the actual character/camera table owners, unpack authored
halfwords in GameCube order, and preserve the Shift-JIS bytes of packed score
strings. The camera helper's recovered return values follow the owned DOL's
two branches: the primary camera for losers and the second camera for winners.
Focused tests execute the actual table initializer and camera arithmetic;
rendering callbacks are outside that test's scope.

Failures remain under ignored `work/issue34/`: missing US scene/trophy/item
roots, a duplicate font owner, fighter-cache reset ordering, the harness's
incorrect A confirmation, a random SSS hover assumption, and the dummy collision
lifetime failure. The next experiment follows each identified boundary; these
failures are not counted as passing runs. The final source smoke is
`results-source-entry-10.log`; its build and receipt are frozen in
`results-native-checkpoint-1/`. The prepared item/effect ownership test also
constructs and closes twice (`results-layout-item-prepared-2.log`).

No Results pixels, PCM equivalence, physical-controller route, whole-session
retail equivalence, live allocation bound or cold/warm performance acceptance
is claimed. The separately frozen browser evidence above still skips Results.

## Original Results rendering and mode routing checkpoint

**Source identified / browser exercised.** A separately frozen Release build
now enters the original No Contest Results screen through ordinary keyboard
CSS, SSS and Mario/Final Destination gameplay in Chrome 153.0.8010.48 on macOS
ARM64. The original text, player panels and Mario demo animations render, and
the scene reaches 316 source ticks. One explicit diagnostic resume follows a
timing pause at Results tick 163. This run stops in Results, before confirmation;
it does not establish a return to CSS, pixel/audio equivalence or performance.
An earlier attempt is retained separately: it reached Results and submitted
306 draws, but the frontend did not recognize phase 8 and its test timed out.
The frontend now treats Results as an active scene for pause/resume and import
ownership.

The browser retains the same 33,554,432-byte source allocation through CSS,
match and Results. Live allocator bytes rise from 293,895,464 on CSS to
349,596,896 on Results; Wasm capacity reaches 400,949,248 bytes. The final
sample is still Results, despite the reused harness's `css-after-match-1`
label. The immutable `scope-correction.json` records that error and binds the
original report and screenshot. These measurements do not bound repeated
Results teardown or live allocations.

**Native traced.** Original VS mode callbacks now produce `ResultsMatchInfo`
from the complete source `MatchExitInfo`, update KO/session state, and select
the destination after the Results scene's exit callback while its source
world remains live. The first actual mode-exit experiment fails with
`Original Results requested unsupported state 192`. This is a legitimate
Prize request: the synthetic profile sets all character/stage unlock bits
without a coherent save, challenge history or trophy state. The original
challenge check first queues achievement `0x1b`; the all-character trophy
check supplies another pending Prize. No pending bits are cleared to force
CSS. The earlier `results-source-entry-10.log` scene smoke did not execute
these mode callbacks and cannot establish that this boundary passes.

**Assets decoded.** The eight development fighters expose 73 authored Results
clips. Their exact demo-table extents come from `ftData_UnkIntPairs`; the
selected Win/Selected/Lose clips are validated against their actual nested
archive bounds. Falco's authored 0x9c1e-byte animation required removing the
old 32 KiB assumption. Fighter roots use the existing
`lbArchive_InitializeDAT` external-chain nulling policy, including Link's
optional Hookshot material/shape animation references. Results motion archives
retain strict external-link rejection. Neither change grants per-asset waivers.

**Native traced.** All sixteen real-asset Results constructions now pass:
each of the eight fighters wins and loses against Mario, advances two neutral
source ticks, and tears down in one retained source arena. The first attempt
stopped at Link's original `OnLoad` auxiliary action request. The web action
adapter had dropped `ftData_80085E50`'s original `msid < x58C` guard, attempting
action 72 in the 14-row demo domain. Both original action loaders now retain
their source-count guards, including the auxiliary null return and unchanged
cached state. The linked regression covers those behaviors and the primary
loader's cross-fighter source count. The passing scene test is
`results-eight-scenes-2.log`; its executables are frozen in
`results-native-checkpoint-2/`. This two-tick gate does not establish every
demo animation, complete confirmation, rendering or retail equivalence.

The final local regression run passes 1,029 tests with 48 optional skips
(`full-suite-results-route-4.log`, 214.035 seconds). The rebuilt Release browser,
native Results/menu targets and passive Dolphin observer compile. The existing
Results-skipping Mario/Falco four-stock source loop still passes twice
(`shared-item-existing-loop-2.log`). The earlier full-suite failure from the
stale 102-file manifest assertion is retained; the updated check explicitly
covers all fifteen added Results files. No CI runner platform changes are made.

| Local artifact under `work/issue34/` | SHA-256 |
| --- | --- |
| `results-browser-render-2/report.json` | `47dcfd0885f082c703d10456f87ab3bf8c0998d859ce7e54424bf9cf996ac64c` |
| `results-browser-render-2/results.png` | `2f2eb99420e8276a55e40f9b05b17cd3a7e74ee092c2caa5914423360cada7f5` |
| `results-browser-render-2/scope-correction.json` | `32dd871b203e9805f4bd6f015b27e87582d8b74b5ca94a302eb991cf5c90e719` |
| `results-mode-route-1.log` | `b800b685be7c3e5e7e45ddc73f798bc0da66f137cb713e71c450f790d54f21b7` |
| `results-eight-assets-4.log` | `56eab424ad7ba70c249eb65fefa5288bde84cc01e59186eee1e82fa64f094537` |
| `results-eight-scenes-2.log` | `7a20f98c0a5592c8369500c12c95f1add9b97e6e8ff8f05eab356b3a730d317f` |

The current browser loop test expects the original Results confirmation before
CSS and preserves any failure. Its earlier Results-skipping success is not a
pass for this new route. Profile/Prize ownership, real physical input, two
repeatable original captures per named three-match sequence, retained-history
comparison and memory bounds remain open. The experimental passive observer
now emits repeated-match and Results-process boundaries, but still lacks the
capture identity, transition-trace join and menu audio epoch required to
promote a complete reference bundle. Final cold/warm acceptance also depends
on the separate issue #33 gate.


## Profile and Prize integration, 2026-09-20

The branch includes current main's completed #33 runtime gate. That gate does
not establish whole-loop performance; new cold/warm consecutive-match runs are
still required. Keyboard-only testing is authorized for this PR. Missing
physical-controller hardware is not a merge prerequisite.

**Source identified.** A fresh owned original-game save was generated through
ordinary Dolphin Pipe input and normal memory-card saving, then decoded with
the strict HSD checksum/layout checks. The corrected, explicitly versioned
`fresh-all-characters-four-stages-v2` derivative changes only the authored
character and stage unlock masks: `0x07ff` and `0x01c0`. Battlefield, Final
Destination and Dream Land use stage unlock bits 6, 7 and 8; Yoshi's Story is a
default stage. Trophy/achievement pending and claimed state is not invented or
cleared. The old private fixture is unchanged. The local derivative's GCI
SHA-256 is `39171db67ec4e6b85837107f7cd7e1231402e239ec7f74bfc9efa35d5a2879ce`;
its SRAM SHA-256 is
`49dee06d38cb76aaa8f90bfb2e937face180243d1a170078b4e13226b012f82c`.
The original-created save has two checksum-valid GameData copies (physical
blocks 1 and 10); the original reader can select the later copy. Both are
updated in v2. The retained v1 derivatives changed only block 1 and are rejected
as comparison context. Their immutable manifests also incorrectly described
block 10 as invalid. The correction receipt at
`work/issue34/fresh-reference-v1/baselines/v1-correction.json` has SHA-256
`12a4de3f72fd0c743ec03bc65115aaf7c718e630f1caf211802eec7968f3cd44`.
Original load verification remains required. This establishes the prepared
bytes, not repeatable sequence capture or agreement with the browser profile.

**Native traced.** The expanded Results scene check passes all sixteen cases:
each of the eight integrated fighters winning and losing against Mario. Each
case runs original processes/audio requests through ordinary Start confirmation
and scene teardown in one retained source arena. MatchEnd inputs are synthesized
for this focused scene test. It does not run the original VS mode routing,
source draws, browser rendering or retail/PCM comparison. The immutable
`work/issue34/results-native-checkpoint-3/receipt.json` has SHA-256
`eafb25ce24f029127f7b8608e90dc00c98cfdbe3bb4fb3cbed0db5aaa84f9c31`;
`results-eight-confirm-1.log` has SHA-256
`d19b3d9d01f1904d41ba99624ec738a49b1108312c789e037897484e8cc0ed33`.

**Compiled / native traced at the asset boundary.** Prize has a checked owner
for the exact original SceneDesc, SIS, trophy and card roots. The integrated
real-asset check validates those roots and closes after the source world. The
first integrated mode-route run reaches Prize and then rejects a missing HPS
registry in `un_802FE918`. That failure is retained in
`work/issue34/prize-mode-route-2.log`. The checked registry now owns all three
authored `s_info` streams. The following native run completes original Prize
confirmation, returns to CSS, starts another match, finishes all four stocks
and confirms Results back to CSS. Its log,
`work/issue34/prize-mode-route-3.log`, has SHA-256
`c658ef9368b6266f71cddc6052f5b7fc760aee022a377f30a21ec55aa9c61b38`.
This run still used the earlier synthetic profile; it does not validate the
new profile initializer, source draws, browser route, retail agreement or
performance. Those gates remain open.


**Native traced, initialized profile.** The follow-up profile test links the
actual original `gmMainLib_8015F600` routines and verifies source preference
initialization, Toy-table mutation and restoration of the complete owned
profile/Toy backing. The native mode loop also passes with this initializer:
No Contest → Results → Prize → CSS, then another full four-stock match →
Results → CSS. The immutable checkpoint at
`work/issue34/profile-native-checkpoint-1/receipt.json` has SHA-256
`411758887283a197cc8840adad26391669b08d18f77ff2e2eced98401a52331c`.
It preserves the binaries and both logs. Its source hashes were sampled after
later edits and do not establish the exact producer source closure. This
checkpoint predates the typed seven-bank name-data repair and includes no source
draws, browser, retail, memory-bound or performance acceptance.


**Browser exercised, keyboard No Contest.** The initialized-profile Release
build completes three Mario/Final Destination No Contest matches in one visible
Chrome 153.0.8010.48 document. Each returns through original Results to CSS;
the first also confirms original Prize. Matches two and three hold LRAS across
the ownership change. The same 32 MiB source backing identity/generation is
retained throughout. This functional run deliberately pauses once for a Results
screenshot and makes no performance, retail, pixel or PCM-equivalence claim.

The immutable build receipt at
`work/issue34/profile-browser-checkpoint-1/receipt.json` has SHA-256
`bf66bb37c4d8a11e9bb8d32dac51ec924f474cbe182843394bf221ca15b8033c`;
the successful `work/issue34/profile-browser-return-2/report.json` has SHA-256
`fb286ce33b7ea139378025a39ae3df76198ec7e4bd8b020db87dda9d3ab766c2`.
The preceding `profile-browser-return-1` report is a retained harness failure:
a single Start entered the original statistics panels but did not confirm the
human panel. The scene continued running without a reported runtime error.
The corrected script supplies distinct bounded Start presses, as the native
source trace already did. This checkpoint predates the typed seven-bank
name-data repair and the expanded owner-teardown memory observations.

**Compiled / regression checked.** The complete local suite passes 1,091 tests
with 49 optional skips (`work/issue34/full-regression-3.log`, SHA-256
`0f4cbda37b1b6da923b986842fe8cda04183208143f8da8670ee973180e3a327`).
The preceding run is retained: discovery read the new sequence test while it
was being written and reported a syntax error. The settled module and complete
rerun pass. A subsequent focused source test verifies that profile activation
restores both original language settings, including an early unload before menu
entry (`profile-source-language-1.log`, SHA-256
`91145d3277adc41e111aa653f8d3dc0b3b3b07d74affb2401eb09e57eb5eb582`).
The Release public build and its audio/export checks also pass; its identity
receipt has SHA-256
`af00070d2b2dd7a6b188fad16937b10d9bf1cccf02cd6c93fca739f40b5d6a33`.
These checks do not establish browser sequence or retail acceptance.

**Browser exercised, normal stock completion.** The named `repeatMario`
inventory completes three Mario/Mario Final Destination matches in visible
Chrome 153.0.8010.48, using the ordinary two-player keyboard layout. Each match
observes P1 stocks `4 → 3 → 2 → 1 → 0`, three respawns, original MatchEnd's
P2 winner, Results confirmation and return to CSS. The first match also visits
Prize. There is no reload or source-state injection between matches.

The frozen Release build is `profile-browser-checkpoint-3/receipt.json`
(SHA-256 `5b94f9ef27d1022fd8c25abc706431c205361d52155bcd1e361996fce14bf4b2`).
The report is `sequence-browser-repeat-4/report.json`
(SHA-256 `27f9e0db1a0646d7ae49f49903b2836d0eb8f383df2042f315a623d7d56cb5ec`),
both under `work/issue34/`. Its sequence and teardown assertions pass, but the
command's final unload assertion fails because the harness expected numeric
`0` for the boolean `source_session_owned`. The retained unload observation is
`false`, with zero arena identity, generation, bytes, source world, objects and
processes. The assertion is corrected without rewriting that report.

All 28 owner observations retain the scene boundaries. Every completed source
world teardown has zero objects and processes. One 32 MiB arena serves all three
matches. Post-return CSS live allocations are 323,349,488, 323,351,208 and
323,351,096 bytes; cached archive/audio-bank counts stay at 21/13, and Wasm
capacity stays at 400,949,248 bytes after the first match. These samples show no
growth from match two to three; broader sequence bounds remain unestablished.

The earlier `sequence-browser-repeat-1` through `-3` attempts remain failures:
Start during the original CSS entry debounce; a stage tile ID mistaken for a
controller port; and a winner assertion before source MatchEnd publishes its
ranking at close. The final driver waits for the source's terminal ranking and
uses source cursor geometry only for observation. This is functional route and
lifecycle evidence, not retail, pixel, PCM or cold/warm performance acceptance.

**Browser exercised, rotation; unload failure retained.** The corrected `rotate`
inventory completes Dr. Mario/Falco on Battlefield, Fox/Roy on Dream Land,
Marth/Link on Final Destination, then Young Link/Link on Yoshi's Story. Every
match observes four stock losses, three respawns, the original P2 winner and
Results confirmation back to CSS. Together with `repeatMario`, this exercises
the eight development fighters and four stages in the declared pairs. It does
not establish other pairings or costume permutations.

The checkpoint-3 run `sequence-browser-rotate-2/report.json` has SHA-256
`b2548a138e79c22c5a1199b8637babdc1e54e24da364e978b53d52292c370816`.
All four match routes and their owner teardown checks pass; final unload from
CSS aborts when original CSS exit requests `/audio/us/clink.ssm`. The menu owner
registered thirteen resident banks and omitted Link and Young Link, although
the gameplay importer already owned their bytes. Both banks are now required
and registered by the menu owner, with existing registry ordering preserved.
This retained run remains a failed command until a new browser run verifies
the corrected final unload.

The preceding `sequence-browser-rotate-1/report.json` has SHA-256
`291a14c1438a12f48f58ab2465d7b2d8bc069c198ed0cc3e47911c87bd9cfa4b`.
It completes three matches and then times out in SSS because the inventory
mistakenly requests stage 16 (Yoshi's Island). Source `St_Kind_Story` is 8.
The original inventory is retained alongside that report as `inventory-used.json`;
the next run uses the corrected predeclared inventory. Neither failed run is
retail, pixel, PCM or performance acceptance.

**Browser exercised, corrected rotation and unload.** The following frozen
Release build (`profile-browser-checkpoint-4/receipt.json`, SHA-256
`a99a1a0f57820ee3c8034a916d96eddb18e2cde4e71bc351583cf7f00b47520e`)
registers both missing banks. `sequence-browser-rotate-3/report.json` has SHA-256
`6cad06ca5f865e37f5b0a3a35a357d43e9e7e3d933d4b55d522c008e90ddc56d`.
The complete four-match inventory and final unload pass, with no console or
page errors. All 36 owner observations fit the 128-event bound. Every completed
source-world teardown has zero live objects and processes; final unload also
zeros the retained arena identity, generation and allocation bytes.

The finite inventory retains 39 immutable archive entries and 15 decoded audio
banks after unload. Those caches belong to the browser runtime and intentionally
outlive the source scene owners. Final allocator live bytes are 296,214,176;
Wasm capacity remains 400,949,248 bytes. These allocator and cache values are
different measures from source-world liveness and the released 32 MiB source
arena. This establishes the named four-match lifecycle, not unrestricted leak
freedom or a retail/performance comparison.

**Original source observed, startup context only.** A visible original-game boot
from the corrected v2 save reaches CSS through ordinary controller Pipe input.
The passive first-CSS row observes character mask `07ff`, stage mask `01c0` and
RNG `f1415e10`; the screenshot shows the unlocked roster. Startup presents the
all-characters message and Kirby Hat 4/5 trophies, each confirmed through A.
The retained `original-visual-startup-1/receipt.json` has SHA-256
`aa2898d4f2b12ede3e70f32d10d8b3160b02e89a484f2d60abb564eb58af5867`.
Its binary identity is
`40fc32dd292ecdaf57ec0d18aff48b89328fbae59ebefb8a0ec3aadde699cdd1`,
bound by `reference-observer-checkpoint-v4/reference-dolphin-build.json`.

This is a diagnostic stopped at CSS, not a whole-sequence reference. P2's Pipe
qualifier incorrectly used ID 1; Dolphin assigns IDs separately for each device
name, so both distinct Pipe names need ID 0. P1 input is observed at the source.
The startup observer also omitted Challenger's `onExitPrize` at `801bff7c`;
the post-match VS Prize exit hook at `801a6308` does not run on that route.
Both defects are reduced before another original capture. The preceding private
boot helper had a separate neutral-axis conversion defect and cannot serve as
a correct input recipe; its available raw attempts remain retained.

The loaded masks are now verified, but the browser and original first-CSS
profiles are not yet equivalent: the original has consumed startup Prize and
loads its saved profile, while the browser initializes a source default profile
and reaches its Prize route after the first match. This difference must be
resolved explicitly before claiming whole-sequence agreement.

**Regression checked.** `full-regression-4.log` passes 1,099 tests with 49 optional
skips (SHA-256
`f2310ae71ff24ecd4ad23c9a3b2e094538b6a8cb3d0ae1b5288e6a73dc7db0a9`).
Later observer/scheduler corrections still require their own focused checks and
the final integration suite.


### First whole-loop timing failure, 2026-09-20

**Performance measured, failed.** The first reserved `repeatMario` cold slot
uses `profile-browser-checkpoint-5/receipt.json` (SHA-256
`cec33338a21299973e4a93b1f295f7da925f4c14b97c898e4eeb6368e8ff3958`)
and the immutable `versus-timing-plan-1/repeatMario.json`. The report is
`sequence-timing-repeatMario-cold-1/report.json` (SHA-256
`c809d6520845f8c1a638185a79b0e47582e163e52db5ed97f396cb10a104ec02`),
all under `work/issue34/`. It reaches the first original Results after four
stock losses and three respawns, then stops at Results source frame 163 with
a native timing pause. No resume or replacement attempt occurred. The warm
slot is ineligible because the cold route and cache-saving unload did not
complete; all rotation timing slots remain unstarted.

The three native deadline violations are 82.895 ms in initial CSS, 31.655 ms
near Results entry, and 522.645 ms in Results. Their staging-slot waits are
80.745, 28.845 and 519.340 ms. Corresponding browser gaps are 96.630, 45.270 and
535.620 ms. Sixteen pipelines are created live in Results: four on its first
source draw and twelve two callbacks before the final stall. This is missing
pipeline coverage; temporal proximity alone does not prove why GPU completion
waited. Results preparation invokes zero source draws by design because its
source camera callbacks are not assumed pure. Do not fix the gap by advancing
hidden gameplay or adding unverified extra source traversals.

The report retains 2,718 source steps/draws, three native target violations,
two native hard violations, three browser hard gaps and 171 audio underruns.
Its 6,416 native callback count includes the paused wait after failure and is
not an active-play denominator. Focus remained present and the page visible;
no console/page error was recorded. The sole 118 ms browser long task falls
wholly inside declared Results construction/preparation. These failed
measurements remain separate from functional keyboard route evidence.

The pushed audio-bank fix at `0b97a81` passes Linux Verify run `35542790426`.
The corrected private reference observer v6 passes 41 focused checks; its
binary SHA-256 is
`fa65f028b7703985515831409a9d6e8d9ff9e25feb1fb6357f8b4f9ed262bcd6`,
with receipt `reference-observer-checkpoint-v6/reference-dolphin-build.json`.
The earlier v5 producer omitted the JIT boundary predicate and remains
preserved as incomplete evidence. No v6 original capture is claimed yet.

### Results shader preparation checkpoint, 2026-09-20

**Source identified / browser exercised; timing diagnosis.** The one-match
`results-gpu-diagnostic-1` run reproduces a 159.825 ms staging wait at Results
source frame 163. Its complete 30-second Chrome trace names game GX pipelines
`3d26c2f2`, `e46b0b91` and `aff9ff0d`: three Metal library compilations consume
approximately 120 ms inside that wait. Browser layout/paint and UI raster work
follow the wait. This identifies game pipeline compilation in the reproduced
Results failure; it does not retrospectively classify the separate initial CSS
stall or every event in the earlier unprofiled report.

The diagnostic report is SHA-256
`1fd64db48a2f01e42a426001402ffff1d8ad75266e7047b4a69d1760bed56c13`;
its trace is `acac9e34ec2b52b8030aa380603cb8c474aad40eac77a3d54d5bdab1936659ff`.
The correlated event packet `gpu-trace-evidence-v1.json` is
`686a166fb189637f3d700e636632f835197168af4af6d989a822b56fc5447618`.
The route completes with one explicitly recorded timing resume. Its cache-export
step then fails because the export button is inside closed Diagnostics. The
complete trace remains usable diagnostic evidence; the command remains failed.

**Browser exercised, finite descriptor discovery.** A separately declared
seven-match pass completes `repeatMario` followed by `rotate` in one document
and arena, including Results/Prize/CSS and final unload. No timing resume or
console/page error occurs. The portable cache is read only after native cache
idle; the private browser profile is retained. Its report
`results-seed-discovery-1/report.json` is
`6a6ca5b953d185e2867a433e99f0bc7f22960f74d173450f522374e2ebd01b09`.
All 60 owner observations fit the bound, and every source-world teardown is
empty. The three repeated Mario CSS live allocations are 325,536,456,
325,538,248 and 325,536,688 bytes. The following rotation introduces more assets;
it ends with 42 immutable archives and 15 audio banks. This is finite lifecycle
and discovery evidence, not an unrestricted allocation bound or acceptance timing.

The cache review adds exactly 27 observed GX descriptors and preserves all 628
previous rows field-for-field, including their original first-use metadata.
There are no payload conflicts or missing base rows, and no Dawn driver-cache
bytes are included. The decoded seed is now
`417b4939c41068ca7a44fe0e4329be3f85e41f607e78e4253a7bb095bb0137d9`:
one Clear row and 654 GX rows. The source scene receives no extra draws or
ticks. The [portable seed receipt](evidence/results-pipeline-seed-v1.json)
binds the producer, inventory, export, review and all appended descriptor
identities. Twelve focused cache/seed checks and the affected development
Release build pass.

Timing diagnostics now distinguish callbacks with source work from callbacks
with no source work while preserving all existing deadline counters. Timing
waits fail promptly on a native timing pause and never resume it. The first
failed acceptance plan remains sealed; no replacement acceptance slot is
consumed by this reduced regression.

**Reduced browser regression passed.** The frozen updated Release receipt is
`results-seed-checkpoint-1/receipt.json` (SHA-256
`8a8a28a03c4547a072950d38b094f89e6caee491733d982dc05f1481d18b2277`).
The one reserved cold-origin Mario/Mario Final Destination prefix completes
four stock losses, three respawns, five visible seconds of Results, its original
Prize/CSS route and final unload, without a timing resume. Its report
`results-seed-check-1/report.json` is
`acaf1369c0e402a9b418c9daae6ee9dc5a7f5fcee12c687d4a08cbbdab6508b7`.
Chrome 153.0.8010.48 runs visibly at DPR 2 with audible audio and focus emulation
disabled. Driver caches remain uncontrolled.

The measured interval contains 3,393 source steps/draws, zero live pipeline
creations/queues, zero native target/hard misses, zero browser hard gaps,
zero audio underruns/overflows, and no browser errors or focus loss. Native and
browser maxima are 8.465 and 26.060 ms. Both browser long tasks fall wholly
inside declared scene preparation. All 3,427 native callbacks are explicitly
partitioned into 3,346 callbacks with source work and 81 without it. This
supports the narrow seed correction; it is one reduced diagnostic, not a new
whole-sequence cold/warm acceptance matrix. Earlier failures remain retained.

### Resume boundary after the usage-conservation checkpoint

Prize and repeated keyboard match returns are functional. Finish the independent
original-comparison and whole-loop timing gates before marking #34 complete or
merging this draft. No physical controller is required for this PR.

The next original-comparison implementation is a typed first-CSS context and
continuous whole-session input replay, reusing the current observer and scene
owners. The DOL getters establish GameRules at profile root `+0x1850` (0x18
bytes), SaveData at `+0x1868` (0x1790 bytes), and seven name banks starting at
`+0x2ff8` (each 0x1f2c bytes). Capture/decode those fields with explicit endian
and packed-field handling, plus the existing RNG/PAD/setup observations. The
current observer carries only the two profile masks. The current browser replay
accepts only one match and deliberately rejects a used source heap; it cannot
be treated as a whole-session replay by concatenating recipes. The opt-in
extension must retain one source arena and cursor through the real scene chain.

Results comparison also needs full typed MatchEnd/standings and the source
post-OnEnter display state. The current observer's 0x28-byte result prefix does
not cover player standings. No importer, new Results snapshot or whole-session
browser replay was implemented at this checkpoint. Two independent original
captures per named sequence, browser comparisons, repeated rotation allocation
bounds and a fresh frozen cold/warm matrix remain open. Run the required final
full suite, affected public build and CI after these changes settle; focused
checks do not replace integration verification.
