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

The retained retail idle capture shows that Title timeout is a source mode
transition, not a Title-to-Title animation loop. After the Title callback's
20-frame input guard, `gm_Scene_Title_OnFrame` counts source frames; when the
counter exceeds 600 it writes a zero exit payload and requests scene exit.
`gmTitleMode_OnExit` interprets that zero payload as `GM_OPENING_MV` and sets
Opening state 1. `gm_Mode_Opening_OnLoad` selects the state through the source
`gm_801BF718()` value. State 1 runs the original four-CPU VS demo. The captured
idle sequence then reaches Title state 2, another VS demo at state 3, and the
HowTo movie at state 4; the retained trace later wraps to Opening state 0.
The `GM_OPENING_MV` state table also declares state 5 (`MvOmake15.mth`), which
was not reached in this retained run.

The capture at
`work/original-menu-route-20260926-01/title-attract-20260927-full-01/` retained
41,052 source-state observations and 194 labeled rendered Metal frame samples.
It contains repeated visits through states 1, 2 and 3 and rendered moving
samples from the VS demo and HowTo movie. It was terminated by the runner after
the declared observation window (`scenario_complete=false`), so it does not
establish a completed repeated-cycle run or controller interruption. The
separate `title-attract-20260927-probe-title1` capture confirms the first
timeout destination and Title return, but likewise ends at its bounded
predicate. Do not describe these files as complete-cycle acceptance.

The browser host now preserves the source Title timeout as
`GM_OPENING_MV` state 1 and invokes the original Opening mode `OnLoad`. It does
not turn the zero payload into Start. The browser still reports an explicit
unsupported destination at the next owner boundary: the original randomized
VS demo needs its full source player/content lifecycle, while this runtime has
admitted only 19 fighters. The movie range reader and THP CPU/alarm owners are
bounded infrastructure for the declared MTH dependencies; the Opening source
scene callbacks and a rendered/PCM-complete attract cycle are not implemented
by this increment.

The follow-up is decomposed at the source boundaries:

1. Own each `GM_OPENING_MV` state and its original mode/scene enter, frame,
   draw, exit and preload callbacks, preserving the state-machine route across
   resource-scope rebuilds.
2. Run state 1/3 as source VS demos without changing their four-player
   `StartMeleeData`, RNG/history selection, all-port input edges or 1200-frame
   source exit. This requires admitting every source fighter the active save
   and usage history can select; unsupported content must fail before drawing
   and remain Eject-recoverable.
3. Run the authored `MvOpen.mth`, `MvHowto.mth` and (when naturally selected)
   `MvOmake15.mth` through the source THP callbacks, bounded DVD ranges,
   source-frame alarm and audio/render owners. Their disc sizes exceed the
   current per-asset cap, so they must remain streamed.
4. Compare more than one complete retail/browser cycle at source-frame
   boundaries, including the original return order and ordinary controller
   interruption from every connected port.

This increment implements and tests the exact Title timeout to Opening state 1
mode handoff and clean owner retirement. It is a runnable source transition
boundary, not acceptance of the attract sequence; the next blocking boundary
is source VS demo ownership and content admission.

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
under ignored `work/` and are not committed:

| Evidence | Retained material |
| --- | --- |
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
| `work/title-idle-attract/public-audio-browser-final-head/report.json` and `*.png` | Headless Chrome run against the Release audio-player package: CSS → supported match → Results → CSS → Main → Title timeout, explicit unsupported Opening state 1 error, Eject/reimport to CSS, audio/PCM transport and teardown |
| `work/title-idle-attract/audio-player-final-head.manifest.json`, `audio-player-final-head-audit.json`, and `audio-http-final-head.json` | Production package inventory, package audit, and local Pages HTTP verification; package `source_sha` binds the browser report to the final source head |
| `work/title-idle-attract/full-tests-after-public-error.log` | Pre-PR-109 full unittest discovery after the public error-surfacing fix: 1,548 tests, 94 optional skips, no failures; superseded by the reconciled-head suite below |
| `work/title-idle-attract/full-tests-reconciled-final.log` | Full unittest discovery on implementation head `1ec06d4`; the only later commit corrects retained evidence paths in this document, with no executable changes |

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
