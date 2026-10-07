# Pokémon Stadium checkpoint 0: source-data structural trace

**Compiled / Source identified** for the owned USA revision-2 disc's English
Stadium base, four transformation archives, Japanese base localization metadata,
two HPS streams, and English SSM bank. The
[portable receipt](../evidence/pokemon-stadium-c0-structural-v1.json) binds the
observed FST ordinals, source ranges, file sizes, input hashes, transformation
padding, committed source tree, target hashes, and two identical outputs from
fresh Node/Wasm processes on a macOS Mac mini. This is a source-data structural
checkpoint; it does not establish a running stage or admission.

The trace reuses the checked `DatArchive`, `DatSis`, stage/model, animation,
light, collision, effect, and audio readers. A small C snapshot exposes the
source `GroundParam` fields to C++ without changing upstream headers. It checks
map provider rows, anonymous local target bounds, external names and slots,
authored table counts, stage parameters, marker identities, collision bindings,
light rows, particle bank, quake model, SIS title lengths, and audio identities.
The batch asset checker adds an explicit `resolve_null_externals` option using
the existing archive policy; rejection remains the default and unresolved links
are reported. This option preserves the original loader's null-link semantics
and does not supply missing runtime services.

Three full attempts failed before the successful pair. The first assumed that
an external animation remained present after null resolution; a six-archive
probe separated local, external, and absent references. The second required
public names on local map targets; a reduced probe showed anonymous targets and
the source's resident-row selection by non-null joint. The third assumed equal
metadata offsets throughout both localized archives. A bounded SIS comparison
confirmed the matching pre-SIS prefix and externs, and identified a 7,520-byte
shift in the trailing quake public offset and 37 relocation slots. The final
trace asserts that shift and reports whole-archive metadata inequality. The
reduced diagnostic's prefix scan was also corrected to begin at byte zero.
All failed full receipts and reduced probes remain in the external task-run
inventory described by the portable receipt.

Validation uses the explicit trace build target and
`scripts/check_pokemon_stadium_c0.py`; the latter opens the owned disc read-only,
requires a clean committed checkout, verifies source/target identities before
and after both processes, and removes only its successful extraction scratch.
The focused build-selection tests and complete Python test suite accompany this
checkpoint. The affected Node/Wasm target was rebuilt; no player build is needed
for this trace-only integration.

Original CSS/SSS selection, stage initialization, asynchronous transformation
loading, rendering, PCM fidelity, retail equivalence, physical input, and live
performance remain unrun. Stadium stays outside the content registry and match
gate. Checkpoint 1 needs a reviewed development gate, original CSS → SSS driven
by raw PAD input, observed `StKind = 3`, and the source language request for
`GrPs.usd`, with exact manifest preparation and explicit later-boundary failure.
See the [Stadium checkpoints](../content/POKEMON_STADIUM_PORT_NOTES.md) and
[stage admission contract](../ADDING_STAGES.md).
