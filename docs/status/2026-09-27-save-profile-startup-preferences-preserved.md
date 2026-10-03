# Save-profile startup preferences preserved

**Source identified / Native traced / Browser exercised / Compiled** for the
startup boundary that originally replaced saved item frequency, the eight-byte
item mask and rumble preferences before Personal autosave. The source host now
captures those typed preferences before applying runtime defaults and overlays
them on each live source snapshot; progress and other source fields remain
live. Its first Personal commit also uses original fresh-profile preferences
when IndexedDB has no prior profile. The Everything baseline clears the
session-only completion notifications raised while building completed save
state, using Melee's original transient reset routine. The
[preference regression receipt](../evidence/save-profile-startup-preferences-v1.json)
records the final audio-player package identity, fresh Personal first save,
synthetic custom-preference import/autosave/export/reload check, and integrated
audio lifecycle run. The custom-preference fixture is synthetic; the existing
[Dolphin interoperability receipt](../evidence/save-profile-dolphin-roundtrip-v1.json)
remains the independent game-written save evidence. Atomic storage protects
committed generations; it cannot preserve progress that was never committed
before a forced close.
