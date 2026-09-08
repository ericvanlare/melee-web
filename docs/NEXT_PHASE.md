# From a live fighter to a playable stock match

The constructor/neutral/restart gate is complete. The next concrete gate is one
controllable Mario on Final Destination in the browser, using the same original
fighter process loop, with original-game movement comparisons.

## Parallel work with bounded interfaces

1. **Browser runtime integration.** Share the owned runtime and asset bundle with
   a browser entry point. Present the actual fighter GObj and source stage through
   HSD/Aurora. Keep simulation at 60 Hz, independent of rendering; preserve pause,
   focus loss, explicit errors, teardown and restart. Avoid copying gameplay into
   the existing inspection animation player.
2. **Input and movement.** Feed the original controller path from the existing
   Aurora PAD boundary. Enable checked action scripts and services for Wait,
   walk/run, jump, fall and landing. Landing's effect/audio command needs real
   operands and providers before it is enabled. Extend through shared schema and
   service boundaries rather than fighter-specific successful substitutes.
3. **Reference traces.** Capture the original game's per-tick actions, position,
   velocity, collision state and RNG for identical initial conditions and input.
   Compare the Wasm runtime before claiming movement accuracy. Review floating
   point primitives where the PowerPC source and host math differ.

The lead integrates one runnable browser slice across these lanes and keeps the
constructor/restart regression passing. Full menus, AI, other characters and
content coverage are downstream of a local Mario-versus-Mario stock match.

## Acceptance for the next gate

Import local owned data, launch a visible Mario on Final Destination, control
movement and jump/landing through source callbacks, pause/resume safely, unload,
and restart without a reload. Require original state comparisons for the named
movement cases. Measure release-build frame intervals and memory on identified
browser/hardware; debug Wasm or viewer FPS cannot establish match performance.

## Foundation to reuse

- [Fighter runtime](FIGHTER_RUNTIME.md): owned common/fighter/costume/metal graphs,
  action identity, native commands, stage lights/bounds/collision, player data,
  item registration, effect banks and full GObj process/destructor execution.
- Checked archive extern preservation and typed named-section publication.
  Unsupported references and services still fail explicitly.
- Immutable original archive bytes, mutable owned native display lists and
  descriptor-ID cleanup across complete heap restarts.
- [Testing](TESTING.md): source ABI comparisons, focused native tests, real-data
  probes, browser rendering checks and fixed dependency pins.

Broaden behavior only when it unlocks this gate or catches a demonstrated bug.
Keep workers on independent files and use GPT-6 Astra medium as configured.
