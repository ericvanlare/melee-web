# Explicit stage-profile map ownership contract

**Compiled / Synthetic controls / Source identified**

Canonical stage profiles now state how their DAT map ownership is resolved. The
seven existing profiles explicitly select the current all-resident and
archive-derived behavior. A new authored policy must provide complete typed
resident, external-reference, and flagged-object ownership declarations; an
unspecified or contradictory policy fails closed. The adapter feeds the
existing `DatNativeMapContract` and hydrator, preserving their authored bounds
and identity checks.

Focused Release diagnostic controls passed for accepted and rejected authored
declarations. The standard Python suite passed (2,046 tests, 133 skipped),
including its existing retained C0 Stadium map-owner regression. The ordinary
`gameplay_menu_browser` Release target also built successfully with all ten
`MELEE_WEB_*` diagnostic/policy options OFF. Its output and the separate C1
synthetic-control build are recorded as distinct producers in the
[portable receipt](../evidence/issue254-profile-map-ownership-v1.json).

Earlier evidence is retained and scoped: a missing `cmake` PATH entry prevented
one build from starting; a subsequent compile exposed an invalid `constexpr`
test value; the first synthetic fixture run exposed a duplicate relocation
slot; and the first full-suite attempt was intentionally interrupted during
scope clarification. The corrected focused run and complete suite then passed.

This validates the profile-to-contract adapter and synthetic controls. It does
not establish a Stadium profile, authored Stadium map hydration, material-graph
hydration, original Stadium OnInit/admission, callbacks, rendering, browser
behavior, or whole-session acceptance. See the receipt for exact producer,
artifact and retained-log identities.
