# Four ports in public Controls

**Compiled / Browser exercised** — the shared public and development Controls
dialog exposes four independent controller sources. The public defaults remain
Auto for Players 1/2 and Off for Players 3/4; choosing Auto or Controller only
enables either extra port. The [receipt](../evidence/public-four-port-controls-v1.json)
binds source, builds, tests, package and browser reports. This addresses the
Controls component of #170 and tracker #158; physical-controller and complete
doubles-session acceptance remain open.

## Implementation and reuse

The existing controller manager already owns four-port assignment, PAD sampling,
neutral disconnects and reconnects. The shared settings component now renders
four source rows and persists all four choices. Players 3/4 offer Auto,
Controller only and Off; keyboard layouts remain limited to Players 1/2 and
touch input to Player 1. Older two-player preferences preserve the entry's extra
port defaults, and invalid stored extra-port keyboard/touch choices fall back to
those defaults. No simulation, source input sampling or native port rule changed.

The existing controller browser harness was extended instead of adding a second
input manager or validation framework. Documentation is in
[Browser controllers](../CONTROLLERS.md).

## Observed validation

Both Release targets (`runtime` and `runtime-public`) built successfully. The
full suite passed 1,919 tests with 141 skips. The tested build/suite source was
rebased onto merged main with an identical Git tree before packaging and browser
checks; the receipt retains both commit identities.

Headless installed Chrome exercised the real development page and audited public
preview through HTTP. Four authored standard Gamepad API devices produced four
unique port assignments and distinct PAD button samples. The checks also cover
P3 Off/re-enable, P4 disconnect/reconnect, persisted choices, legacy preferences,
invalid stored extra-port sources, unavailable storage, narrow layouts and focus.
The four-row screenshots on both surfaces were inspected and remain private
run evidence. These authored samples are not a physical-controller capture.

The public package audit and 19-check public lifecycle harness passed using the
owned disc. That route includes original CSS/SSS, supported Mario/Final
Destination gameplay and a harness-induced No Contest through Results to CSS,
plus menu/eject/reimport and settings/network checks. It uses the existing
audio-disabled public profile and does not establish natural-match or audio
acceptance. The 32-file development HTTP inventory and 34-file public package
inventory were unchanged before and after the browser checks. Owned servers and
browsers closed after validation.

The first full-suite attempt is retained separately: it used the pre-migration
Aurora tree and the old hardcoded capture-inventory comparator. The exact known
predecessor was migrated through standard bootstrap, and the merged comparator
fix was included before the successful suite. No failed evidence was overwritten.

## Remaining gates

Physical controllers, a four-human match, sparse active ports, original-game
pixel/PCM comparisons, foreground performance and uninterrupted audio each need
their own evidence. This change neither deploys a public release nor closes
competitive-set acceptance.
