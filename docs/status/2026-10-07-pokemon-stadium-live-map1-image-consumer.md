# Pokémon Stadium: live map-1 IMAGE consumer lifetime

**Compiled / Source identified / Native traced**

The original `HSD_JObjLoadJoint` now loads the checked Stadium map-1 descriptor
inside a bounded diagnostic test. Actual `grStadium_801D3138` finds the canonical
IMAGE borrowed from `DatNativeMap`: its returned TObj has the exact catalog IMAGE
pointer, and independent live JObj/DObj/MObj/TObj traversal confirms the returned
material owns the single live reference. Foreign-owner IMAGE and empty-view
misses preserve the output-material sentinel.

The shared lifetime check uses a zero-initialized local `HSD_GObj.hsd_obj` query
view, with no source GObj registration or destruction. The view clears before
original `HSD_JObjRemoveAll`; the loaded root ends before caller-owned map, SIS,
foreign map, catalog and handle teardown. It reuses the
[canonical descriptor/SIS prerequisite](2026-10-07-pokemon-stadium-screen-roots.md),
and rejects broader stage publication/visual entry because those introduce
Ground, camera, animation and GX-link lifetimes.

Before loading, actual borrowed toon/shadow state must be nonparticipating.
No global is disabled and no authored flag is changed to pass this gate. Root,
object, next, image, id, matrix-id and coordinate fields remain equal. Removal
restores source IDs, live class counts and used source pool entries, texture-bound
live count, entity/process/GX lists and full StageInfo. Raw and decoded archive
baselines, save, selection, RNG owner/value, item globals/link and generation/ticks
also remain unchanged. Source caches may remain; they are not live ownership.

One retained 98-file preflight passed at
`2ded3d4f465d82cea1a3323343ef300a1ab8b6d6`, with two sequential live
load/query/remove owner lifetimes and a header-only trace. All input hashes
remained equal to the original union and earlier descriptor-only fixture.
The synthetic source fixture also passed hit/miss/remove twice. Compiler and
packet-preparation failures are retained; the receipt explicitly identifies the
earlier transcript-only log gap and the appended producer-label correction.

Integration validation used clean source producer
`4544b8d1e0754af6fd37a05007a24e87e72839ac`. Executable C/C++ source matches the
retained producer; the only source delta adds the persistent Python assertion for
the new observed completion line. Full discovery passed with the fixture
environment unset; retained/E8 tests skipped before native launch. The affected
`native_menu_host_trace` also compiled in ordinary Release with diagnostic OFF.
The historical native execution used diagnostic ON; the generic
`browser-release` trace header does not identify that profile. Native evidence
was not recaptured for integration.

The [portable receipt](../evidence/pokemon-stadium-live-map1-image-consumer-v1.json)
records producer, source, build, input, log, artifact and review identities.
This completes the bounded live consumer prerequisite in
[#227](https://github.com/ericvanlare/melee-web/issues/227) and contributes to
[#164](https://github.com/ericvanlare/melee-web/issues/164).
No E8, Stage2524C, OnInit, Ground, registered stage GObj, stage publication,
animation/gameplay ticks/draw, global SIS, camera/text/copy-buffer consumer,
browser, retail comparison, performance, pixels, audio acceptance, deployment or
Stadium admission is established.
