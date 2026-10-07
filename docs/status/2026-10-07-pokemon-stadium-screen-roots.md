# Pokémon Stadium: canonical screen IMAGE and writable SIS roots

**Compiled / Source identified / Native traced**

`DatNativeMap::image_descriptor(source_offset)` now borrows only the canonical
IMAGE already present in its checked map texture graph. A missing graph member
throws, with no second hydration or fallback decoder. `DatSis` remains unchanged.

The retained diagnostic preflight resolved the original dummy IMAGE and SIS
public names through source `HSD_ArchiveGetPublicAddress` on an explicit typed
handle. Independent C traversal of map 1 found exactly one reference to the same
IMAGE pointer. The IMAGE retained its C0 offset and 16×16 I4 identity. The SIS
retained its 22-slot / 88-byte table; a table slot and an existing owned text byte
were safely changed and restored through the published pointer. Synthetic
fixtures also checked source aliases and canonical IMAGE aliases across map rows.

Synthetic missing roots, non-graph and foreign-owner IMAGE candidates, malformed
SIS, duplicate registration, live-handle scope close and wrong-scope close all
rejected. Existing focused archive controls preserved fatal unknown, released and
NULL-descriptor behavior. Two sequential retained owner/catalog lifetimes passed,
with handle/catalog teardown before owner destruction. Guards preserved raw and
decoded archive copies, save, selection, RNG owner/value, item globals/link,
generation/ticks and empty stage state. The trace contained only its header.

The first native experiment at `149d9fe` failed before map/SIS checks because the
test compared a `ResolveNull` decoded copy against raw input. That constructor
intentionally clears validated external-link slots in its owned copy. Two reduced
DAT tests passed before the test-only correction: raw bytes still have their own
invariant, and decoded bytes now have a post-constructor baseline. The second
native experiment passed at `bc02190b3e4be7566f34a6fd9cbe1ef06bf5b388`; both runs
and all raw artifacts remain retained. All 98 input sizes and SHA-256 identities
were unchanged and independently rechecked.

The current-main integration producer is
`ef2de4ce4d929c112011461f55de4c6e30f439f9` on main `15fd219`. Its 11 source files
are identical to the passing native producer. The ordinary Release runtime built
with diagnostic **OFF**. The historical native build used Release diagnostic
**ON**; its generic `browser-release` trace header does not identify the profile.
Full discovery passed 2,021 tests with 136 skips and no failures under default fixture settings. The retained
preflight was not repeated for integration.

The [portable receipt](../evidence/pokemon-stadium-screen-roots-v1.json) records
source, build, input, log and artifact identities. This completes the bounded
root-ownership prerequisite in [#223](https://github.com/ericvanlare/melee-web/issues/223)
and contributes to [#164](https://github.com/ericvanlare/melee-web/issues/164).
No E8, Stage2524C, OnInit, Ground, global SIS binding, screen/copy buffers, stage
process/draw/ticks, browser, extraction, deployment, retail comparison, performance
or stage admission was exercised by this prerequisite.
