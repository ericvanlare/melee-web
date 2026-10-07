# Pokémon Stadium: C0 typed catalog source/native trace

**Compiled / Source identified / Native traced**

One catalog-only preparation trace passed on the previously retained
[`GrPs.usd`](../evidence/pokemon-stadium-c0-typed-catalog-v1.json) input
(1,461,024 bytes, SHA-256
`aa740cfbbeced294f058449caca8ac0380532521dde06ca4a6dcc03080cf6a6d`). It
used producer commit `afa7b61df57bdfafc48205eaa9c613141de60c0b`, tree
`ba848024be7cab3835f14ed3e11db54470edcb90`, and the two focused RelWithDebInfo
targets identified in the receipt. The command was
`node build/browser/pokemon_stadium_c0_trace.js --probe catalog
assets-local/stadium-c0-20261006/GrPs.usd`, with a 60-second deadline; it
completed in 0.499 seconds with child and runner exit code 0. The receipt binds
source, targets, tools, command, output, and unchanged-before/after identities.

The catalog owned the listed collision/Ground/yaku/particle/texture/quake roots
and verified each authored Ground light pointer-slot count against the native
map count: `[2, 2, 2, 0, 0, 2, 0, 0, 0, 0]`, sum 8. Slots count per authored
entry even when pointers alias. `itemdata` has a present symbol but null root,
so this is not evidence of an authored empty item list. `map_plit` was present
but is not separately owned on this native-map route. The source data remained
unchanged; neither the native map nor a particle bank was published.

The first frozen packet expected 24 Ground light descriptors and failed after
observing 8. That failed packet and result remain historical evidence. Source
review showed 24 counted the separate bounded map `light_override_table` rows.
The corrected check derives the Ground slot vector from authored entries and
compares every entry with `DatNativeMap::source_light_counts()`; it does not
hardcode the observed vector. The [receipt](../evidence/pokemon-stadium-c0-typed-catalog-v1.json)
preserves both packet identities and the independent review hashes.

This is a C0 typed-catalog prerequisite only. It did not perform the E8 source
archive lookup or observe C1 selected-file closure; it did not query SIS, publish
stage data, enter `Stage_8022524C`/`on_init`, run a browser, or make a gameplay
or admission claim. Stadium remains disabled. The actual C1 E8 request and its
selected-file lifetime remain open under [#203](https://github.com/ericvanlare/melee-web/issues/203)
and the broader [#164](https://github.com/ericvanlare/melee-web/issues/164).
The next probe must observe that request and stop before `Stage_8022524C` or
`on_init`, then verify teardown.
