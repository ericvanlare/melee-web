# Pokémon Stadium: C1 E8 request and checked teardown

**Compiled / Source identified / Native traced**

One diagnostic trace completed the original E8 request for Stadium `StKind 3`
and checked typed teardown. The native executable was built at commit
`0a668403b75be645f2226599c8927b42b32fee3d` (tree
`f3928e2884722f1aecf754342e489fa8f00a7434`). The test launched it from commit
`21aa898cd0f4a9977743cd2445413deb33e9a8cb` (tree
`ce3cf192d42b84f65f94fabbe03ec0f9c5c2909c`); the intervening changes touched
Python test and preflight code only. The [portable receipt](../evidence/pokemon-stadium-c1-e8-request-v1.json)
records both identities, source and output hashes, fixture closure, assertions,
and retained run-bundle checksums.

The reviewed fixture union had 76 unique menu names and 36 selected-file names,
for 98 unique files with none missing. The trace recorded one
`stadium_e8_request_returned` event. It observed `/GrPs.usd` through both source
size and typed-open calls, resolved the typed roots, confirmed the authored
empty `itemdata` and `map_plit` roots, preserved source seed and save ownership,
and completed checked teardown. The `StageInfo.x6E4` source effect was checked;
`StageInfo.xA0` was observed as `160` only and was not compared with an expected
value.

The separately reviewed reopened-context lifecycle preflight first failed at a
combined alias/category guard before recording lifecycle observations. After a
narrow guard correction, the lifecycle-only trace passed at
`767258ee4879cb2ac2a0b3a6a2427e570850ab2a`; that run did not call E8. The later
single E8 run passed with the corrected precondition. Both receipts and the
retained first failure are identified in the portable receipt.

This is source-request and teardown evidence for [#203](https://github.com/ericvanlare/melee-web/issues/203),
not stage admission evidence. The trace did not construct stage objects, publish
Ground or StageItems state, enter `Stage_8022524C` or `on_init`, run stage
callbacks, admit a match, or enter gameplay. Pokémon Stadium remains disabled.

For integration, the five existing commits were rebased onto main commit
`2bb56a5f1707f2723f51d1a23e65e73f8658da28`; `range-diff` found those patches
unchanged. The first full Python discovery exposed that the standalone lifecycle
test checked only menu files before launch; its native preflight failed because
selected `PlCo.dat` was absent from the default partial fixture. The failure is
retained. The guard now checks the authored 98-name union before launch, and a
synthetic test verifies that missing `PlCo.dat` is caught. This test-only guard is commit
`704571dfe08f6a08e13e70e67e3685eb0b6d843b`. The repaired full discovery passed
1,998 tests with 134 skips, and the affected Release diagnostic target built
successfully. In that
suite the incomplete-default lifecycle preflight skipped before native launch,
and the E8 test skipped because no fixture campaign was selected. The C1a
raw-PAD selection test ran and passed. The initial failed suite and the repaired
suite/build logs remain in the retained external integration bundles identified
by the [portable receipt](../evidence/pokemon-stadium-c1-e8-request-v1.json)
and its linked integration result manifest.
