# B4: Bowser hit observer and geometry boundary remain unresolved

**Current-main integration validation — Compiled.** The host checks ran on
clean source head `43a3a929e9a678b6417e56e5ba6ea7bdbe6be101` (tree
`ea3a7019e6237c589721b2e20bb638a526091e59`), after merging main commit
`7c5d47ab58f25b2a4241147c00d70c68a28b5ac6`. The unfiltered suite passed
1,948 tests with 153 skipped (2,101 discovered, zero failures or errors), and
the ordinary Release runtime build passed. The current-head build closure and
raw logs are recorded separately from the d1 capture producer.

Before that build, all eight d1 Release build products were preserved and
verified against their recorded closure; the three staged JS/WASM/data capture
outputs also matched their manifest. The current-head build kept the probe
object, runtime archive, JS and data hashes unchanged, while the emitted Wasm
hash changed from the retained d1 output. The capture remains tied to d1's
preserved artifacts; no browser run followed this build. This validation did
not read or hash original payloads and makes no new Bowser-cause claim. The
build log shows SDL 3.4.10's `SDL.c.o` and `libSDL3.a` were rebuilt, but the
retained d1 evidence has no prior SDL object/archive identities; the rebuild
trigger is unobserved.

**Capacity follow-up — Browser exercised; issue #274 remains open.** The
approved observer-capacity change was built from producer commit
`d1b906979c70731d2f934766cee08af52cd70da4` (tree
`b020077cfe72cf67437925801a25dc6e7cd876c2`, based on `fe28e8e`). The ordinary
Release runtime build passed, and the unfiltered Python suite passed 1,948
tests with 153 skipped (2,101 discovered, zero failures or errors). These
checks belong to that producer revision, before the later main-branch merge.

The capacity follow-up retained a complete, validated 76-event row at browser
cursor 5239: four candidate hits each visited all fifteen authored hurtboxes
in order, and all 60 geometry results were false. The row also contains one
candidate pass/pair and the expected entry/return records. This is an observed
case requiring 76 rows, not a global event bound; it does not identify why the
retained original accepted the hit. The selector-off control and the historical
64-row overflow behavior remain separate controls. The selected rows at 5238,
5239 and 5240 and the full captured export passed their strict validators.

The retained-export comparison reproduced 5,240 state/PAD/entity/order rows
(indices 0–5239) and the setup row, with no difference through that prefix;
strict validation also checked the complete old and new export tails. The
original divergence remains at source frame 4055 / browser index 5239:
original Bowser has 11 damage and `DamageFlyTop`, while the browser has zero
damage and `WalkFast`. The capture remained incomplete after manual unload
(owner exit 1); Results and whole-session acceptance failed. The historical
owner CDP gate also remains failed because its recorded report path pointed at
the retail-browser report, which did not contain the required process list.
Separate retained CDP-to-OS attribution and cleanup review verified the actual
capture report's process identities and no owned survivors; that does not
retroactively pass the owner's gate. Preparation, quoting and attribution
attempt failures remain preserved as failed attempts, separately from the
corrected receipts. The comparison used retained extraction descriptors and
did not reopen original payloads during post-processing.

This follow-up does not establish an original collision cause or fix the first
divergence. Issue #274, Results and whole-session acceptance remain open.
Pixels, PCM, physical input, live timing and performance remain separate gates.

**Earlier observer capture and controls.**

**Compiled / Browser exercised**

The private Release observer for [issue #274](https://github.com/ericvanlare/melee-web/issues/274)
reproduces all 5,240 retained browser state, PAD, entity and order rows through
session-frame index 5239, plus the original match setup row. The
[scoped receipt](../evidence/v10-bowser-hit-transition-observer-v1.json) binds
its new native producer, current 40-artifact staging inventory, actual strict
comparison APIs, raw probe identities, bounded capture and fresh cleanup.
Historical 32- and 34-artifact receipts remain separate.

At selected cursors 5238, 5239 and 5240, Bowser's collision owner runs with both
suppressing gates false. Each cursor contains two damage-processing calls with
zero logs and no Bowser motion-change call. At cursor 5239, the browser records
22 actual `HSD_Randf` calls: seven before collision entry and fifteen after its
return. Collision entry and return have the same RNG seed. The earlier source
70-step LCG relationship remains an orbit inference, not an observed original
draw-call count.

The [retained original divergence](2026-10-08-b4-first-match-results-comparison-failure.md)
remains unchanged: source tick 4055 has DamageFlyTop and 11 damage, while the
browser has WalkFast and zero damage. The original damage source links to
primary slot 0. This capture observes the browser's absent damage log and
transition; it does not identify the collision rejection or RNG cause.
Cursor 5240 is browser-only because the audited retained original suffix ends
at tick 4055 / browser index 5239.

The capture requested stop cursor 5241, observed 5247 and exported 5,250 frames.
It retains the incomplete/manual-unload labels and exit 1. All three selected
hit and RNG observations validated; the complete overshot export tail also
validated. Attributed processes and groups were freshly absent, CDP attribution
completed, the port was reusable, and only the owned temporary directory and
matching lane marker were removed.

The actual current-main Release refresh passed. The full Python suite ran
2,092 tests including 153 skipped: 1,938 executed tests passed and one patch
canonicalization check failed. The repair changes only two Git blob-index rows;
the applied source tree is unchanged and all 15 focused canonicalization
controls pass. The full suite is not relabeled as all passing.

The next proposed observation follows normal hurt candidates through
`ftColl_80078C70`, `lbColl_8000805C` and `ftColl_80076ED8`, separating ordinary and
phantom log production. `ftColl_80076CBC` is the shield branch. The existing
Arrow shield probe remains bound to its historical scenario. No gameplay/RNG
fix, wider replay or original scan is justified yet. Issue #274, Results and
whole-session acceptance remain open; pixels, PCM, physical input, live timing
and performance remain separate unrun gates.
