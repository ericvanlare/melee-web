# Pokémon Stadium: C2 item-state owner preflight

**Compiled / Source identified / Native traced**

A retained-C1-context diagnostic preflight completed two source-item runtime
`prepare_source`/`end` lifetimes using the authored ItCo roots and the retained
Stadium `ALDYakuAll` commands. It used the existing `DatItemRegistry`, native
Article registry, `DatItemArticle` Random schema, public-data decoder, and color
owner. Before any Ground setup, each non-null script consumer in rows 1–7 was
checked against the authored Random Article `state_count()`. The preflight
confirmed the exact registered Random Article and state-table pointers, left
all Stadium script pointers untouched, and refused a competing active item
owner.

For each lifetime, `runtime_end` ran while its archive, arena, registry,
public-data, Article, and color owners remained alive. The seven captured item
runtime global pointer values and source color pointer were restored before
owner destruction. The native checks also confirmed the item entity link stayed
empty, gameplay generation and tick count did not change, source RNG owner/value,
save card, and CSS/SSS selection were unchanged, and raw `GrPs.usd` bytes were
unchanged. The trace contains only its header: no E8 request, stage publication,
or source menu entry occurred.

The affected Release diagnostic target built from commit
`5cf2f859f7eb9a2d3fb91f63bfb3258d86c188ac`. The explicit retained-fixture test
ran once and passed with all 98 fixture names present. Full discovery then
passed 2,012 tests with 135 skips and no failures using default fixture settings;
the item-state and E8 fixture traces skipped before native launch because no
98-file fixture campaign was selected. E8 was not rerun. The first build attempt
was retained; its C++ inclusion of an upstream C-only item header failed, and
the corrected diagnostic reads those source fields in the existing C probe.

This is only an item-owner prerequisite. Missing ItCo/Random Article, malformed
root, and insufficient-capacity negative cases were not executed, so [issue
#214](https://github.com/ericvanlare/melee-web/issues/214) remains open. The
result does not establish stage readiness, admission, or gameplay. The portable
build/fixture/test identities, assertions, exclusions, and retained artifact
hashes are in the [receipt](../evidence/pokemon-stadium-c2-item-state-owner-v1.json).
No Ground storage, `Stage_8022524C`, `OnInit`, `Ground_801C0800`, item startup,
stage process/draw/tick, or later IMAGE/DatSis/auxiliary GObj work was run.
