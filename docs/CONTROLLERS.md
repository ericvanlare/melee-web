# Browser controllers

The public player and shared development runtime normalize physical input once,
at the existing PAD sampling boundary. Browser `mapping: "standard"` supplies the
layout for ordinary controllers. Face buttons use positions: south → A,
east → B, west → X, north → Y. Right shoulder → Z; triggers → L/R.
Controls allows an explicit custom mapping when that convention is unwanted.

Unknown raw layouts and GameCube identities require setup. The previous browser
SDL fallback guessed a generic layout, which could interpret Mayflash raw X as A.
Native SDL controller database entries are not portable browser mappings. The
browser adapter also retains floating-point HID hats rather than narrowing their
out-of-range neutral value into a signed joystick axis.

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
is available under **Controls** in the public player. Connect the adapter, press
a button to expose it to the browser, run setup, then inspect every control.
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
```

Use the installed Playwright package directory and Node executable. Tests launch
their own Chrome profile. The authored GameCube fixtures deliberately combine
known face/stick indices with constructed click/hat layouts. They are not actual
Mayflash capture evidence. Ordinary iteration needs the fast normalization and
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
- Physical Mayflash/OEM acceptance remains open. There is no new built-in raw
  Mayflash profile without a complete physical sweep.
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
759-test suite ran with 31 skips and one outdated artifact-count assertion;
that assertion was updated for the three new controller files and its complete
four-test wrapper passed on rerun. No broad gameplay/timing matrix was run.
