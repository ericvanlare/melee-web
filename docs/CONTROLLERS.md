# Browser controllers

The public player and shared development runtime normalize physical input once,
at the existing PAD sampling boundary. Browser `mapping: "standard"` supplies the
layout for ordinary controllers. Face buttons use positions: south → A,
east → B, west → X, north → Y. Right shoulder → Z; triggers → L/R.
Controls allows an explicit custom mapping when that convention is unwanted.

The public player and development `runtime.html` use the same compact **Controls**
component to select the source separately
for Player 1 and Player 2: **Auto**, **Keyboard**, **Controller only**, or **Off**.
Auto prefers a recognized controller and otherwise enables that player's keyboard.
Keyboard overrides physical input for that slot; automatically assigned controllers
move to another eligible slot. Both public slots can use the keyboard at once.
The public player disables automatic ports 3/4, while the shared manager and probe
retain four-port support. B0XX keyboard input remains Player 1 only.
Source choices survive reload with the existing keyboard preferences. Detailed
controller testing/remapping and keyboard bindings are collapsed settings sections,
not a required step before play. **Disc**, then **Play** enters the original menus.

The recognized Mayflash 0079:1843 raw layout on Chrome/macOS gets an automatic
suggested mapping. Its binding list is visible before setup, and the live display
uses that suggestion immediately. A single binding can be changed without
repeating complete setup; assigning an occupied input swaps the two bindings.
Saved custom profiles take precedence, with an explicit restore-suggestion action.
Other raw layouts and GameCube identities require setup. The previous browser SDL
fallback guessed a generic layout, which could interpret Mayflash raw X as A.
Native SDL database indices are not directly portable to browser mappings. The
adapter also retains floating-point HID hats rather than narrowing their
out-of-range neutral value into a signed joystick axis.

## Shared settings boundary

`web/controller-settings.mjs` owns the compact dialog markup, source choices,
keyboard layout, preference migration/storage, source descriptions, lazy advanced
panel, and focus/cleanup behavior. Its scoped stylesheet is shared too. Each
entry supplies its existing player handle and lifecycle state; the component
never creates a runtime or changes simulation sampling. New pages can mount this
component rather than importing a page shell.

Both standalone entries share preferences at the same origin. The public page
sets `disableExtraPorts: true`; development preserves ports 3/4 for its existing
diagnostics. The older prototype keeps its hidden keyboard-checkbox adapter,
but layout changes now go through this settings owner. Iframe overrides remain
session-only. Development audio, replay and rendering diagnostics stay in the
development entry and remain absent from the public artifact.

## Suggested Mayflash layout and evidence

The suggestion is restricted to Chromium, macOS, VID/PID 0079:1843, raw mapping,
16 buttons and 10 axes. It does not claim recognition of other Mayflash modes,
models, browsers or operating systems. Sources and adaptation:

- The pinned SDL 3.4.10 database supplies physical face/stick indices: A=b1,
  B=b2, X=b0, Y=b3, Z=b7, Start=b9; main X/Y=a0/a1, C X/Y=a5/a2;
  analog L/R=a3/a4. Y directions are inverted for original PAD units.
- The [community SDL database at 5a12daa](https://github.com/mdqinc/SDL_GameControllerDB/blob/5a12daa568d19344f9b6e9286ef5929833b25c7c/gamecontrollerdb.txt)
  additionally suggests b4 for L. R=b5 is an explicit provisional inference;
  neither digital click has a physical acceptance capture. The UI labels this.
- A read-only local inspection of the attached adapter's HID descriptor found
  four report collections, 16 button usages, six unsigned-byte axes and a hat
  usage 0x39. [Chromium's macOS HID reader](https://github.com/chromium/chromium/blob/f127500330c7a2f9b71e11bcabb65e595c1e3ec5/device/gamepad/gamepad_device_mac.mm)
  indexes axes by usage minus 0x30 and buttons by usage minus 1, ignoring duplicate
  usages. Thus the browser hat is axis 9, with unused axes 6–8. Applying the
  native macOS SDL database's interleaved collection indices would be wrong.
- Analog trigger HID range is -1 at byte 0 to +1 at byte 255; physical rest is
  origin-adjusted as described below. Hat cardinal values
  occupy eight equally spaced positions in [-1,1], with neutral outside it.
  The software fixture uses this observed descriptor shape with authored values;
  it is not a recording of the user's physical inputs.

The suggested PC-adapter trigger axes span the HID byte range, but their physical
rest need not be byte zero. The attached controller displayed trigger values
32/34 with released buttons and near-center sticks. Requiring every raw analog
value to be below 15 therefore prevented the entire controller from activating.
The suggestion now learns a session-local trigger origin only with released
buttons, centered sticks (within 15 PAD units) and low triggers (at most 64).
It retains the smallest observed origin and subtracts it in byte units; it does
not stretch travel or replace the game's trigger clamp. Strongly held triggers
cannot become the initial origin. If a light squeeze is held while connecting,
release it to update the origin. Precision against a retail reference remains
open; this is a bounded hardware-normalization fix, not a universal calibration
claim. The resting-offset fixture now passes both activation and original Wasm
PAD delivery, including light pressure independent of clicks.

Chromium's handling of duplicate usages means this does not establish access to
all four physical adapter ports. Four browser-visible devices are supported by
the manager, but physical multi-port transport needs separate verification.

Setup records every GameCube button, both sticks, D-pad and separate analog
trigger pressure/digital clicks. Hats, button D-pads and signed-axis D-pads are
supported. Trigger pressure can arrive as an axis or a GamepadButton value.
Stick setup records direction, retaining the browser's scale; it does not stretch
the measured physical extreme, add smoothing or change simulation timing. PAD
input uses signed 8-bit stick and unsigned 8-bit trigger units. Hardware-specific
rounding and center accuracy still require a physical capture/reference comparison.
The existing Aurora clamp remains a diagnostic copy; the game consumes raw PAD
through its existing source path.

Profiles stay in localStorage under a versioned browser/platform/device/layout
key. They apply only to that origin and survive reload. Storage denial leaves a
visible session-only warning. Profile setup is configuration, not a hardware
accuracy certification. Player assignment is separate from the browser's index.
Unique-layout reconnects preserve the assigned port; ambiguous identical-device
reconnects remain unassigned. Unused adapter ports can be left unassigned.

Controls mutes game input while configuring; closing it requires controls to
return to neutral before play. Existing native focus and held-button/trigger
suppression remain shared with keyboard input. Physical input retains per-port
priority over the keyboard. Browser profiles report vibration unsupported and
do not send vibration to SDL's independent port assignments. Native SDL input
and older browser entries without the shared controller owner retain their
existing path.

## Fast verification

`controller-check.html` loads no disc, Wasm game or GPU pipelines. The same panel
is available under **Controls** in the public and development players. Connect the adapter, press
a button to expose it to the browser, then inspect the suggested or standard
mapping. Use an individual **Change** button for a mismatch, or complete setup
for an unknown layout.
Check light trigger pressure without an L/R click and then the separate full
click. Recording is explicit and local; download includes raw samples and the
active setup prompt. Keep physical captures outside tracked files. No upload or
telemetry endpoint is added.

The input probe links the real Wasm/Aurora PAD boundary but starts no game or
renderer. After configuring an Emscripten build:

```sh
.venv/bin/cmake --build build/browser-public-selective-release --target controller_probe -j 2
python3 scripts/serve.py --directory build/browser-public-selective-release --port 8794
python3 -m unittest discover -s tests -p test_controller_input.py -v
node tests/controller_browser_test.mjs --url http://127.0.0.1:8794/ --playwright "$MELEE_PLAYWRIGHT_DIR" --out work/controller-boundary
node tests/controller_setup_browser_test.mjs --url http://127.0.0.1:8794/ --playwright "$MELEE_PLAYWRIGHT_DIR" --out work/controller-wizard
node tests/controller_setup_browser_test.mjs --url http://127.0.0.1:8794/ --playwright "$MELEE_PLAYWRIGHT_DIR" --out work/controller-wizard-axes --signed-dpad
node tests/controller_suggestion_browser_test.mjs --url http://127.0.0.1:8794/ --playwright "$MELEE_PLAYWRIGHT_DIR" --out work/controller-suggestion
node tests/controller_player_browser_test.mjs --url "$PUBLIC_PREVIEW_URL" --playwright "$MELEE_PLAYWRIGHT_DIR" --out work/controller-player
node tests/controller_player_browser_test.mjs --url "$DEVELOPMENT_RUNTIME_URL" --playwright "$MELEE_PLAYWRIGHT_DIR" --out work/controller-development
```

Use the installed Playwright package directory and Node executable. Tests launch
their own Chrome profile. The authored GameCube fixtures deliberately combine
known face/stick indices with constructed click/hat layouts; the suggested-profile
fixture uses the inspected HID descriptor shape. Neither is a physical Mayflash
capture. Ordinary iteration needs the fast normalization and
changed-boundary tests; a renderer/gameplay route matrix adds no controller
mapping coverage. Rebuild the public target and run its packaging/browser smoke
when changing the native transport or release graph.

## Evidence and remaining scope

- **Compiled:** public selective Release runtime and input-only Wasm probe.
- **Browser exercised, input boundary only:** standard buttons, axes, pressure,
  clicks, encoded hat directions/neutral, setup gating, reload persistence,
  four ports, assignment, disconnect/index reuse, focus/visibility and keyboard
  priority through real Wasm PAD. The separate 18-step UI sweep checks held
  controls, saved mappings and local prompt recording.
- Physical Mayflash/OEM acceptance remains open. The automatic Mayflash profile
  is a documented suggestion with provisional L/R digital clicks, not a certified
  hardware profile.
- Browser-visible Xbox/PlayStation/Switch controllers can use standard mapping;
  this does not certify every model, connection mode, OS or browser. Direct
  Nintendo adapters and Mayflash Wii U mode may not be exposed by Gamepad API.
  This change adds no USB driver/bridge. Browser-inaccessible hardware and
  pressure discarded by a driver cannot be repaired by remapping.
- Browser rumble, verified raw profiles and physical controller precision
  comparisons remain follow-up work. No gameplay, retail-equivalence or broader
  performance acceptance follows from these input tests.

Local validation on 2026-09-14 used Chrome 153.0.8010.36. The Wasm boundary
sweep took 0.21 seconds and the two complete setup layouts took 11.00 and 11.13
seconds after launch. These are test durations, not gameplay latency figures.
All seven boundary groups, both wizard runs, 13 existing native-input cases,
23 public-package cases and the six public-browser smoke groups passed. The
earlier 759-test suite ran with 31 skips and one outdated artifact-count assertion;
that assertion was updated for the three new controller files and its complete
four-test wrapper passed on rerun. No broad gameplay/timing matrix was run.
After adding the suggestion, all eight Wasm boundary groups pass, including
automatic Mayflash X/trigger/hat input without setup. The suggestion UI check
passes visible bindings, live X, one-control correction, occupied-input swap,
reload persistence and restore in about 1.5 seconds after launch. These are
software regressions, not physical controller latency measurements.
The player-source follow-up adds a ninth Wasm group covering P1 keyboard with P2
controller, the reverse arrangement, and disabling a slot. Its public UI check
uses the real compiled player with authored Gamepad values to cover automatic
activation, compact settings, both-keyboard selection, saved choices and closing
settings. It does not load a disc or claim gameplay acceptance.
The final player-source candidate passes all 759 Python tests (31 skips,
317.27 seconds), all nine Wasm groups, the packaged-player source UI test, the
six public-browser smoke groups and the 23-case public package suite. The final
package audit covers 22 files / 14,691,084 bytes, graph `9d88c5fbae4d3962`.

The production rollout on 2026-09-14 merged PR #22 as
`72def75a7c58292b5f7f2d3b04f70ad21b665f33` after both exact-head native CI
runs and public checks passed. The immutable production artifact is
[00ee2ca2.webmelee.pages.dev](https://00ee2ca2.webmelee.pages.dev); the apex is
[webmelee.gg](https://webmelee.gg/). Its manifest SHA-256 is
`89b4209fa9cbbb630a9f7d2e2ead93da63a8e541b9b6dc2227b781dd9ad4ec83`.
The production-configured artifact contains 23 files / 14,691,354 bytes.
Each hosted origin passed 21 exact resource checks and 32 forbidden/missing
routes, plus all ten public browser groups with an owned local disc. Authored
controller source-selection checks also passed staging and the apex. The user
reported that the physical-controller playable preview worked well; this does
not replace full hardware/retail precision acceptance.

Two initial staging verifier/test failures remain recorded locally: a stale
HTTP file inventory rejected the newly allowed controller modules, and a
keyboard smoke wrongly expected no Player 2 controller when Auto correctly
assigned the attached Mayflash there. The verifier now shares the producer's
explicit inventory; the keyboard smoke selects keyboard/off explicitly. Those
check fixes did not change the shipped player bytes.

The separate shared-settings follow-up passes all 760 Python tests (31 skips,
247.54 seconds), development and selective-public Release builds, both actual
entry-page controller UI checks, the older prototype's keyboard/B0XX interface
check, and all ten packaged public disc/menu lifecycle groups. The shared UI
checks cover source selection for either player, saved and denied storage,
legacy migration/session overrides, restored B0XX coercion, optional remapping,
focus and narrow layouts. They use authored Gamepad samples, not a new physical
controller or retail-equivalence capture. The local public package audit covers
24 files / 14,697,688 bytes with graph `4eabcc742109c682`; its HTTP check passes
23 exact resources and 31 missing routes, with the local Pages configuration
route limitation retained in the report. This follow-up is a separate PR and
has not been promoted to production.
