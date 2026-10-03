# Main Settings > Rumble route

**Source identified / Retail compared / Browser exercised / Compiled** for the
original Main → Settings → Rumble route and its return to CSS. The retail
Observer trace records Main/Settings/Rumble/Settings/Main menu IDs
`0/4/19/4/0`; ordinary A disables Controller 1 rumble and B backs out through
both original pages. In a fresh isolated Chrome context, the public player
renders the original Settings and Rumble scenes, changes the source SaveData
rumble bytes at offset `0x458` from `[1,1,1,1]` to `[0,1,1,1]`, autosaves,
re-enters the route, returns through VS selection to CSS, exports the same
preference, and retains it after document reload. The source menu owner unloads
cleanly. The [scoped route receipt](../evidence/main-settings-rumble-route-v1.json)
indexes the retail trace, browser package identity, input, save bytes, screenshots
and retained failure runs. The [route capture notes](../ORIGINAL_MENU_ROUTE_CAPTURE.md#main-settings-rumble-route)
separate source state, rendered browser evidence, persistence and lifecycle.
Retail video/audio were disabled, and the browser package is audio-disabled;
this does not establish pixel or audio equivalence, physical input, foreground
timing, performance, or coverage of other Settings pages.
