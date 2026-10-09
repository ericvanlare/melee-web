# Original menu route

The separate `capture_sd_reference_prefix.py` diagnostic accepts an explicit
recipe-five/menu-five GCI campaign for original Rules → Items → two-human Mario
CSS → Final Destination → natural one-minute tie → SD initialization. Its fixed
input plan declares neutral P1/P2 and disconnected P3/P4 through the bounded
gameplay prefix. Earlier authored recipes and reduced Rules probes remain
distinct contracts; ordinary replay collectors still reject authored plans.

The route reuses MenuFlow values, generic CSS cursor/door/live-state slices and
the highlighted stage kind. Its sole added steering field is the original SSS
acceptance cooldown, read only after the verified constructor return. Every
menu motion and source-consumed input has a declared cap. Final VS setup and SD
normalization remain exact byte comparisons. The final SSS A is released after
observed consumption; any A leaking into gameplay fails the neutral plan.
An interrupted observer prefix requires independently complete native MWRI.
Neither portable controls nor a compiled producer establish that this extended
original route ran, or that it matches native gameplay, pixels, PCM or timing.

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
source owners and reimport starts at CSS. The reconciled native host now also
has a deterministic supported VS handoff diagnostic: it checks the authored
four-CPU payload, retained PAD input across suspension, finish to Opening state
2 Title, and owner teardown. That diagnostic is scoped lifecycle evidence; it
does not make the browser's unsupported asset boundary or the attract route
accepted.

The native Opening probe also follows the real Title timeout to authored state
0, then rejects movie entry before source preload because the narrow browser
host has no checked `lbMemory`/`lbHeap` owner. It restores the menu/world owner
and leaves `MvOpen.mth` unread; this is an explicit unsupported movie boundary,
not a THP playback or retail movie capture.

Retail captures establish the sequence through three Opening-mode Title visits,
including two returns to Opening state 0, and separate P1/P2 interruption paths.
They do not establish a complete unattended return to normal `GM_TITLE` or an
Omake15 visit. The browser retains the original Title timeout payload, source
state selection and `gm_Mode_Opening_OnLoad`, then fails explicitly at checked
asset preparation when the randomized four-CPU demo selects content outside the
current runtime's admitted 19 fighters and 7 stages. The source menu/match route
remains usable, and Eject/reimport recovers to CSS. The native diagnostic
exercises the supported state handoff separately, without substituting for
browser asset publication or a full attract run.

Completing the retail route requires these bounded source owners:

1. Extend the supported four-CPU VS demo lifecycle to every source-selected
   identity: all 26 selectable source characters, all 29 source stages, source
   camera/CPU-level phases, 1200-tick exit, all-port interruption, and
   teardown/re-entry. The random source selector may choose any unlocked
   identity; per-character or per-stage overrides would change the source
   behavior. The current native receipt covers one deterministic supported
   handoff and does not close this broader acceptance goal.
2. Run `MvOpen.mth`, `MvHowto.mth`, and naturally selected `MvOmake15.mth`
   through original THP video/audio callbacks, streamed DVD ranges, source
   timing/alarm, and owned graphics/audio services. The movie assets remain
   bounded streams rather than whole-file imports.
3. Carry the Opening state and source RNG/save/session owners through each
   scene/resource rebuild, route P1/P2 input and Eject from every phase, and
   prove repeated clean return/re-entry.
4. Compare full retail and production-browser cycles at source-frame
   boundaries, with transition screenshots and PCM transport evidence. The
   existing audio test validates the menu/match route and the explicit
   unsupported Opening boundary; the native handoff receipt does not stand in
   for this browser/retail gate.

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

The original idle callback's zero triggered-button payload selects
`GM_OPENING_MV` state 1 and runs `gm_Mode_Opening_OnLoad`. Checked asset
preparation then reports the exact unsupported source-selected demo contents;
the route does not skip the demo, substitute another destination, or claim a
completed attract cycle. Eject retires the source owner and reimport starts at
CSS.

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

<a id="main-settings-rumble-route"></a>

## Main Settings > Rumble route

The retail Observer capture in
[`save-profile-dolphin-roundtrip-v1.json`](evidence/save-profile-dolphin-roundtrip-v1.json)
records the original Main `MenuKind` route `0 → 4 → 19 → 4 → 0` at source
ticks `95, 130, 141, 186, 197`: Main, Settings, Rumble, Settings, Main. The
source scene remains `GS_MENU` (`1`) throughout the settings route after Title
(`GS_TITLE`, `0`). At tick 175, ordinary A changes Controller 1 rumble from on
to off; B leaves Rumble and then Settings. The source callbacks are
`mnMain_Scene_OnEnter`, `mnMain_Scene_OnFrame`, `mnVibration_Init`,
`mnVibration_HandleInput`, and `gmMainLib_SetRumbleEnabled`. The Rumble menu is
`MENU_KIND_SETTINGS_RUMBLE` (`19`) and uses the `MenMainConVi_Top`,
`MenMainCtlVi_Top`, `MenMainOnoffVi_Top` and `MenMainCursorVi_Top` exports in
the Main `MnMaAll.usd` archive. Main also loads its authored `SdMenu.usd`
(`SIS_MenuData`) and `SdToy.dat` (`SIS_ToyData`) roots. The retail Observer
receipt has a Null video backend, so its menu IDs/input/save bytes are source
reference evidence rather than a retail visual capture.

The headless installed-Chrome route in
[`main-settings-rumble-route-v1.json`](evidence/main-settings-rumble-route-v1.json)
renders those original Main, Settings and Rumble scenes. From a new isolated
browser context it keeps the default Everything unlock profile, confirms the
switch to Personal progress, enters CSS, opens Main with the B0XX L+R+Start
chord, and reaches Title and Main by their original callbacks. Three D-pad
Down inputs and A enter Settings then Rumble. After the original intro gate,
ordinary A disables Controller 1 rumble. The native SaveData snapshot reads
`[0, 1, 1, 1]` at offset `0x458`; autosave, repeated Settings/Rumble entry,
exit through original Main/VS selection to CSS, GCI export, and document reload
retain that value. The production native menu owner unloads successfully after
the route. The reduced repeated-entry browser run also retains each rendered
checkpoint and source observer state.

This is source-callback behavior and preference persistence evidence for one
Settings subroute. The browser screenshots establish rendered original menu
scenes and input response; they are not pixel comparisons. The retail capture
uses Null video and no audio output. The browser run uses the audio-disabled
public player, so audio fidelity, physical-controller behavior, foreground
timing and performance remain untested. The route does not implement or claim
the other Settings pages.

## Main Settings > Display route

The retail Observer capture in the scoped [Display route receipt](evidence/main-settings-display-route-v1.json)
records Main, Settings and Display `MenuKind` IDs `0 → 4 → 21 → 4 → 21 → 4 → 0`
at source ticks `96, 128, 155, 200, 210, 245, 255`. From the source Title
scene, three D-pad Down presses and A enter Settings; two Down presses and A
enter Display. A turns deflicker off; B returns to Settings, A re-enters
Display with the same row selected, B returns to Settings, and B returns to
Main. The relevant retail callbacks are
`mnMain_Scene_OnEnter`, `mnMain_Scene_OnFrame`, `mn_8022DB10`,
`mn_8022D104`, `mnDeflicker_8024A6C4` and `mnDeflicker_8024A168` from
`melee/mn/mnmain.c` and `melee/mn/mndeflicker.c`. The Main archive resolves the
authored `MenMainConDf_Top` model from `MnMaAll.usd`; the settings scene also
uses `SdMenu.usd` (`SIS_MenuData`) and `SdToy.dat` (`SIS_ToyData`). The source
SaveData byte at `0x45D` changes from `1` to `0`. Dolphin reports the route
pass and exact menu sequence; its Observer stream is marked interrupted because
the capture runner stops its owned Dolphin after the sequence. Retail video and
audio were disabled.

In a fresh isolated headless-Chrome context, the public player starts with
Everything unlocked, confirms a new Personal profile, enters original CSS,
opens the original Title/Main menus, and changes Display deflicker from on to
off. The browser reads source SaveData at `0x45D`, waits for Personal autosave,
backs out and re-enters Display, returns through original Main/VS selection to
CSS, exports the GCI with the same byte, then enters SSS, reaches a supported
match, opens No Contest Results and returns to CSS. The passing
`browser-route-integrated-01` run uses the combined Rumble and trophy-baseline
runtime and sends no SSS cursor input, so stage identity is unobserved and
excluded. The earlier route used short directional inputs and misidentified its
stage; that label is superseded. The corrected runner uses
`scripts/serve.py` for cross-origin-isolation headers; the setup failures are
retained in the receipt. Personal profile reload and native menu owner teardown
retain the preference. The SSS back-cancel probe separately confirms B returns
to CSS after the source input gate. Rendered screenshots and the raw browser
report are indexed by the receipt.

The receipt separates source state, navigation, rendered visuals, save effects,
audio, physical input, performance and lifecycle. Browser rendering proves the
original scenes are displayed and that the option responds; it does not prove
pixel equivalence. Dolphin used Null video and No Audio Output, and the browser
package was the silent player profile. Audio quality, physical input,
foreground timing, performance and the identity of the random/default stage
remain untested. These results do not establish coverage of other Settings
pages or full-game parity.

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
| `work/title-idle-attract/pr110-native-opening-handoff.stdout.log` and `pr110-native-opening-handoff.jsonl` | Reconciled Release native handoff receipt: the authored Opening VS selection publishes four CPU/level-9/stock rows, suspends with retained PAD input, finishes into Opening state 2 Title, and retires its owner; the same build's compiled source-file bridge and source-frame alarm traces pass. This is a deterministic native boundary diagnostic, not retail capture, browser equivalence, or a complete attract-cycle claim |
| `work/title-idle-attract/pr110-full-suite-final.log`, `pr110-audio-release-build-final.log`, and `pr110-public-release-build-final.log` | Final reconciled validation: full unittest discovery ran 1,583 tests with 96 optional skips and no failures; affected audio-preview and public Release targets rebuilt cleanly, including the native handoff, source-file bridge, source alarm, and public runtime targets |
| `work/title-idle-attract/pr110-browser-smoke/report.json` and `pr110-browser-runtime-build.log` | Rebuilt development runtime reached its import-ready boundary through a real loopback HTTP server and headless installed Chrome. No disc was supplied, so this is browser transport/readiness evidence only and makes no gameplay or attract claim |
| `work/title-idle-attract/public-audio-browser/report.json` and `failure.png` | Retained failed first browser iteration: CSS and two supported matches passed, then Title timeout left the menu owner closed without surfacing an error dialog; the 45-second route assertion failed. Superseded by the final UI recovery check |
| `work/title-idle-attract/public-audio-browser-final-head/report.json` and `failure.png` | Retained harness failure after the menu route, Title Eject/reimport, audio and PCM checks passed: document reload discarded an in-memory scoped-asset observer before the final assertion. Superseded by the verified report below |
| `work/title-idle-attract/public-audio-browser-verified-final-head/report.json` and `failure.png` | Retained second harness failure after Title Eject/reimport, audio/PCM checks, and scoped asset transactions passed: final Eject checked a retired AudioContext from the earlier document. Superseded by the final report below |
| `work/title-idle-attract/public-audio-browser-complete-final-head/report.json` and `*.png` | Headless Chrome run against the Release audio-player package: CSS → supported match → Results → CSS → Main → Title timeout, explicit unsupported Opening state 1 error, Eject/reimport to CSS, scoped asset transactions, audio/PCM transport and teardown |
| `work/title-idle-attract/public-player-audio-title-attract-resume-settled/report.json` and `*.png` | Final production audio-player browser route: 15 checks passed, including CSS-first start, two menu cycles, Main/Title Eject and CSS-first reimport, SSS → supported Mario/Final Destination → Results → CSS, nonzero 32 kHz PCM and audio-context retirement |
| `work/title-idle-attract/audio-preview-title-idle-a05f5e3-after-graphics/report.json` and `*.png` | Same source-bound audio-player package, 12 checks passed; records the original idle timeout to state 1, exact unsupported source-selected fighter/stage error, nonzero PCM, Eject/reimport to CSS and closed prior audio context. The existing `--select-after-graphics` option skips only the older early RVZ wording assertion |
| `work/title-idle-attract/audio-preview-title-idle-a05f5e3.log` | Preserved preliminary failure from that harness's optional early invalid-RVZ assertion, whose expected wording predates the current player error; the final Title idle scenario above ran with its supported after-graphics option |
| `work/title-idle-attract/public-match-pause-probe-resume-settled/after-start.png` and `after-lras.png` | Focused headless public-player probe after UI resume: ordinary Start opened source P1 Pause and LRAS reached Results; it validates the input-settle delay without exposing development diagnostics |
| `work/title-idle-attract/public-player-audio-title-attract-ready-phase/report.json`, `public-player-audio-title-attract-source-pause/report.json`, and `public-player-audio-title-attract-recovery-probe/report.json` | Preserved failed iterations found and corrected harness issues: missing phase in a shared state reader, unsupported use of a development-only diagnostics export, and a dropped input edge immediately after resume; the focused probe and final full route passed |
| `work/title-idle-attract/full-tests-final-precommit.log` | Full repository unittest discovery with the owned menu fixture root explicitly configured through `MELEE_MENU_FIXTURE_ROOT`: 1,553 tests, 93 optional skips, no failures, 296.446 seconds |
| `work/title-idle-attract/runtime-public-final-release-build.log`, `audio-release-final-a05f5e3-build.log`, `audio-player-title-attract-a05f5e3.manifest.json`, `audio-player-title-attract-a05f5e3-audit.log`, and `audio-player-title-attract-a05f5e3-http.json` | Release public/audio builds, audited source-bound audio package, and local Pages HTTP evidence. The package identity binds both browser reports to source `a05f5e3fbeed05639c4b71aba9fa66b6784dec19` |
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
| **Browser exercised** | Production audio-enabled public CSS-first startup with the full source roster; two original menu cycles; Main/Title Eject and CSS-first reimport; routed SSS, supported match and Results return; original Title idle handoff through checked source-selected Opening asset preparation, explicit failure for unadmitted assets, and Eject/reimport; connected 32 kHz nonzero PCM transport and audio-context teardown | A complete attract cycle, all randomized demo/movie destinations, retail/browser pixel or PCM comparison, audible quality, foreground input latency/timing, physical-controller acceptance, performance, or tournament acceptance |
| **Compiled** | Reconciled silent and audio-enabled public Release targets and audited package graphs | Deployment or merge |

The separate allocation-history GDB/Python route remains available for scopes
that need a stopped scheduler and allocation/RNG sampling. It is not a
prerequisite for observing this ordinary menu route. Keep future claims scoped
to the retained capture and build identities above.
# Reduced Items lock experiment

After the recipe-five original prefix failed at `items-frequency-row:observed`,
menu packet six retains the same setup actions but stops at Items row31/value3.
Its separate `items_row` observer scope reads the original one-byte animation
lock only under actual scene1/MenuFlow kind16. The verified GALE01r2
`fn_80233E10` instructions at `0x80233ec0` load and test that byte at
`0x804d6bec` before navigation. Main-menu cooldown zero alone is insufficient.
The runner waits for the observed lock zero, sends one declared Up pulse, then
requires the original row/value and neutral release. No CSS, gameplay, timeout
or SD continuation is admitted. Native input completion remains separate from
the intentionally interrupted observer. Missing, misplaced or malformed lock
data and consumed input while locked fail explicitly.

Portable reconstructed positive/negative controls do not establish native
behavior. The failed original stream has no lock slice, so it establishes
ignored normalized Up, not the actual runtime lock value. Prior full-prefix
packets, producer binaries and failure evidence remain preserved.

The reduced lock probe subsequently observed the opening A still held for one
copied source sample after the Items owner appeared with lock1. Its receiver
therefore recognizes an entry drain only after exact Rules row5/declared A to
Items row0/value1/entering1/lock1 ownership. Only the identical held A bank can
drain while that locked owner persists, within the existing polling/sample
caps. The first copied neutral permanently closes this allowance. New A,
unlocked A, another input or owner, and Up before lock0 are fatal. The runner
still releases after first copied A and waits for copied neutral; no input is
added or replaced and no queue is flushed. Entry samples remain reported.

Full-prefix menu7 carries that same typed lock and entry drain through the
existing recipe-five route. Its controller bytes and action order match
historical menu5; Items frequency actions additionally require lock0. The
receiver observes one Up to row31/value3, three separate Right pulses with
source changes 3→2→1→0, then B and the original Rules owner. It rejects a
frequency change without its declared pulse or leaving Items before commit.
The original CSS/FD route and strict normalized VS/tied-timeout/SD payload
checks remain; the stop is still SD setup, without resolution inputs. Tag56
uses its existing verified byte reader for `sd_prefix` as well as the reduced
`items_row` probe. The preserved ed97 producer lacks that full-prefix scope;
source23b9/binaryb6ea compiled it. The actual menu7 attempt observed lock-clear,
row31/value3, the three Right decrements and B commit back to Rules, then failed
at first CSS inventory because the driver required Human before moving either
cursor. Its raw prefix/cleanup is retained in the scoped
`original-sd-prefix-boundary-failure-v3.json` receipt (SHA14924194…). Both initial
doors were NA, their cursors were at y=-21.5, and no CSS input was authored.

The corrected menu7 driver admits only that initialized vacant owner or a
coherent own-Human owner. It reuses the declared move-to-Mario axes: original
`mnCharSel_CursorThink` joins the own NA door when the cursor enters
0.2<y<22, as already used by `retail_cpu_menu_prepare.select`. The driver then
requires observed own-Human before A placement; CPU, foreign ownership and
unconstructed inventory fail. Historical menu5 retains its immediate Human
precondition. Menu7, recipe5, action bytes and caps are unchanged. Observer
overlay/nativeb6ea remain unchanged; the receiver/driver checkpoint is separate.
The changed a051 campaign actually observed both own-Human joins and the final
two-Mario colors1/0 lineup, then FD highlighting and consumed confirmation A.
It failed on the shared SSS counter's accepted-selection value30, not the
constructor's initial19. The scoped `original-sd-prefix-boundary-failure-v4.json`
receipt (SHA91125a42…) retains that progression and incomplete MWRI/cleanup.

The receiver now distinguishes constructor countdown<=20 from accepted
selection: only observed FD/cooldown0 and a new declared P1 A after neutral arm
the source-authored30-frame counter. It requires first30 and consecutive source
ticks/decrements, the same selected FD owner, and an observed neutral release;
another A after neutral or other continuation fails. The FD/cooldown0 selection
snapshot remains frozen, and VS admission requires observed countdown0 plus
all existing strict setup checks. The actual fixture retains30→18 only; later
countdown0/VS/tie/SD controls are explicitly synthetic. No native observer,
menu7, recipe5, controller bytes or caps change for this decoder correction.

The changed 7d2 campaign observed the exact30→0 countdown, then neutral zero
progress through source tick239/menu count539. It failed at seq1689: the same
SSS/FD/zero snapshot and frozen counters remained, while authored routing changed
from SSS state1 to VS state2 (previous state1). `gm_801A4014` changes routing
after OnExit; the SceneInfo pointer remains installed until the next
`gm_801A4B88`. PADRead polls during this interval are observations, not source
ticks. The receiver correction retains only that exact retiring route and
unchanged owner/FD/zero/tick/input-count snapshot after observed countdown0 and
neutral. It forbids any consumed input or return to active routing. Active
duplicate/gapped ticks still fail, and strict VS entry remains separate. The
offline actual replay accepts the buffered prefix through seq1698, without
admitting VS/SD or the interrupted terminal as success. The failed run and
incomplete MWRI remain preserved; no native observer, inputs or caps changed.

The single changed campaign at receiver5e136/native23b9-b6ea passed the existing
strict receiver through original normal VS, natural tied timeout and SD setup.
The scoped `original-sd-prefix-boundary-result-v6.json` receipt (SHAf71945e0…)
binds the raw streams, complete native MWRI, intentionally interrupted primary
observer, immutable profile and exact owned cleanup. The predeclared offline
V11 comparison (`declared-v11-initialization-comparison-v2.json`, SHA1df30ced…)
passed the retained normalized participant, port, color, stock, damage, rule,
scene-order and natural-timeout fields. These receipts are under the retained
`menu-tie-extension-v1/sss-retirement-launch-v1` campaign. This remains an
initialization comparison: original SD resolution/Results, raw PAD/RNG equality,
pixels, PCM, physical input and live timing are excluded.

### Separate competitive profile-entry diagnostic

Authored recipe6/menu8 uses the same verified GCI and original menu, CSS,
SSS and normalized VS ownership boundaries for a separate competitive profile.
It observes stock4, handicap0, damage10, Rules Plus timer8/FF1/pause0,
every selectable Items row0..30 off and frequency None. The Items path follows
the original two-column navigation: Down0..15, Right15->30, Up30..16,
Up16->32; each conditional A requires an observed on value and unlocked owner.
The unmapped preference bit28 is retained. Committed preference bytes and
normalized setup are both checked; a None frequency alone cannot pass.

`MWRC_SD_MENU_PROBE=competitive_entry` stops at the verified normal VS setup
return with a complete native MWRI footer and interrupted primary observer.
This scope rejects active gameplay, SD and legacy whole-session completion.
The prior SD native producer is preserved and rejects this new scope. The
650caaf1/e4634a74 canonical producer compiled, and its first original attempt
observed every switch off and the three frequency decrements. It failed at the
first Items-to-Rules snapshot (seq1127): the receiver required cooldown0,
while original `fn_80233E10` commits and sets cooldown5. The retained
`competitive-original-profile-entry-launch-v1/first-items-boundary-reduction-v1.json`
(SHA2595d314…) binds the actual return, held B release and countdown5→0.
The receiver correction requires exactly that committed return and consecutive
source countdown, allowing only the already consumed held B until neutral.
Readiness remains cooldown0 before any new action. Native producer, recipe,
menu and inputs are unchanged; the reviewed second attempt remains pending.
The failed stream is incomplete and cannot establish profile-entry acceptance.
Source/unit controls do not establish runtime settings or outcome equivalence.
Natural non-tied timeout comparison remains a separate #292 gate. This prefix
makes no original Results/CSS lifecycle claim; that continuation is not required
unless the terminal source fields need it.
