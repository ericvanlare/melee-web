# Pokémon Stadium diagnostic profile and OnInit boundary

**Compiled / Synthetic controls / Source identified**

The diagnostic-only Stadium profile and profile-to-`DatNativeMapContract`
adapter compile in the C1 trace target. Focused profile and borrowed-yakumono
controls passed. The prototype parser accepts only the explicitly scoped,
marked diagnostic row; the ordinary manifest still contains its seven existing
stages and excludes Stadium.

The corrected source producer passed the standard suite (**2,049 tests, 133
skipped**) and the default-off ordinary `gameplay_menu_browser` Release build.
The suite included the existing retained C0 map-owner regression and ran both
OnInit source-boundary controls. The separate map-1 constructor test remained
skipped behind its explicit gate. Its 98-file fixture remains present in its
owned local resource location and was not selected or configured by this suite.

Source review found that the later OnInit fixture needs a dedicated owner for
the map-1 display callback's three text-class GObjs and borrowed SIS slot 1.
`melee_web_stage_last_end` removes stage-class Ground objects, cameras and
lights; it does not own these callback-created text/SIS objects. The map-1
Ground removal callback is empty. Therefore this result does not establish a
safe executed OnInit/teardown lifecycle, stage admission, camera setup, OnStart,
scheduled callbacks, rendering, or browser behavior. The cleanup boundary and
its partial-init ordering need review before any actual OnInit attempt.

The [portable validation receipt](../evidence/issue251-stadium-profile-oninit-v1.json)
binds the corrected source/tree, focused C1 producer, suite and ordinary build
logs, cache options, affected object/output hashes, retained failures, and the
source-only cleanup audit. Raw logs and the prior-artifact preservation
manifest remain under external run ID `issue251-oninit-profile-20261007`.
