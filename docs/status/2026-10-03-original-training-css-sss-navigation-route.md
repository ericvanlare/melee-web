# Original Training CSS/SSS navigation route

The current menu audit confirms the VS rules and item-settings route is still
partial: `mnmainrule`, `mnruleplus` and `mnitemsw` are in the source build, but
the browser host does not own their original entry/back path or settings-to-match
handoff. This Training navigation slice was already underway and is independently
bounded at the source CSS/SSS states. The next route slice should prioritize the
original VS rule/item path through a settings change, real match and Results/CSS.

**Source identified / Compiled / Browser exercised** for the bounded original
1P Training menu route: CSS → Main → the source `SEL_1P_TRAINING` callback →
`GM_TRAINING` → original Training CSS → original Training SSS; SSS B returns to
Training CSS, CSS B returns to Main, and repeated entry/exit succeeds. The
browser selects Mewtwo through CSS geometry and PAD samples, selects Final
Destination through SSS geometry and PAD samples, then reaches the explicit
unsupported `GS_TRAINING` state-2 boundary before its one-player simulation is
entered. Eject and clean reimport recover after that boundary. The
[scoped receipt](../evidence/training-menu-route-v1.json) identifies the
source states/callbacks, assets, browser screenshots, input and artifact hashes.

Training mode is `GM_TRAINING` (`0x1C`); its authored mode-state ids 0 and 1
use `GS_CSS` (`0x08`) and `GS_SSS` (`0x09`), while id 2 is `GS_TRAINING`
(`0x04`). The route uses `mn_8022D7F4` for the 1P Training selection,
`gm_Mode_Training_OnInit`/`OnLoad`, the source CSS/SSS scene callbacks, and the
mode callbacks `gm_801B1B74`/`gm_801B1C24`, `gm_801B1EB8`/`gm_801B1EEC`, and
`gm_801B1F70`/`gm_801B2204`. The host stops before the state-2 OnEnter because
Training's simulation, HUD/options, item controls, reset and CPU services are
not integrated. Its error remains explicit.

This is source and real-rendered browser evidence, not retail equivalence. The
retail Training route capture was unavailable in this host session; only the
owned Rev. 2 CISO identity was verified. Headless Chrome used a fresh isolated
Everything-unlocked session and synthetic keyboard/PAD samples. Personal-save
progression and save writes were not exercised; physical input, audible quality,
pixel comparison and performance remain unrun. The host audio output was muted
for capture. The Training row in the [full-game inventory](../full-game-inventory.json)
therefore remains acceptance-unassessed.
