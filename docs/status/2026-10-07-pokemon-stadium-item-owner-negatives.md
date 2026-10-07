# Pokémon Stadium: item-owner prerequisite negatives

**Compiled / Source identified / Native traced**

The production public-data decoder now validates the full authored character
Article table extent before reading its entries. A synthetic short-table check
rejects a four-byte referenced region even when adjacent archive words remain
readable. The source count and fixture layout come from the existing C boundary.

One retained-C1-context preflight passed on the exact previously retained
98-file union, with every file size and SHA-256 verified before the run and
unchanged afterward. Before item preparation, it exercised missing input names,
missing public symbol and links, absent Random roots, a present Random root with
null native registration, five undersized decoded targets, and invalid color
extents. The x14 case explicitly allows public decoding of the readable pointer
and requires the color owner to reject the short extent. Capacity one is a
synthetic guard test using the actual parsed non-null row-one script consumer;
it does not establish an insufficient authored ItCo capacity.

The negative checks preserved all captured item global and public-data pointers,
source color pointer, Random state and script links, empty item link, generation
and ticks, RNG ownership/value, save card, CSS/SSS selection, raw ItCo/GrPs bytes,
and empty stage state. The existing two positive prepare/end lifetimes also
passed. The trace contains only its header, with no E8 or publication event.

The ordinary Release runtime built with the diagnostic flag **OFF**; the
separate Release native trace built with it **ON**, both from clean producer
`4852d072a3777070f778ecc893f34fa72fefb31a`. The trace's generic `browser-release`
header label does not identify an ordinary production profile. Full discovery
passed 2,013 tests with 135 skips and no failures under default fixture settings.
The retained-context and E8 fixture gates skipped before native launch in that
suite; the explicit retained preflight ran once separately.

The [portable receipt](../evidence/pokemon-stadium-item-owner-negatives-v1.json)
records the source/build/input identities, raw artifact hashes, preparation
failures and scope. The earlier pre-fix baseline binaries and raw compiler logs
were overwritten by the prior worker; its narrative and hashes are not retained
raw baseline evidence. Prior item-owner and E8 evidence remains preserved.

This completes the bounded negatives in [#216](https://github.com/ericvanlare/melee-web/issues/216)
and contributes prerequisite evidence to [#214](https://github.com/ericvanlare/melee-web/issues/214)
and [#164](https://github.com/ericvanlare/melee-web/issues/164). It does not admit
Stadium. No E8 request, `Stage_8022524C`, `OnInit`, `Ground_801C0800`, item startup,
stage process/draw/tick, browser, extraction, deployment, retail comparison, or
performance gate was run.

The current-main integration refresh uses clean producer
`9f9cf165373471ae0ab5e3296ac0477dfa0da703` on main
`af13467f09ee0245bceeb7c80fb4ede698ec5f69`. Its executable patch is equivalent;
all nine item-owner source/helper files are unchanged. Both affected Release
builds passed with ordinary diagnostic OFF and native trace diagnostic ON.
Full discovery passed 2,019 tests with 135 skips and no failures. All 98 retained
input identities still match. The native fixture was not rerun: its observed
preflight stays bound to producer `4852d07`, separately from this refresh's build
and suite validation. The receipt's `current_integration_validation` section
records the new producer and artifact/log hashes.
