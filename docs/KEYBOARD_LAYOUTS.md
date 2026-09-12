# Keyboard layouts

Controls offers **2 players** (the existing default) and **1 player · B0XX**.
The choice and keyboard enable flags are remembered locally. No input preference
or disc data is uploaded. If browser storage is blocked, controls still work for
the current session. Switching the layout, disabling keyboard input, changing
P1's physical source, losing focus, and restarting clear held keyboard input.

One-player mode uses P1 only; a physical P2 controller can still be used. With
P2's keyboard disabled and no controller connected, the original CSS converts
that slot to CPU. The shared native match boundary accepts ordinary VS CPUs
at levels 1–9. Select the CPU character and difficulty through the original
CSS, then use Start to enter SSS. See [CPU opponents](CPU_OPPONENTS.md) for the
shared source/data ownership and validation scope. Port assignment is still
the existing runtime behavior.
Physical controllers retain the existing priority on their assigned ports.
Controller assignment and hardware validation in issue #5 remain open.

## Layout source

There is no single universal keyboard layout. The [Reddit discussion](https://www.reddit.com/r/SSBM/comments/krxalr/just_got_into_melee_loving_it_playing_with_a/)
points to the [B0XX keyboard project's default and community alternatives](https://github.com/agirardeau/b0xx-ahk/tree/7c070f8e0f135c8108cfb0af9a37dc6809070b15#default-controls).
This preset uses that project's default `hotkeys.ini` at commit
`7c070f8e0f135c8108cfb0af9a37dc6809070b15`, including its modifier keys.
Labels describe physical QWERTY positions, as the native input uses SDL scancodes.

| Action | B0XX keyboard |
| --- | --- |
| Left / down / right / up | 2 / 3 / 4 / ] |
| A / B | M / O |
| X / Y | P / 0 |
| L / R / Z | Q / 9 / [ |
| C-left / down / right / up | N / Space / comma / K |
| Mod X / Mod Y | V / B |
| Light / mid shield | minus / equals |
| Start | 7 |
| D-pad | Arrows, or both modifiers + C-stick |

The Controls table also lists the unchanged split-layout bindings, including
P2's Home/Page Up shields, Page Down grab, and numpad C-stick. P1's D-pad uses
Z/X/C/V while P2 keyboard is enabled, reverting to arrows when it is disabled.

## Mapping and integration

`boxx_input.h` adapts the pinned project's directional state and coordinate
tables under its [MIT notice](../licenses/b0xx-ahk.txt). It includes the two
modifier magnitudes, diagonal/shield angles, modifier + C-stick/B Firefox
angles, angled C-stick inputs, and light/mid shield values. Opposing directions
use last-press priority without reactivating an older held direction when the
newer one is released. Repeated keydown events do not change priority.

The mapping emits integer raw GameCube coordinates (`normalized × 80`) and
raw trigger values, before Melee's original controller processing. For example,
right is 80, right + Mod X is 53, right + Mod Y is 27, and a default diagonal is
(56, 56). L/R clicks set their digital bits and corresponding full analog
trigger; light/mid shield produce right-trigger 49/94 without a digital click.
These are mapping tests, not an original-game or hardware equivalence claim.

This is a B0XX-style keyboard preset, not a claim to implement current B0XX
firmware or tournament certification. We intentionally do not reproduce the
AHK/vJoy rounding workaround, Windows hooks, or stale event-dependent D-pad
and shield values: modifier layers and shield priority are evaluated from the
currently held keys. Mid shield takes priority over light; R click takes
priority over both. No timed macros or automatic gameplay actions are added.

`browser_input.cpp` watches the existing SDL key-event stream to retain press
order, then samples the mapping once at the existing `melee_web_input_poll()`
boundary. The keyboard provider does not change the source scheduler, replay
path or renderer. The raw sample and separate diagnostic clamp retain their previous
roles. The two-player path retains its existing Aurora bindings.

The temporary iframe adapter now has one native configuration dependency:
`Module._melee_web_input_set_keyboard_layout(0 | 1)`. The function validates the
layout and clears held input. `EMSCRIPTEN_KEEPALIVE` exports it without a build
configuration change. The shell handle exposes `setKeyboardLayout('two' | 'boxx')`;
the rest of the shell still does not access the child runtime directly. This is
not a diagnostic PAD-injection path, and it does not run another input timer.
After extraction, the shared runtime should own this configuration call as part
of its input-settings API. Both pages use the same compiled mapping.

An older runtime can still use the existing layout; choosing B0XX with a build
that lacks the setter displays an error and restores the previous selection.
Rebuild the runtime before assembling the updated prototype. The preview
packager includes the adapted code's MIT notice.

## Verification scope

Fixed-vector C++ tests cover modifiers, opposing directions and releases,
shield and Firefox tables, C-stick layers, trigger priority, and reset. Native
adapter tests cover event delivery, physical priority, one-player keyboard
isolation, focus/enable/layout resets, startup failure, and shutdown.

The HTTP browser test uses ordinary keyboard events and reads the existing PAD
diagnostic to verify key-to-input routing. It also checks preference restoration,
layout switching, modal focus, original CSS/SSS navigation, and Eject. This is
input-boundary evidence; it does not admit physical controllers, keyboard rollover,
controller-to-photon latency, a complete match, or performance. See
[prototype evidence](PROTOTYPE.md) for the recorded run.
