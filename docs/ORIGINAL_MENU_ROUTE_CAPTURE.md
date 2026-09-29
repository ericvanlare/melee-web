# Original menu route

## Observed retail route

The title/main-menu route is now implemented with the recovered title and menu
callbacks and has been observed on the owned GALE01r2 disc. The capture uses the
existing Observer-enabled Dolphin route, ordinary Pipe controller input, and
fresh copies of the private profile and retail-created memory card. It does not
require a PowerPC debugger, embedded Python, or debugger writes.

The route and accepted inputs are:

| Boundary | Retail input and result | Captured source state |
| --- | --- | --- |
| Cold boot | Boot with the isolated retail memory-card baseline; Start skips the opening movie | Opening movie and Title scene kinds |
| Title → Main | After the source title input guard, Start opens the original 1P root | Title/Main scene kinds; main `MenuFlow` root and input cooldown |
| Main → Versus → CSS | Down selects Versus; A opens the Versus submenu; A selects Melee | Root selection, submenu selection, then CSS scene kind |
| CSS Back check | Ordinary B remains in CSS | CSS scene kind before and after the probe |
| CSS → parent Main | L+R+Start enters the original Main 1P root | CSS routing bytes and Main root selection |
| Main → Title | B from the Main root returns to Title | Main and Title scene kinds |
| Title → CSS again | After the Title guard, Start, Down, A, A re-enters CSS | Title/Main/Versus/CSS boundaries |

The complete cold-boot sequence was repeated twice. The capture is scoped to
menu routing and its source boundary states. Its 24 screenshot markers are the
latest headless Metal frame dumps retained at each label; each marker also
retains the separate passive Observer state and the controller command log.
This is visual route evidence, not a pixel comparison or a PCM/audio check.

## Title idle and Opening-mode findings

### Retail sequence and input

The pinned retail capture
`/Volumes/AgentStorage/melee-web/runs/title-attract-repeat-20260928-204724-721ca350/retail-title-visits3-01/`
boots cold with neutral input and reaches three Opening-mode Title visits. Its
route trace records the no-input order
`state 0 → 1 → 2 → 3 → 0 → 1 → 2 → 3 → 4 → 0 → 1 → 2`:

| Exiting source scene | Next Opening state and scene | Recorded source counter at handoff |
| --- | --- | ---: |
| `MvOpen.mth` | 1, four-CPU VS demo | 6131 |
| State 1 VS demo | 2, Title | 1201 |
| Title callback | 3, four-CPU VS demo | 621 |
| State 3 VS demo | 0, `MvOpen.mth` | 1201 |
| `MvOpen.mth` | 1, four-CPU VS demo | 6131 |
| State 1 VS demo | 2, Title | 1201 |
| Title callback | 3, four-CPU VS demo | 621 |
| State 3 VS demo | 4, `MvHowto.mth` | 1201 |
| HowTo movie | 0, `MvOpen.mth` | 4761 |
| `MvOpen.mth` | 1, four-CPU VS demo | 6131 |
| State 1 VS demo | 2, Title | 1201 |

These are Observer source-counter values sampled at the route boundary; each
counter belongs to the scene that just exited. They are not wall-clock
durations. The repeated run covers two complete returns to Opening state 0 and
part of a third route. It stops at Title visit 3 by declared predicate, so it
does not claim a full unattended return to normal `GM_TITLE` or an Omake15
visit. The captured save/session state routes the second state 3 through
HowTo, then back to state 0. Source code shows that `gm_8015DB00()` controls
this alternation: state 3 increments it and selects state 0 when even; HowTo
increments and selects state 0 unless it is already 5, in which case the
authored next state is Omake15. That later state-5 path is source-identified,
but remains unobserved in this capture.

The retail input captures
`retail-interrupt-p1-opening-01/` and `retail-interrupt-p2-vs-01/` use the same
cold-boot baseline with isolated profile/card copies. P1 Start at source tick
125 interrupts `MvOpen.mth`, returns to normal Title, then ordinary P1 Start
opens Main. In the second run P2 Start interrupts the state-1 VS demo while P1
is neutral, returns to normal Title, and P1 Start opens Main. A retained P2
Opening-movie probe is at
`work/original-menu-route-20260926-01/title-attract-20260927-audio-p2-probe/`.
The source callbacks use `gm_GetButtonsTriggered(PAD_MAX_CONTROLLERS)`: Opening
movie accepts Start/A before its late cutoff, VS demo accepts Start/A, and the
normal Title callback accepts Start only after its 20-callback guard. The held
input is edge-consumed during that guard; it is not replayed later.

`gm_Scene_Title_OnFrame` then increments its source timer and times out when
`frame_count > 600`, writing the source's zero exit payload. The focused native
host check counts the exact 501 remaining callback steps after its first 120
Title callbacks, for 621 total Title callback steps. Retail records 621 at the
Title-to-state-3 route boundary. `gmTitleMode_OnExit` preserves the zero-payload
route to `GM_OPENING_MV` state 1; Start is never fabricated. Opening state 1
uses `gm_SetupTitleDemo`, which chooses four source CPUs and a stage from the
source unlock/usage-history tables and RNG. The selected roster/stage therefore
varies naturally; this capture does not claim identical RNG or demo identities
between separate retail runs.

The no-input run retained 30,924 decoded source-state observations, 61,441
Observer events and 69 labeled Metal samples. I reviewed its moving MvOpen,
VS-demo and HowTo images. The capture sampler's immediate state-2 marker is
not synchronized to the source Title render, so that image is not used as
Title visual evidence. Use the aligned normal-Title screenshot in
`work/original-menu-route-20260926-01/retail-route-12-full/evidence/` and the
post-interruption Title sample in the P2 capture for that scene. This is visual
inspection, not a pixel comparison. The P1/P2 captures each end at Main after
their declared input predicate; their MWRO end markers are interrupted by
the runner's intentional stop.

### Browser increment and remaining boundaries

The browser preserves the original Title timeout payload and Opening state-1
selection. It calls the source `gm_SetupTitleDemo` and derives an exact
four-player asset closure without narrowing unlocks, RNG/history or selected
identities. The production audio-player regression reaches that real boundary.
The current runtime admits 19 of 26 source fighters and 7 of 29 stages; this
source-selected demo chose unsupported content, so checked asset preparation
fails explicitly with the identities listed in the error. Eject retires the
source owners and reimport starts at CSS. This is the smallest runnable source
increment at the first missing owner boundary, not an implemented or accepted
attract cycle.

Completing the retail route requires these bounded source owners:

1. Complete the four-CPU VS demo lifecycle, including all 26 selectable source
   characters, all 29 source stages, source camera/CPU-level phases, 1200-tick
   exit, all-port interruption, and teardown/re-entry. The random source
   selector may choose any unlocked identity; per-character or per-stage
   overrides would change the source behavior.
2. Run `MvOpen.mth`, `MvHowto.mth`, and naturally selected `MvOmake15.mth`
   through original THP video/audio callbacks, streamed DVD ranges, source
   timing/alarm, and owned graphics/audio services. The movie assets remain
   bounded streams rather than whole-file imports.
3. Carry the Opening state and source RNG/save/session owners through each
   scene/resource rebuild, route P1/P2 input and Eject from every phase, and
   prove repeated clean return/re-entry.
4. Compare full retail and production-browser cycles at source-frame
   boundaries, with transition screenshots and PCM transport evidence. The
   existing audio test validates the menu/match route and the first explicit
   unsupported Opening boundary; it does not stand in for this gate.

The earlier `title-attract-20260927-full-01/` run also remains as a failed
reproducer: it stopped emitting Observer events during a later state-3 demo
and did not reach Omake15. The new visit-3 capture proves two repeat returns
without claiming that failed route completed.

## Browser implementation and lifecycle

The development and public player keep their CSS-first Play behavior. The
original title and Main scene entry, tick, draw, transition, and exit callbacks
now own their menu scenes. CSS L+R+Start uses the source `force_main_menu`
route; the original CSS exit is retained, the checked `GM_MENU` owner enters
the source Main scene, and the CSS payload is reopened only after the later
source route returns to `GM_VS`. Root-menu B runs the source exit to Title;
Title Start runs its source exit back to Main. Unsupported or incomplete source
routes still fail through the checked owner.

The main/title world binds their authored scene exports through owned DAT
decoders and the existing archive scope. The additional Main-menu resources are
`MnMaAll.usd`, `SdMenu.usd` (`SIS_MenuData`), `SdToy.dat` (`SIS_ToyData`),
`LbMcGame.usd`, `NtMemAc.usd`, `LbMcSnap.usd`, and `GmEvent.dat`;
`LbAd.dat` supplies the original audio load descriptor. Title uses
`GmTtAll.usd`, including its authored title mark image and palette. The shared
menu service owns the exact pinned source `ssm_files[]` table for title demo
selection; it does not narrow loading to a fixture bank. Original callbacks
retain input sampling, RNG, initialization, and scene teardown ownership; no
synthetic menu state replaces them. This route capture does not compare RNG
values across retail and browser runs.

The default profile keeps the source's full unlockable-character table open:
`gm_80164F18()` derives the `0x07ff` mask from its 11 authored rows. It then
runs `gm_8017297C()` and `gm_801741FC()`, the paired source debug-unlock helpers
for the 0x42 unlock-notification entries and 0x12c reward-ledger entries. That
gives Title's original save checks a settled unlock/award state; the source
Title callback still decides whether it enters GM_MENU or requests Challenger
Approach. The current browser stage mask remains `0x01c0`, limited to the
supported stage set. This uses the source-owned initialization approach in
PR #102 while keeping #96 independent of its broader Settings/save-profile
feature.

The final public Release checks use headless installed Chrome through
`scripts/browser_tools.mjs` and `scripts/browser_driver.mjs`. Both affected
Release runtimes (`runtime-public` and `runtime-audio-preview`) were built; the
production audio-enabled `audio-player` package was prepared and audited. The
audio-enabled package check reached CSS first with the complete character
roster visible, completed two CSS → Main → Title → Main/Versus → CSS cycles,
and exercised Eject from Main and Title. Each native unload completed before
reimport; each reimport started directly at CSS. It then completed CSS → SSS →
Mario/Final Destination gameplay → Results → CSS. The browser report retains
screenshots, source phase/message, Web Audio state, and PCM transport
observations. Chrome observed connected 32 kHz worklet output with nonzero PCM
through CSS, Main, Title, SSS, gameplay, Results and CSS re-entry, and observed
the live audio contexts close during Eject. The production package also passed
local Pages HTTP inventory/header verification; Wrangler reports its known
local 502 responses for the reserved `_headers` and `_redirects` routes. This
is functional lifecycle and PCM-transport evidence, not PCM equivalence,
audible quality, foreground timing, input latency or performance evidence.

The native regression checks that the default mask is `0x07ff`, every source
unlock-table character remains available to CSS, and the roster remains intact
after Eject/reimport. It exercises Title Start from P1 and from P2 while P1 is
neutral; a P2 Start held through the source guard is consumed as an ordinary
input edge and requires release/repress. A separate source lock/reset/unlock
sequence creates an authentic pending character notification, so the original
Title routine requests unsupported Challenger Approach and the host reports
that destination explicitly. Eject then recovers to CSS. A Title timeout is
also checked to retain a zero payload and no fabricated destination. These
checks do not enable Challenger Approach or claim full title/demo coverage.

## Retained local evidence

All captures, screenshots, private inputs, and generated runtime bundles remain
outside Git and are not committed. Existing evidence stays under ignored
`work/`; the repeated cold-boot capture set below is on the verified external
NVMe:

| Evidence | Retained material |
| --- | --- |
| /Volumes/AgentStorage/melee-web/runs/title-attract-repeat-20260928-204724-721ca350/retail-title-visits3-01/capture.json and route-source-states.jsonl | Successful neutral-input cold-boot capture through three Opening Title visits: 30,924 source-state observations, 61,441 Observer events, 69 Metal samples and the repeated state 0→1→2→3→0→1→2→3→4→0→1→2 trace; the third visit is the declared stop, not a full unattended return |
| /Volumes/AgentStorage/melee-web/runs/title-attract-repeat-20260928-204724-721ca350/retail-title-visits3-01/capture.mwro, input.mwri, dolphin.log, and evidence/ | Passive Observer stream, named neutral P1/P2 Pipe input record, owned Dolphin log and sampled source/render evidence; the source-Title marker is not treated as an aligned Title screenshot |
| /Volumes/AgentStorage/melee-web/runs/title-attract-repeat-20260928-204724-721ca350/retail-interrupt-p1-opening-01/capture.json, route-source-states.jsonl, inputs.jsonl, and evidence/ | P1 Start interrupted MvOpen, returned to normal Title, and P1 Start entered Main; scenario predicate completed, with the runner stopping the owned Dolphin afterward |
| /Volumes/AgentStorage/melee-web/runs/title-attract-repeat-20260928-204724-721ca350/retail-interrupt-p2-vs-01/capture.json, route-source-states.jsonl, inputs.jsonl, and evidence/ | P2 Start with P1 neutral interrupted the four-CPU VS demo, returned to normal Title, and P1 Start entered Main; scenario predicate completed, with the runner stopping the owned Dolphin afterward |
| `work/original-menu-route-20260926-01/retail-route-12-full/capture.json` | Complete cold-boot route manifest, input and build identities, 24 scoped screenshot/source markers, and completion status |
| `.../retail-route-12-full/route-source-states.jsonl` | 1,874 decoded passive PAD/menu source-state observations |
| `.../retail-route-12-full/capture.mwro`, `input.mwri`, `inputs.jsonl`, `dolphin.log` | Observer stream, input transport receipt, named controller commands, and emulator log |
| `.../observer-build-menu-route-07.json` | Observer build receipt and source overlay hashes; Dolphin 2606a-dirty from commit `c77bbaa0f372c3f72281602a8b087206706542cb`, binary SHA-256 `a197b95a737ad1ad410738fdaab94985c48bdccd5063ce99f4134b849c82d1b1`, JITARM64, guest-memory writes disabled |
| `work/pr96-title-main/review-round2/review-head-public-audio-browser/report.json` | Headless installed Chrome report for 15 production audio-player checks on the menu fix, including the full CSS roster, two menu cycles, Main/Title Eject and reimport, supported match/Results return, audio initialization, teardown, PCM transport and zero runtime errors |
| `work/pr96-title-main/review-round2/review-head-public-audio-browser/*.png` | Screenshots for CSS, Title/Main, both route cycles, SSS, gameplay, Results, and post-reimport boundaries |
| `work/pr96-title-main/review-round2/review-head-audio-player.manifest.json`, `review-head-audio-http.json`, `review-head-audio-audit.json` | Source-bound production audio package identity, package audit, and local Pages HTTP inventory/header verification |
| `work/pr96-title-main/review-round2/logs/review-head-native-menu-ownership.log` | Final-head focused roster, CSS availability/re-entry, P1/P2 Title Start edge, genuine unsupported Challenger request, timeout payload, and Title/Main Eject regression; failed iterations are retained beside it |
| `work/pr96-title-main/review-round2/logs/full-tests.log` | Full unittest discovery on implementation commit `6456af7`: 1,547 tests, 94 skipped, no failures. The later PR commit changes documentation only; optional fixture/tool skips are listed in the log |
| `work/pr96-title-main/review-round2/logs/review-head-audio-build.log`, `review-head-public-build.log`, and `review-head-audio-audit.log` | Final-head Release builds for `runtime-audio-preview` and `runtime-public`, plus production audio package audit |
| `work/pr96-title-main/review-round2/review-head-audio-http.json`, `logs/pages-dev-review-head-audio.log` | Final-head local Pages HTTP verifier receipt and Wrangler output, including the documented reserved-route limitation |
| `work/pr96-title-main/public-audio-browser-reconciled-head-04/report.json` | Earlier diagnostic run on the same reconciled runtime before final package identity; superseded by the final-head report above |
| `.../browser-public-lifecycle-03/report.json` | Historical silent-only browser evidence from the earlier public-player lifecycle; superseded for this PR by the audio-enabled final-head run |
| `work/title-idle-attract/native-title-main-smoke-final-head.log` | Final-head native menu-host regression output; with the explicit owned fixture root, it checks the full CSS roster, P1/P2 Title Start input edges, authentic unsupported Challenger request, zero-payload Title timeout to `GM_OPENING_MV` state 1, Eject, owner retirement, and CSS reimport |
| `work/title-idle-attract/public-audio-browser/report.json` and `failure.png` | Retained failed first browser iteration: CSS and two supported matches passed, then Title timeout left the menu owner closed without surfacing an error dialog; the 45-second route assertion failed. Superseded by the final UI recovery check |
| `work/title-idle-attract/public-audio-browser-final-head/report.json` and `failure.png` | Retained harness failure after the menu route, Title Eject/reimport, audio and PCM checks passed: document reload discarded an in-memory scoped-asset observer before the final assertion. Superseded by the verified report below |
| `work/title-idle-attract/public-audio-browser-verified-final-head/report.json` and `failure.png` | Retained second harness failure after Title Eject/reimport, audio/PCM checks, and scoped asset transactions passed: final Eject checked a retired AudioContext from the earlier document. Superseded by the final report below |
| `work/title-idle-attract/public-audio-browser-complete-final-head/report.json` and `*.png` | Headless Chrome run against the Release audio-player package: CSS → supported match → Results → CSS → Main → Title timeout, explicit unsupported Opening state 1 error, Eject/reimport to CSS, scoped asset transactions, audio/PCM transport and teardown |
| `work/title-idle-attract/audio-player-complete-final-head.manifest.json`, `audio-player-complete-final-head-audit.json`, and `audio-http-complete-final-head.json` | Production package inventory, package audit, and local Pages HTTP verification; package `source_sha` binds the browser report to the final source head |
| `work/title-idle-attract/full-tests-after-public-error.log` | Pre-PR-109 full unittest discovery after the public error-surfacing fix: 1,548 tests, 94 optional skips, no failures; superseded by the reconciled-head suite below |
| `work/title-idle-attract/full-tests-reconciled-final.log` | Full unittest discovery on implementation head `1ec06d4`; follow-up commits correct evidence indexing and browser harness lifecycle assertions, with no runtime implementation changes |

The route uses the owned CISO directly (SHA-256
`b7de482eb955c8a96b6746dfa043b69ae7bf6c7c2a09ac382b9da126faa7055c`) and the
matching extracted `main.dol` (SHA-256
`dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646`, GALE01r2
revision 2). The capture binary, profile template, and initialized card baseline
are hash-bound in the local build and capture manifests. The memory-card save
was created through the ordinary retail prompt during the retained setup run;
each route run used a fresh copy. No private disc, save, profile, screenshot,
capture, or generated public bundle enters Git.

## Evidence scope and open gates

| Label | Supported claim | Not established here |
| --- | --- | --- |
| **Source identified** | Pinned title/Main callbacks, mode routing, the 11-row `0x07ff` unlock mask, and authored notification/reward-ledger owners | Full-game menu coverage |
| **Retail compared** | Cold-boot route order, ordinary inputs, observed source menu states, and labeled screenshots on the owned disc | Pixel/PCM equivalence, physical-controller acceptance, foreground timing, or performance |
| **Browser exercised** | Audio-enabled public CSS-first startup with the full source roster; two original menu cycles; Main/Title Eject and CSS-first reimport; routed SSS, supported match, Results return; one Title idle timeout to the exact Opening state 1 error boundary, followed by Eject/reimport; connected 32 kHz nonzero PCM transport and audio-context teardown | Complete attract cycles, retail/browser pixel or PCM comparison, audible quality, foreground input latency/timing, physical-controller acceptance, performance, or tournament acceptance |
| **Compiled** | Reconciled silent and audio-enabled public Release targets and audited package graphs | Deployment or merge |

The separate allocation-history GDB/Python route remains available for scopes
that need a stopped scheduler and allocation/RNG sampling. It is not a
prerequisite for observing this ordinary menu route. Keep future claims scoped
to the retained capture and build identities above.
