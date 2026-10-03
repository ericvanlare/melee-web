# Fixed browser surface dimensions — issue #134

**Source identified / Compiled / Browser exercised** for keeping the browser
render surface at its configured pixel dimensions while CSS fits the display.
The [surface receipt](../evidence/mobile-browser-surface-v1.json) binds the
high-DPI allocation comparison, startup/rotation/fullscreen/input checks,
audited public package route, full suite and Release builds to their artifacts.
Source clocks, timing limits, audio and draw order are unchanged.

The new recording shows original character select immediately before Chrome's
page-error screen. It provides no OS termination reason. The measured graphics
allocation reduction is a component fix; physical iPhone crash confirmation
remains open. Desktop WebKit still produces a forced timing pause with the
smaller surface, retained as a separate failure. Headless functional checks do
not establish mobile performance, uninterrupted audio or whole-session
acceptance. No merge or deployment is included.
