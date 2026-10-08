# B4: inner geometry narrows the retained Bowser failure

**Compiled / Browser exercised**

[Issue #274](https://github.com/ericvanlare/melee-web/issues/274) remains open.
The [portable receipt](../evidence/b4-inner-geometry-boundary-v1.json) separates
the Release observer capture, its incomplete whole-session outcome, the offline
reducer and the full-suite validation.

The Release observer was built from native producer
`aea0d318a2734709b99d6be4901463c0f8bfbad1`; patch metadata canonicalization in
`afec01d` did not change the applied observer source tree. The browser harness
source is merge commit `159334762d696d208f9959056dc547e9360b7377`. Headless
Google Chrome on the Mac mini validated selected rows at source cursors 5238,
5239 and 5240. At cursor 5239, the retained row has 76 outer events and all 60
actual inner geometry calls, each with a false result.

The one capped capture requested stop cursor 5241, observed cursor 5244, and
exported 5247 source frames. It ended with owner exit 1 after manual unload; the
final CSS route and complete input timeline were not reached. The three selected
rows passed their validators, but the capture remains failed/incomplete and is
not whole-session evidence.

The retained-data reducer ran once from base commit
`159334762d696d208f9959056dc547e9360b7377` with the two dirty test sources
identified in the receipt's v2 run. That run passed 33 Python tests and one Node
test, and reproduced all 60 captured false results and the unchanged/instrumented
output bits: 52 reject at the axis-aligned bounds checks and eight at the solver
distance check. The reviewed v3 change only scopes the captured-output map to the
selected cursor; its two pure controls passed without rerunning the reducer.
Because the retained geometry is at cursor 5239, that filter preserves the v2
reducer result. Final implementation commit
`b7c6c9e83a84e2684c26a6f0cee5f65e8372897d` contains the v3 source.

This is a host-float sensitivity diagnostic; it does not establish PowerPC
equivalence or explain why the original and browser fighter states differ.

The full suite passed on that exact test commit: 2,109 discovered, 154 skipped,
1,955 passed, zero failures or errors, in 487.111 seconds. It skipped the
optional retained-capture reducer test; its retained log identity and the v2
and v3 source identities are in the receipt.

The original divergence remains source tick 4055 / browser index 5239: original
Bowser has 11 damage and `DamageFlyTop`, while the browser has zero damage and
`WalkFast`. The inner-geometry observation does not explain why those fighter
states differ or establish a fix.
Results, whole-session acceptance, original arithmetic equivalence, pixels,
PCM, physical input, live timing and performance remain open or separate gates.
