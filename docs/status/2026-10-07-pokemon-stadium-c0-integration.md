# Pokémon Stadium C0: integration validation and main equivalence

**Compiled / Source identified**

The full Python suite and ordinary private Release runtime build passed on the
C0 catalog producer tree atop the B4 PR head. After PR206 merged, `main` had the
same tree as that stack base. The catalog branch was rebased onto `main`; its
tree and the Release artifact hashes exactly match the validated producer. The
suite and build were not rerun after the rebase. The [receipt](../evidence/pokemon-stadium-c0-integration-v1.json)
records the producer, stack base, post-merge tree comparison, test totals,
effective Release options and target hashes.

This is integration/build-health evidence only. The earlier [C0 typed-catalog
trace](2026-10-07-pokemon-stadium-c0-typed-catalog.md) remains the sole catalog
trace result. This integration pass did not call E8, establish selected-file
closure, publish Stadium stage data, enter `Stage_8022524C`/`on_init`, or make a
playability or admission claim. Stadium remains disabled under [#203](https://github.com/ericvanlare/melee-web/issues/203).
