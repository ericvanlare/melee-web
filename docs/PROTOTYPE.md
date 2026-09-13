# Browser prototype boundary

This is a local staging/development shell for the existing native browser runtime:
a full-window black 4:3 game area with plain toolbar buttons for Disc, Play, Pause,
Controls, Fullscreen and Eject. Status appears only while an operation is busy or
has failed. Controls and keyboard bindings are on-demand. The shell contains no
alternate menu or game simulation.

`prototype-shell.mjs` owns those utility controls and mounts
`prototype-runtime-adapter.mjs`. `prototype-content.mjs` remains a background
manifest check against the shared roster and stage identities. Native CSS/SSS are
the only visible character and stage inventory.

This is a prototype preview, not an acceptance result, release bundle or public
distribution mechanism. Disc images, extracted files and generated runtime
binaries stay local and untracked.

## Current capability and evidence

Ordinary VS CPUs at levels 1–9 now use the shared compiled source and locally
decoded CPU data. One-player B0XX mode can enter SSS with a CPU opponent. See
[CPU opponents](CPU_OPPONENTS.md) for the current implementation and evidence;
the keyboard-only checkpoint below predates this addition.

The user selects a USA 1.02 ISO, GCM or CISO locally. The existing runtime reads
`NATIVE_GAME_DISC_FILES`, opens original CSS and SSS, and runs the current native
four-by-four gate: Mario, Fox, Falco and Marth; Final Destination, Battlefield,
Yoshi's Story and Dream Land. The source owns selection, rules, controller
processing, simulation, drawing, audio, pause and teardown. The shell owns focus,
pause/resume, controls, fullscreen and child restart/eject.

Availability comes from `src/gameplay_content.h`, through
`melee_web_fighter_content()`, `melee_web_fighter_content_by_kind()`,
`melee_web_stage_content()` and `melee_web_stage_content_by_ground()`. Checks in
`src/gameplay_menu.c`, `src/gameplay_player_context.c`,
`src/gameplay_stage_profile.c` and `src/gameplay_world.cpp` remain authoritative.
`web/match-flow.mjs` is the stale Mario/Final Destination HTML scaffold; its
`supported` flags and fake flow do not define prototype availability.

Evidence remains scoped. `STATUS.md` records source-menu transition checks and the
four-by-four browser gate. Ordinary keyboard Start and SSS cancellation pass.
Complete ordinary keyboard play, physical controllers, controller-to-photon
latency, original-game visual/PCM comparison, clean cold-cache timing and broad
content admission remain open. `docs/REPLAY_CORPUS.md` records bounded workloads
and repeated-session diagnostics without broad distribution or gold/content
admission; its expanded report says `gold_admitted: false`.

There is no hard production diagnostics exclusion yet. The existing runtime
artifact contains observation, replay, raw-PAD and performance diagnostics. The
prototype adapter does not expose them, which is an API decision rather than a
binary/release guarantee. Production needs a build/CI exclusion check. The public
mount must omit raw-PAD traces, replay cursors, action sweeps, source memory and
native diagnostic readbacks; development tools attach separately.

## Temporary same-origin adapter

`prototype-runtime-adapter.mjs` exports:

```js
mountPrototypePlayer({ root, onStatus = () => {}, onSceneChange = () => {} })
```

It returns a frozen handle:

```js
{
  importDisc(file): Promise<void>,
  openCharacterSelect(): Promise<void>,
  pauseOrResume(): Promise<void>,
  setKeyboard(slot, enabled): void,
  setKeyboardLayout('two' | 'boxx'): void,
  focus(): void,
  dispose(): Promise<void>,
}
```

`onStatus` receives a frozen snapshot when changed:

```js
{
  state: 'booting' | 'available' | 'importing' | 'preparing' | 'pausing' | 'unloading' | 'error',
  message: string,
  scene: 'idle' | 'character-select' | 'stage-select' | 'preparing' | 'closed' | 'match',
  progress: {complete: number, total: number} | null,
  canImport: boolean, canLaunch: boolean, canPause: boolean, canUnload: boolean,
}
```

`onSceneChange(scene)` reports the display scene. Its numeric source is child
`#status.dataset.phase`, not simulation state: `1` CSS, `2`, `4`, `5` preparation,
`3` SSS, `6` closed and `7` match.

The child must be same-origin and contain IDs `canvas`, `disc`, `launch`, `pause`,
`unload`, `status`, `keyboard` and `keyboard2`. The adapter reads status text,
status phase and control `disabled` states; assigns `disc.files` through
`DataTransfer` and `File` constructed in the child window, so the existing
reader receives same-realm ArrayBuffers without reading the whole disc; invokes
`disc.onchange`, `launch.onclick` and `unload.onclick`; clicks pause;
focuses `contentWindow`/canvas; and appends child CSS. These are temporary DOM
dependencies, not a runtime API. The outer shell supplies its toolbar root,
controls, status/progress/error and on-demand dialogs.

Disc import reuses the child's inline handler, which loads `runtime-assets.mjs`,
calls `loadNativeGameDisc`, transfers bytes through `put`, runs
`prepareNativeResources` and handles failure. The adapter's one native configuration
call is `_melee_web_input_set_keyboard_layout`; it does not call gameplay,
replay, `loadDiscBundle`, audio or cache methods. The [keyboard follow-up](KEYBOARD_LAYOUTS.md)
documents that input-settings boundary. Its 250 ms timer
reports display state only; it never drives source ticks. Launch verifies the
child enabled its live controls. Pause uses a status observer to wait for the
existing handler’s acknowledgement before accepting another command. A watchdog marks a
failed child unusable; Eject or restart removes the iframe. Normal Eject waits
for teardown/cache save, including after preparation without launching CSS.
A fatal or timed-out child can only be discarded; its optional cache save is
not guaranteed.

Exact HTTP/HTTPS same-origin access is required. Sandboxed, `file://` and
cross-origin iframes cannot use this contract. The child owns the Emscripten loop,
visibility, audio worklet and input polling. Eject completes native unload,
cache-idle wait and cache save before iframe removal when the runtime is responsive.

## Browser callback and source-clock boundary

`src/gameplay_menu_browser.cpp::tick()` calls `window.menuServiceCommands()` before
`aurora_update()`, then calls `melee_web_input_poll()` once per browser callback.
`menu_clock.tick()` can consume zero or multiple fixed source steps. A catch-up
callback reuses that one browser-polled input sample for all consumed source steps;
it does not sample a new device state per source step. This catch-up input-sampling
gap remains open under the accuracy contract.

For each consumed step, `SourceFrameSequence::before_step()` establishes the source
presentation boundary, `GameplayMatchSession::tick()` or
`melee_web_menu_host_tick()` advances the source, and `did_step()` records it.
`SourceFrameSequence::finish(present_source)` completes the draw sequence. A
no-step callback may redraw a frozen preparation scene but may not invent a source
tick. Preserve this tick/draw order and catch-up distinction when extracting it.

## Future `mountMeleeRuntime` API

The iframe should become an explicit versioned runtime mount:

```js
const runtime = await mountMeleeRuntime({
  canvas, onState, onPreparation, onTiming, onAudio, onError, onComplete,
});
await runtime.importDisc(file);
await runtime.prepare();
await runtime.start();
runtime.focus();
await runtime.pause(); await runtime.resume();
runtime.getState();
await runtime.unload(); await runtime.destroy();
```

The lifecycle handle is `importDisc`, `prepare`, `start`, `focus`, `pause`,
`resume`, `getState`, `unload` and `destroy`. `getState()` returns a versioned
typed state such as `uninitialized`, `importing`, `prepared`, `css`, `sss`,
`preparing`, `match`, `paused`, `ending` or `error`. Callbacks report lifecycle,
preparation, timing, audio, errors and completion. The public API has no raw-PAD
diagnostics, replay controls, source-memory access or native pointers. Dev tools
attach through a separate development module/build.

Extract inline responsibilities without changing their owners:

- `prepareAudio`, `waitForAudioAck`, `pauseAudioForPreparation` and `syncAudio`
  own the audio context/worklet. `menuAudio` supplies native PCM and
  `menuAudioReadyForPreparation` supplies the acknowledgement gate.
- `put` is the only JS-to-native file transfer: temporary `Module` buffers are
  allocated, `_melee_web_native_menu_file` is called, and buffers are freed.
- `prepareNativeResources` emits `menuPreparation`, pauses audio, yields one
  browser turn, calls `_melee_web_native_menu_prepare` and emits completion.
- `unloadAndSave` calls `_melee_web_native_menu_unload`, emits preparation
  cancellation, waits for `_melee_web_native_menu_cache_idle`, and calls
  `Module.saveRuntimeCache` only when dirty.
- The current `Module` setup, fatal-stop handling and `boundary` command queue
  move into the shared owner. `menuServiceCommands` must still drain commands
  synchronously at the native boundary. `menuAudioReadyForPreparation` must still
  return its synchronous acknowledgement; neither can become a fire-and-forget
  status event. `menuFrame` retains lifecycle/input/audio bookkeeping, then emits
  coalesced presentation status separately.
- The required `window.menu*` callbacks initially remain narrow dispatchers into
  that owner, because the compiled native code invokes those names. Move optional
  timing sinks, raw-PAD diagnostics, stock/action sweeps, replay state/transport,
  hitch collection and evidence downloads into a separate development attachment.

After the performance work lands, extract in this order:

1. Move the inline host state and core functions into `melee-runtime.mjs` without
   reordering preparation, commands, audio gates or source callbacks. Retain one
   active owner per document. The current global Emscripten loader and `#canvas`
   selector do not support arbitrary concurrent mounts.
2. Make startup own the loader URL/asset resolution, pre-start cache installation,
   `onRuntimeInitialized` and abort/error listeners. Resolve the mount only after
   startup completes; reject explicit aborts. Keep `Module` off the public handle.
   Repeated full mounts require a modularized loader/explicit shutdown; until
   then, use document reload for complete heap reset rather than claiming disposal.
3. Extract development controls and observers. The development entry imports
   their attachment; the staging/production entry must not import those modules.
   Keep core failure and timing-disruption reporting available to the player.
4. Change `runtime.html` to mount the shared owner, attach its diagnostic tools,
   and preserve its existing control semantics. Switch the prototype adapter to
   mount that same owner and delete all iframe DOM/status parsing.
5. Update the coordinated build copy rules, executable artifact identity inventory
   and browser tests. Verify both pages' boot/import, CSS→SSS→match→CSS,
   audio acknowledgement, focus/visibility, pause, teardown and second launch.
   Re-run the relevant reference and uninstrumented performance gates before
   claiming unchanged gameplay admission. Do not execute fresh holdouts for UI QA.

Ownership remains:

| Responsibility | Owner |
| --- | --- |
| Disc/image reads and immutable assets | `DiscImage`/`openDiscImage`; `RuntimeFiles`; `RuntimeArchiveCache` |
| CSS/SSS world and menu audio | `GameplayMenuWorld` |
| Source CSS/SSS session, RNG, globals, PAD queue and transition | `MeleeWebMenuHost` |
| Selected match assets and source match flow | `GameplayWorld`; `GameplayMatchSession` |
| Browser callback, source clock and presentation | `tick()`; `present_source()` |
| Optional renderer cache | `installRuntimeCache` |

Preserve source transition order: construct without source simulation; enter CSS;
observe/rebuild CSS→SSS; obtain SSS selection from the host; construct deferred
match; close match/world; restore host RNG; re-enter CSS. Unload completes native
close before cache save or canvas release.

## Native extraction order

Use `loadNativeGameDisc`, whose `NATIVE_GAME_DISC_FILES` manifest currently has 66
entries. `RUNTIME_DISC_FILES` has the earlier 14-file Mario/FD slice and is
insufficient.

1. `openDiscImage()` validates ISO/GCM/CISO identity and GameCube magic;
   filesystem table parsing/validation follows the DOL check.
2. Read the DOL pointer/header/sections and verify the fixed original DOL SHA-1.
3. Parse the FST; require every manifest path, non-empty bounded files and the
   aggregate limit.
4. Read manifest entries in insertion order and report progress.
5. Extract `sislib_font.bin` from the DOL font range.
6. Validate and append generated `dsp_coef.bin`.
7. Pass bytes through `_melee_web_native_menu_file`, then prepare natively.

Imported disc data is never written to the renderer cache; that cache is
renderer-only and saves after native teardown.

## Content bridge and reproduction

`scripts/prepare_prototype.py` reads only the narrow static tables in
`src/gameplay_content.h` and writes `prototype-content.json` with schema
`melee-web-prototype-content-v1`. It fails closed on missing tables, unrecognized
rows or duplicate source names. `prototype-content.mjs` uses generated rows for
availability and joins historical `ROSTER`/`STAGES` for identity compatibility;
`match-flow.mjs` never supplies supported lists.

The script does not rebuild the runtime. It requires a matching existing build,
checks each `BUILD_ARTIFACTS` entry by SHA-256, copies stable artifacts and records
runtime, prototype and native-content hashes in `prototype-build.json`. The output
must be a fresh ignored directory.

```sh
python3 scripts/prepare_prototype.py --runtime-build path/to/matching/build \
  --output path/to/new/prototype-preview --environment staging
python3 scripts/serve.py --directory path/to/new/prototype-preview --port 8794
```

Open `http://127.0.0.1:8794/prototype.html`. `--environment development` changes
the document title and exposes a Dev button for the development player. Staging/development
is an HTML/build distinction, not a query, authorization or security boundary.
`serve.py` supplies loopback COOP/COEP, path confinement and no-store headers.
Evidence POST storage is opt-in with `--evidence-directory` and remains outside
the served output. Do not publish the output or commit game data, extracted assets
or generated binaries.

## Input issue #5

Controller assignment, ordering and arbitrary remapping remain deferred. Controls
now switches between the existing split keyboard and a one-player B0XX-style
preset with modifiers; see [keyboard layouts](KEYBOARD_LAYOUTS.md). Preferences
are stored locally, and the current player keeps its existing physical port routing. Detection is not physical-controller
acceptance. The future input boundary needs `setInputBindings(bindings)` with
explicit slot/port ownership, release/focus behavior and validation.

## Integration inventory

The initial shell was new-files-only. The keyboard follow-up also changes
`src/browser_input.cpp`, `src/browser_input.h`, their existing tests and
`THIRD_PARTY.md`, and adds `src/boxx_input.h` with its tests/license. These input
files need coordination if another branch changes them. The CPU follow-up also
changes the native owners and shared patch listed below. `runtime.html`,
renderer/cache instrumentation and native build configuration remain untouched.
CI now installs dependencies before running dependency-backed tests.
`runtime.html` DOM/status changes are semantic conflicts because the temporary adapter relies on exact IDs, phase values and
handlers. Future `runtime.html`, CMake/build target, runtime tests,
`runtime-assets.mjs` manifest or native content-table changes must be coordinated
with the mount boundary, staging hashes and content bridge.

## Initial shell validation — 2026-09-12

The prototype target was assembled from the matching `d69a7fb` Release runtime
snapshot. Its Wasm SHA-256 is
`09717d148a0231262afbe9868c58828f4c7aea098f9b602e290396be587f229f`.
At this initial checkpoint, no native target, source timing, performance
instrumentation or build configuration changed. The later keyboard follow-up
rebuilds the runtime with the input mapping described in KEYBOARD_LAYOUTS.md. `prototype-build.json` records the complete served
runtime and shell hashes; generated files remain ignored.

`python3 -m unittest discover -s tests -v` completed successfully: 528 tests,
30 skipped, 319.986 seconds. Skips are unavailable native trace/build dependency
fixtures and optional Slippi fixtures in this isolated checkout; this is not a
claim that the complete native acceptance suite ran. The three prototype tests
also pass separately, including actual HTTP headers, unavailable-file rejection,
packaging drift rejection and shared roster/native availability consistency.

The browser check uses headed Chrome 153.0.8010.36 over loopback HTTP. It checks
startup, the generated inventory, keyboard toggles and canvas focus, invalid RVZ
and ISO rejection/retry, 390px/320px layout, fullscreen, verified owned-disc
preparation, caught launch-error propagation, original CSS entry, acknowledged
pause/resume with rapid-click suppression, ordinary keyboard CSS→SSS navigation,
and Eject
with iframe/heap retirement. No page/HTTP/console errors or upload requests were
observed in the passing run. Screenshots and raw reports stay under ignored
`work/`; screenshots containing original game content are not committed.

Reproduce using Node with Playwright and installed Chrome:

```sh
node tests/prototype_browser_test.mjs \
  --url http://127.0.0.1:8794/prototype.html \
  --out work/prototype-browser \
  --disc /path/to/owned-disc.ciso \
  --playwright /path/to/playwright
```

Omit `--disc` for the startup/error/layout checks. These are interface checks,
not a complete match, audio/physical-input acceptance, a performance run, or
fresh-holdout/consecutive-match evidence. The public 4×4 gate remains open.


## Keyboard follow-up validation — 2026-09-12

This section records commit `3f0705f` before CPU integration. Its two-human
restriction is superseded by [the shared CPU work](CPU_OPPONENTS.md); the earlier
checks and failed attempts remain recorded here with their original scope.

Controls now offers the original two-player keyboard and a one-player B0XX-style
preset. The exact bindings, MIT source, native input API and limitations are in
[KEYBOARD_LAYOUTS.md](KEYBOARD_LAYOUTS.md). This follow-up modifies only the
browser input provider and prototype files/tests/docs; the contested runtime,
renderer/cache instrumentation, native gameplay, patches and build configuration
are untouched.

A fresh local `Release` runtime build completed with
`python3 scripts/build.py --target runtime --configuration Release --jobs 4`.
The final preview's Wasm SHA-256 is
`5b873d798c9efe826068d48f35e6cf63640646a439ab885193e5abe4f99deb1a`.
`build/prototype-keyboard-final/prototype-build.json` records the served hashes.
No disc data or generated binary is tracked.

Full `python3 -m unittest discover -s tests -v` discovery completed successfully:
542 tests, 46 skipped, 247.253 seconds. Skips are unavailable native trace targets,
optional local fixtures and tests requiring the separate default browser build;
the changed input tests use this worktree's Release SDL headers and ran. The log
is retained at `work/prototype-keyboard-unittest-final.log`.

The 13 native input adapter tests and the standalone B0XX vector test pass.
They cover raw mapping, modifiers, event order, focus/layout resets, preserving
held P1 input when P2 connects/disconnects, physical priority and failure cleanup.

Headed Chrome 153.0.8010.36 passed all 14 recorded HTTP browser checks at
640×480 native framebuffer resolution. `work/prototype-keyboard-browser-final/report.json`
records zero page/console/HTTP errors and zero upload requests. The check sends
ordinary keyboard events and reads existing PAD diagnostics: all documented
B0XX buttons and axes, Mod X/Y, shield diagonals, SOCD release, D-pad layer,
focus loss, persistence after reload, split-profile restoration and Eject pass.
The original two-keyboard CSS→SSS route still passes. B0XX cancellation returns
to CSS, and its Start key reaches the native PAD boundary.

**B0XX Start was not admitted from the tested one-keyboard CSS state.** With P2
keyboard disabled and no P2 controller, original CSS converted P2 to CPU. The
existing `gameplay_menu.c` gate requires two humans, so it rejected that state.
The final browser check explicitly preserves this restriction; it does not count
CPU or solo-match support as a pass. Controls surfaces the limitation.

Earlier browser attempts remain in ignored `work/`: the first pressed a key
before the existing focus policy acknowledged activation after reload; another
expected the unsupported CPU selection to enter SSS. The final test waits for
input activation and the source CSS's entry lockout, verifies the actual Start
sample, and checks that the two-human gate remains intact. Native gameplay and
admission checks were not loosened to make the UI test pass.

This is keyboard/input-boundary evidence only. Complete matches, physical
controllers, keyboard rollover, input latency, firmware/tournament equivalence,
audio and performance admission remain unverified by these checks.

## Shared CPU follow-up — 2026-09-12

CPU work adds no adapter API and changes no inline host code. Both pages use
the original CSS difficulty slider and the same `GameplayMatchSession`,
source CPU routines and retained PlCo root22 owner. The final HTTP UI test
passes CPU gameplay and teardown on both pages, plus prototype No Contest
back to CSS. The old Controls warning about unavailable CPU matches is removed.

Two independent original-game runs per difficulty agree exactly with the port
for 480 ticks each at CPU levels 1 and 9 (Mario versus Mario on Final
Destination). Both the compiled trace target and the drawn browser runtime
match the declared fighter, input, RNG, clock and PAD-history fields. The browser
executes 480 source steps and 480 draws per workload. These bounded checks do not
complete matches or establish pixel, audio, hardware or performance acceptance;
[CPU opponents](CPU_OPPONENTS.md) records the procedure, artifact identities and
remaining scope.

The CPU follow-up changes `gameplay_menu.c/.h`, the retail setup decoder,
`dat_common.cpp/.hpp`, common tables/context, reference tools and their tests.
It also adds the shared player-selection header and a small admission change
in `patches/melee-gameplay.patch`. That shared patch is the main additional
integration conflict risk. Coordinate edits to those native owners when
integrating; no `runtime.html`, renderer/cache timing, CMake or hitch-capture
files are changed. Future runtime extraction carries the existing compiled
runtime dependency, including CPUs, without a separate prototype AI path.
The only workflow change reorders pinned dependency installation ahead of tests
in `.github/workflows/verify.yml`.
