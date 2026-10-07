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

Current-main integration validation used clean source producer
`adee01b9b35fdebf4a3d4377880fb68a5dec7f4e`, based on
`2fd79a51afb84e3a260b8553d568cb6360f91171`. All 16 boundary source hashes
remain unchanged after rebase. Full discovery passed with 2,030 tests and 132
skips, with the fixture environment unset; retained/E8 tests skipped before
native launch. Four existing ordinary menu owner tests now run because their
ordinary native target is available, explaining the earlier 136 skips.

The affected ordinary Release target with diagnostic OFF is reused from exact
historical producer `4544b8d1e0754af6fd37a05007a24e87e72839ac`: all four retained
build artifacts match and no current-main change intersects its explicit Ninja
inputs. This is historical build reuse, not a new build producer. The retained
native execution remains at `2ded3d4f465d82cea1a3323343ef300a1ab8b6d6`, using
diagnostic ON; the generic `browser-release` trace header does not identify that
profile. Native evidence was not recaptured for integration.

The [portable receipt](../evidence/pokemon-stadium-live-map1-image-consumer-v1.json)
records producer, source, build, input, log, artifact and review identities.
This completes the bounded live consumer prerequisite in
[#227](https://github.com/ericvanlare/melee-web/issues/227) and contributes to
[#164](https://github.com/ericvanlare/melee-web/issues/164).
No E8, Stage2524C, OnInit, Ground, registered stage GObj, stage publication,
animation/gameplay ticks/draw, global SIS, camera/text/copy-buffer consumer,
browser, retail comparison, performance, pixels, audio acceptance, deployment or
Stadium admission is established.
