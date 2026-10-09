# Weaker-fighter comparison lineup (#295)

**Compiled / Source identified / Browser exercised / Retail compared**

Status: partial, bounded evidence only. #295 remains open; this lineup does not establish full-fighter accuracy. The source scan read the retained v10 MWRO through match 2 tick 59 (155,821 records). Its before/after source stat was stable; the full MWRO SHA-256 was not recomputed. The portable receipt binds the recorded source hash, scan output, parser, producer paths, and scoped comparisons.

- **Sheik — source-only checkpoint.** Source creates distinct Zelda 19 and Sheik 7 entities and swaps the active owner through `transformed[0]`; a dormant Sheik entity alone is not a pass. Checkpoint `0b3847ee3f69ef031decc3f2ccc9c37227bd4302` is unbuilt and uncaptured. The next run still needs transform-mode CSS/SSS live-owner barriers and must arm teardown checks only after match entry; no browser replay or transformation comparison exists. Plan receipt: `runs/weaker-lineup-inventory-20261009-030652-bf6fe926/sheik-source-observation-plan-v2.md`.
- **Jigglypuff — fresh setup/Ready comparison passed; active gameplay missing.** The independent named prefix below matches setup and 60 Entry/Ready source ticks, all at gameplay clock zero. Historical context: The v10 source has Jigglypuff (CKIND/FTKIND 15) at match 1, slot 0, ticks 0–59. The retained v10 browser route is blocked earlier by the parked match-0 Bowser RNG divergence at clock 5239; do not bypass or reopen it. The independent prefix below supplies a separately identified setup/Ready route. Historical PR86 producer `2444564b` and comparator `e91cc7e9` provide reusable parts; the full-state writer remains present. The fresh comparison uses checked secondary-entity admission and that explicit four-player profile.
- **Peach — current setup and 184-tick comparison passed.** The current v10 clock-60 comparison covers setup plus match-0 ticks 0–183 (184 ticks), including Peach at slot 3 (character-select kind 12, physical Fighter kind 9); all declared fields matched at that bounded boundary. It remains incomplete and does not establish whole-session accuracy. Harness `52926e13` and unchanged Release runtime `282fbddb` are identified separately. Requested cursor 1368, observed 1382 and exported 1384 remain distinct; the 16 extra match rows receive shape/order checks only. Root independently replayed the unchanged strict comparator and obtained the identical result. The historical `90289a2` comparison remains in the receipt.
- **Ice Climbers — fresh Popo/Nana setup/Ready comparison passed; active gameplay missing.** The independent named prefix below compares both distinct source entities. Historical context: The v10 source has Popo 10 at slot 1/entity 0 and Nana 11 at entity 1 through match 2 tick 59. The strict comparator can express both entities, but the retained v10 producer/trace is primary-only. Historical PR86 paired evidence first rejected Nana motion/input at tick 105; after that repair, a Nana position bit differed at tick 609, and the extended A comparison later first differed on Popo screen-KO X at tick 5606. Those older A observations do not validate the current v10 candidate.

The earlier external inventory’s B transformation statement and its later “no original Jigglypuff MWRO/MWRC” statement are superseded by the scoped source scan and producer review; the original inventory files remain unchanged. The retained v10 later-match browser route stays blocked at Bowser; the Jigglypuff and Ice source intervals are not browser results. See [the evidence receipt](../evidence/weaker-fighter-comparison-lineup-v1.json) for hashes, source intervals, comparator contracts, and historical limits.

The Peach browser capture deliberately ends incomplete after its prefix. All 46 served artifacts matched before and after; exact owned process cleanup and socket reuse passed. The harness cleanup fix passed focused failure controls and the full 2,256-test suite (153 skips, no failures). A copied, unused disc-provenance locator has a disclosed filename typo; actual capture disc path, size and hash agree, and frozen artifacts remain unchanged. No full-session, physical-input, pixel, PCM or performance acceptance is claimed.

The initial source-scan stdout was not separately retained. Its source was reconstructed and verified against the recorded hash; the enhanced stat-bracketed scan was independently rerun by root and reproduced every output field. The original file hash remains recorded evidence, not a fresh full-file hash.

The independent Jigglypuff/Ice Climbers prefix implementation is compiled at
`1f9681e0` (tree `e29d6746`). The ordinary Release browser build and full suite
passed: 2,269 tests, 153 skips, no failures. Its named four-CPU profile checks
source identities before emitting both Popo and Nana through the existing strict
v2 entity schema. Default v8/v9/v10 observation and comparison remain unchanged.
These were implementation-only checks. The later independent original and
paired browser Ready-prefix result is recorded below.

The original source processes a whole queued PAD batch before drawing. The
capture therefore retains the first qualifying draw and all 60–64 observed
source ticks, while the scientific comparison covers the first 60. Existing v8
browser rendering rejects zero- or multiple-tick draw batches during preparation;
it does not trim the source interval or change original scheduling. Source
traversal, gameplay clock, PAD cursor and requested diagnostic mode remain
separate. Successful prefix completion requires checked teardown and still
reports an incomplete match/session. Retained preparation failures and exact
producer/review identities are in the same evidence receipt.

The fresh original capture and its browser replay now passed the named
`jiggly-ice-mario-fox-v1` diagnostic. Collector `ef944178`, native observer
`a66db44f`, browser harness `44593ed2`, and actual Release runtime `1f9681e0`
remain separate identities. The first original attempt failed exact rules
admission because the reused menu driver toggled item-mask bit 18 on a non-Team
route. Restricting that menu action to its declared Team route resolved the next
attempt; the failed evidence remains retained and the original rules/RNG were
unchanged.

The unchanged strict comparator matched the constructed-fighter setup and all
60 SourceTicks (0–59), including RNG, full PAD and five fighter entities with
separate Popo and Nana. **This is Entry/Ready scope: gameplay `match_frame` is
zero in every compared row. No active gameplay has been compared.** Source
setup entry 4064 and constructed-fighter comparison snapshot 4090 are distinct.
The 1,164-frame recipe produced exactly 1,164 source steps/draws; all 60 exported
Match rows (port indices 1104–1163) were compared. Root independently reran the
comparator and obtained the identical report. Its result remains
`prefix_unverified`, `complete=false` and `whole_session_equivalent=false`.

All 89 frozen inputs and 46 served artifacts were unchanged; the inventory is
40 standard artifacts plus six named auxiliary files. Owned processes were
retired and the server port was reusable. The harness now admits only an
explicitly requested, hash-bound prefix with the expected source interval and
checked teardown; it rejects unsolicited diagnostic completion and preserves
ordinary whole-session acceptance. The actual recipe exposed and corrected a
metadata-version preparation error before capture. The same receipt records
all raw hashes, first failures, source controls and independent reviews.

Next: finish review/integration of Sheik's source readiness barriers and prepare
its first active-owner transition experiment. A later Jigglypuff/Ice Climbers
prefix must explicitly cover active gameplay-clock ticks while preserving this
Ready-only profile and evidence. No Bowser replay, comparison tolerance change,
full-fighter/session, pixel, PCM, physical-input or timing acceptance follows.
The final milestone full suite and current-head CI remain pending.
