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

The public Release browser check ran headless installed Chrome through
`scripts/browser_tools.mjs` and `scripts/browser_driver.mjs`. It reached CSS
first, verified ordinary B leaves CSS active, completed two CSS → Main → Title
→ Main/Versus → CSS cycles, exercised Eject's page reload, then re-imported the
owned disc and started at CSS again. That check records browser-visible source
phase/message and screenshots. It is functional lifecycle evidence, not
foreground timing or input-latency evidence.

## Retained local evidence

All captures, screenshots, private inputs, and generated runtime bundles remain
under ignored `work/` and are not committed:

| Evidence | Retained material |
| --- | --- |
| `work/original-menu-route-20260926-01/retail-route-12-full/capture.json` | Complete cold-boot route manifest, input and build identities, 24 scoped screenshot/source markers, and completion status |
| `.../retail-route-12-full/route-source-states.jsonl` | 1,874 decoded passive PAD/menu source-state observations |
| `.../retail-route-12-full/capture.mwro`, `input.mwri`, `inputs.jsonl`, `dolphin.log` | Observer stream, input transport receipt, named controller commands, and emulator log |
| `.../observer-build-menu-route-07.json` | Observer build receipt and source overlay hashes; Dolphin 2606a-dirty from commit `c77bbaa0f372c3f72281602a8b087206706542cb`, binary SHA-256 `a197b95a737ad1ad410738fdaab94985c48bdccd5063ce99f4134b849c82d1b1`, JITARM64, guest-memory writes disabled |
| `.../browser-public-lifecycle-03/report.json` | Public Release identity SHA-256 `2760098b76ab200a3f2b7bc95dedb9dd86fa46c863dd2239020de137f78679c1`, headless Chrome `153.0.8010.54`, route boundaries, CSS B probe, two repeated cycles, Eject/reimport result, and screenshot inventory |
| `.../browser-public-lifecycle-03/*.png` | Browser screenshots at the CSS, Main, Title, route-return, and post-reimport boundaries |
| `.../menu-full-tests-with-owned-inputs.log` | Final full-suite run with owned DOL/disc/source/symbol inputs configured |
| `.../menu-runtime-final-build.log`, `.../menu-public-release-final-build.log` | Final development runtime and public Release build logs |

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
| **Source identified** | Pinned title/Main source callbacks, mode routing, and authored resource owners | Full-game menu coverage |
| **Retail compared** | Cold-boot route order, ordinary inputs, observed source menu states, and labeled screenshots on the owned disc | Pixel/PCM equivalence, physical-controller acceptance, foreground timing, or performance |
| **Browser exercised** | CSS-first startup, original menu route, two repeated returns, public Eject/reimport teardown and CSS re-entry | Retail/browser pixel or PCM comparison, foreground input latency, or tournament acceptance |
| **Compiled** | Development and public Release menu runtimes | Deployment or merge |

The separate allocation-history GDB/Python route remains available for scopes
that need a stopped scheduler and allocation/RNG sampling. It is not a
prerequisite for observing this ordinary menu route. Keep future claims scoped
to the retained capture and build identities above.
