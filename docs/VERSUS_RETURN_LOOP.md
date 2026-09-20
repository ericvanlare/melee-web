# Versus return loop

[Issue #34](https://github.com/ericvanlare/melee-web/issues/34) extends the old
Results-skipping playable slice. Its acceptance remains open: the original
Results scene, whole-session retail comparison, physical input and retained
source-heap equivalence are separate from the keyboard crash repair below.

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
