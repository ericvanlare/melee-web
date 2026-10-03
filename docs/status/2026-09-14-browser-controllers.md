# Browser controllers

The browser controller candidate adds browser-standard mapping, a suggested
Mayflash 0079:1843 mapping for Chrome/macOS's 16-button/10-axis raw layout,
individual binding corrections, and complete setup for unknown layouts. The
suggestion adapts SDL definitions to the inspected USB descriptor and Chromium
indexing; digital L/R clicks remain provisional. Separate GameCube trigger
pressure/clicks and fast input-only browser regressions pass. The public player
defaults to controllers when recognized, with per-player keyboard overrides in
compact Controls settings. Session-local trigger-origin handling fixes activation
with the attached Mayflash's nonzero rest values. The user reports that the
physical Mayflash/OEM controller worked in the playable preview. PR #22 merged
as `72def75a7c58292b5f7f2d3b04f70ad21b665f33` after both exact-head native CI
runs and public checks passed. The fix is live at
[webmelee.gg](https://webmelee.gg/); its immutable production artifact is
[00ee2ca2.webmelee.pages.dev](https://00ee2ca2.webmelee.pages.dev). Hosted
artifact, disc/menu lifecycle, controller and recovery checks pass. Digital
L/R clicks, full physical gameplay and retail input precision remain unaccepted.

A separate follow-up shares the compact Controls component between the public
player and development `runtime.html`, including source choices, layout,
preferences, optional remapping and focus behavior. Development diagnostics and
audio remain separate.
See [controller scope and verification](../CONTROLLERS.md).
