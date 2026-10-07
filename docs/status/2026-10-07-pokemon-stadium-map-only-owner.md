# Pokémon Stadium: map-only native ownership probe

**Source identified / Native traced** for one diagnostic C0 Stadium map-only owner
trace using the retained English GrPs.usd archive. The
[portable receipt](../evidence/pokemon-stadium-map-only-owner-v1.json) binds
the source, producer, Wasm target, exact focused test, and explicit exclusions.

The trace constructs DatNativeMap from the C0 archive under the explicit
10-row map contract. Four authored rows (source IDs 0, 1, 2, and 5) are
resident; 26 external row/field references and all 44 flagged-object slots are
checked against their authored identities, including external null names,
field offsets, and order. The check covers resident map objects, bounded Ground
light tables, animation flag bytes on imported rows, null-slot order, and
repeated material aliases. Two ownership cycles verify teardown and recovery
after a rejected partial allocation. The resolved DAT data section is compared
with a pre-owner baseline, and the raw archive file is independently reread to
check full-file immutability.

The shared structural marker decoder preserves authored pair order, including
duplicate IDs and absent stage-specific markers. The synthetic trace feeds the
decoded table through the original Ground setter and verifies that the last
duplicate wins. Existing admitted stages retain the strict marker wrapper and
its readiness requirements; the focused Final Destination regressions pass.

The focused test passed once with the pinned input and clean tracked producer.
The earlier attempt is retained; it exposed a test comparison between
DatArchive::data() (the resolved data-section view) and the complete file.
The corrected assertion compares like-for-like data-section bytes and checks
the complete raw file separately. Synthetic marker-order and focused FD
numeric/map regressions passed in separate runs recorded in the receipt.

This trace directly exercises DatNativeMap; it does not follow the original
CSS/SSS handoff or construct the gameplay stage. Stage_8022524C, on_init,
stage callbacks, transformation, collision, rendering, readiness/admission,
publication/E8, browser behavior, and the full suite remain unrun. Stadium
remains outside public content admission.
