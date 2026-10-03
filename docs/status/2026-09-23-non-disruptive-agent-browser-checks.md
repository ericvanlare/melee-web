# Non-disruptive agent browser checks

The public player preserves a centered 4:3 game rectangle above its wrapping
toolbar. **Browser exercised** presentation evidence and retained failures for
the production shell, DPR, resizing and DOM fullscreen are recorded in the
[presentation receipt](../evidence/public-presentation-aspect-v1.json) and
[resize guide](../PUBLIC_RESIZE.md). OS fullscreen/focus remains unrun.

Routine browser checks default to headless installed Chrome; foreground
captures require explicit `--headed`. Agent guidance also covers temporary
scripts. **Browser exercised**, bounded functional utility: GPU output,
screenshots, input, diagnostics, player lifecycle and PCM inspection retain
their scope in the [browser automation guide](../HEADLESS_BROWSER_VALIDATION.md).
Initial Import now waits for renderer readiness from post-main native frames.
The [implementation receipt](../evidence/headless-browser-defaults-v1.json)
binds fresh builds, the full suite, passing migrated browser checks and the
macOS observation with no test-browser window or focus interruption. The
original [investigation receipt](../evidence/headless-browser-compatibility-v1.json)
retains the early-import failure. Headless results do not admit gameplay or
replace foreground timing, physical-device, audible-output or
original-comparison evidence.
