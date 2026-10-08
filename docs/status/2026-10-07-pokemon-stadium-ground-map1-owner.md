# Pokémon Stadium: map-1 Ground ownership lifetime

**Compiled / Source identified / Native traced**

One bounded diagnostic run exercised the original map-1
`Ground_GetStageGObj(1)` constructor and `Ground_801C4A08` removal, after the
existing CSS/SSS selection and a single typed E8 request. The 64-byte Ground
storage lease was retired, the full typed ftDevice snapshots were restored,
and the checked source, selection, save and archive teardown completed. The
run recorded one `stadium_e8_request_returned` event. It ran on macOS 26.6
arm64 with Node.js 24.19.0 against the Emscripten wasm32 C1 diagnostic target.

The corrected harness validates all 20 authored marker pairs against their own
resolved root; none matches the selected map-1 root. It therefore requires an
empty `StageInfo.x280` baseline and confirms it remains unchanged. The output
reports zero map-1 marker pairs, a 64-byte storage request, and one
constructor/removal lifetime. Original Ground callbacks were not dispatched;
there were no process ticks or rendering. `StageInfo.xA0` was observed as 160
only and was not compared with an expected value.

The retained 98-file fixture matched its pre-run inventory both at invocation
and after the owned process group exited. The full Python suite passed 2,041
tests with 133 skips; it did not configure the fixture root or map-1 gate, so
the retained map-1 test was skipped in that suite. The affected C1 diagnostic
Release target also built successfully with its diagnostic option ON and the
other nine `MELEE_WEB` boolean options OFF.

The current diagnostic producer is `13d93caa` (tree
`b5ae431c`). Ordinary `gameplay_menu_browser` Release artifacts are from the
separate producer `0c3c932a`; they were not rebuilt or relabeled as current
diagnostic output. The portable [receipt](../evidence/pokemon-stadium-ground-map1-owner-v1.json)
records both producers, the retained run identities, and the root review.

The earlier v4 attempt stopped before the constructor because the harness
misread the authored marker root and expected a generation after its allocation
record had been erased. That failure remains preserved and is not constructor
evidence. The corrected v5 attempt passed the single component lifetime.

This does not exercise full Stadium startup, `Stage_8022524C`, Stadium
`OnInit`, map 0 or map 2, callbacks, collision startup, gameplay ticks, draw or
render, browser behavior, retail comparison, timing, audio fidelity, or public
stage admission. It is not whole-session acceptance; Stadium remains disabled.
