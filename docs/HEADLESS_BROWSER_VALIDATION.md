# Browser automation without desktop interruptions

Routine browser checks default to modern headless installed Chrome. They can
render the game, retain screenshots, inspect GPU output, exercise input and
collect diagnostics without opening or focusing a desktop window. Pass
`--headed` only for an explicitly arranged foreground session. Failures never
automatically retry in a visible browser.

The evidence label is **Browser exercised**, scoped to the functional scenarios
below. Headless checks do not replace visible performance, physical-device,
audible-output or original-comparison gates. The original investigation
[receipt](evidence/headless-browser-compatibility-v1.json) retains its checks,
early-import failure and local artifacts.

## Running checks

The shared smoke and routine public-player, audio, resize, prototype and
controller-player checks accept `--headed`; omitting it keeps Chrome headless. The
runtime-free public-shell check uses the same boolean flag. Browser selection
uses `--playwright` / `MELEE_PLAYWRIGHT_DIR` and installed Chrome, with
`MELEE_BROWSER_PATH` for an explicit Chromium executable. Invalid explicit
configuration fails instead of selecting a different browser.

Ad hoc Playwright scripts must use the same launch policy:

```js
import {loadBrowserTools, browserLaunchOptions} from './scripts/browser_tools.mjs';
const {chromium, browser: installedBrowser} = await loadBrowserTools();
const browser = await chromium.launch(browserLaunchOptions(installedBrowser));
try {
  const page = await browser.newPage();
  // Drive the real HTTP page and retain scenario diagnostics/screenshots.
} finally {
  await browser.close();
}
```

Do not use desktop `open` commands or `bringToFront()` for routine checks.
`scripts/run_hitch_matrix.mjs` (both `profile` and `run`) and
`scripts/capture_cpu_browser.mjs` require `--headed` before doing capture work.
Their foreground protocols remain unchanged after that explicit opt-in. A
minimized or background window does not substitute for a visible timing run.
Changed harness hashes require fresh hitch profiles; keep historical receipts
bound to their original scripts.

## Repeat the synthetic capability probe

```sh
node tests/browser_headless_capabilities_test.mjs \
  --out work/headless-capabilities \
  --playwright /path/to/installed/playwright
```

The output directory must be new. The probe uses the shared installed-browser
resolver, a real loopback HTTP server and headless Chrome. It validates GPU
framebuffer readback before and after resize, DOM/network inspection, keyboard
and mouse events, document focus, file-chooser bytes, download bytes, console
and page-error reporting, and a retained screenshot. Its synthetic fixture is
not gameplay evidence. It records the browser, adapter, available backend
information and effective launch flags; failures retain their report and page
artifacts. A software fallback is not requested.

## Original compatibility investigation

The existing checks were run from isolated copies with their assertions
preserved and their browser launch changed to headless. Two public scenarios
also used the cache-readiness preflight described below. Raw results and the
exact runner variants are retained under ignored `work/headless-validation/`.

| Utility | Exercised boundary |
| --- | --- |
| Driver and failure diagnosis | Real HTTP fixtures, serialized key chords, application errors, failed teardown, recovery and exclusive report creation |
| Development player | Owned-disc import, original CSS entry, unload and shared controller settings |
| Controller UI | Authored Gamepad values, suggested mapping, correction, persistence and both guided D-pad variants; no physical-device claim |
| Public player | Startup, controls, invalid-disc errors, responsive layouts, DOM fullscreen, keyboard CSS/SSS/cancel, pause/resume, Eject and second import |
| GPU output | Hardware WebGPU readback in the synthetic fixture, actual public framebuffer resizing, and retained original-menu/game screenshots |
| Audio processing | Connected 32 kHz worklet, nonzero PCM through CSS, SSS, two match entries and No Contest, then audio-owner teardown |
| Gameplay regression | Original CSS selection, SSS cancellation, Mewtwo Confusion capturing/releasing the opponent and clean unload |
| Desktop interruption | macOS activation notifications and on-screen window observations associated with the gameplay browser process |

Screenshots from the public source-pause and second-match states were inspected.
This establishes that the agent can inspect rendered output; it is not a
pixel-equivalence comparison against headed Chrome or the original game.

## Retained early-import failure

The first public smoke selected the disc as soon as the import control became
enabled. It failed with `Pending renderer work did not drain. Reload to recover.`
The report remains a failure. A smaller no-disc probe observed that the import
control became usable before `_melee_web_native_menu_cache_idle()` returned 1.

At the investigation's base revision, `tests/public_player_browser_test.mjs`
already waited for that cache-ready state before importing. Its full headless
run passed unchanged.
Separate smoke and audio variants added the same semantic preflight and passed;
they did not alter the renderer-drain deadline, simulation, input or assertions.
This identified a startup readiness boundary; it did not prove that the failure
was exclusive to headless mode. No new headed control was run on the user's
desktop.

Do not turn this preflight into a blanket waiver. A test of immediate import
must still exercise immediate import and retain the failure. A preparation
error or timeout must remain explicit; do not retry automatically in a visible
browser or resume a timing failure to obtain a pass.

The shared runtime now keeps Import disabled while the initial native
renderer-cache state is pending and shows graphics preparation. Direct API
imports are also rejected before that boundary. The first settled state is
latched, so later gameplay does not disable replacement imports merely because
an active scene makes the cache non-idle. The existing optional cache-error
state remains distinct from a missing or invalid native service. Startup has a
bounded failure, and the renderer-drain and simulation deadlines are unchanged.
The public-player check asserts cache readiness as soon as Import is enabled,
without a second readiness wait that could hide this regression.

## Implementation validation

The [implementation receipt](evidence/headless-browser-defaults-v1.json) binds
fresh Release development/audio builds, the audited local public package, the
full suite, and the actual migrated browser commands. The public smoke passes
immediate enabled-control import without the investigation's extra cache wait.
Public menus, audio processing, controller tooling, prototype UI, resizing and
the original Mewtwo regression pass with screenshots and diagnostics retained.
The synthetic probe uses hardware WebGPU on the recorded machine.

The macOS monitor observed no activation, foreground samples or on-screen
windows for the owned Mewtwo regression browser. Both foreground-only commands
reject missing `--headed` before browser or capture work; explicit opt-in is
tested through argument/launch configuration without opening a desktop window.

Integration also corrected the prototype check's stale catalog expectations
and the audio report's headed-only wording. The resize check now resizes during
startup before waiting for the newly gated Import control, then retains its
post-startup and controls-open checks. The original failure artifacts remain in
the receipts. No deployment was performed.

## Evidence boundaries

Keep the scenario's assertions, ordinary browser rendering, diagnostics and
screenshot artifacts when using headless Chrome. Record the mode, browser and
adapter in receipts, and retain explicit headed execution for scenarios that
need it. Synthetic/native headless traces that omit draw callbacks are a
different boundary from a headless browser executing the normal draw path.

Headless document focus and DOM fullscreen do not establish operating-system
focus, display presentation or actual fullscreen-window behavior. Playwright's
normal functional-test scheduling flags and muted audio also differ from the
[hitch protocol](HITCH_CAPTURE.md). Nonzero PCM at the worklet is not evidence
of speaker output, original PCM equivalence or hardware audio latency.

Keep the declared visible timing matrix, cold/warm acceptance, real controller
connections and latency, audible output, and OS focus-loss tests on their own
protocols. Use an explicitly arranged foreground session or a separate named
test machine for those checks. The [accuracy contract](ACCURACY_CONTRACT.md)
and [playbook](PERFORMANCE_AND_ACCURACY.md) continue to apply.
