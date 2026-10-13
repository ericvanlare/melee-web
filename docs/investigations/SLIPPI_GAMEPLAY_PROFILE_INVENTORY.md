# Slippi gameplay profile: source inventory

**Source identified / Partial semantics / No port or profile validation**

This is the source-only profile investigation for [#318](https://github.com/ericvanlare/melee-web/issues/318)
and the [mode identity plan](https://github.com/ericvanlare/melee-web/issues/9).
The [machine inventory](../evidence/slippi-gameplay-profile-inventory-v1.json)
contains pinned sources, every configured entry, selected file/hash closure,
bounded hook summaries and explicit unknowns. It contains no upstream code or binary payload.
The [milestone status](../status/2026-10-12-match-only-rollback-feasibility.md)
is the changing evidence boundary. No Slippi code has been ported.

## Modes and Stadium

| Mode | Verified public source/release behavior | Scope limit |
| --- | --- | --- |
| Ranked / Unranked | Fixed rules, no pause; **Stadium is always unfrozen**. | Client character IDs must be below 26; selected stages are checked against the matchmaking response. Exact backend stage/rule policies are not established here. |
| Direct | Pause permitted, stage selection enabled; frozen/unfrozen Stadium selected with Z. | Effective match settings can be received dynamically. |
| Teams | No pause; frozen/unfrozen Stadium selection. | FoD patch edits only the recognized legal-stage masks: on for Singles, off for Teams; other masks unchanged. |
| Party | Five-minute Time mode, items, score UI and rotating weekly stages. | Current backend schedule, full rules and Stadium state unresolved. |
| Offline | Slippi's Netplay codes may also be enabled for ordinary Dolphin play. | Offline alone does not identify vanilla: record the enabled profile/codes. |

The Stadium finding comes from official [v3.5.0 notes](https://github.com/project-slippi/Ishiiruka/releases/tag/v3.5.0)
and the [v3.5.2 Teams fix](https://github.com/project-slippi/Ishiiruka/releases/tag/v3.5.2).
Party was introduced in [v3.6.0](https://github.com/project-slippi/Ishiiruka/releases/tag/v3.6.0).
The pinned client paths and lines supporting the other claims are in the receipt.

## Shared changes

The inspected Netplay configuration selects one shared patch set, with mode-aware
client and game-side behavior. It is not a separate INI for every online mode.
Selected static defaults include four stocks, eight minutes, Stock mode, no items
and Singles stages; live setup can override these, including Party's rules.

- **UCF 0.84** is selected. Named hooks cover dashback, squat reverse, pad-buffer/cardinals,
  SDI, shield drop/shield SDI, tumble and its indicator. Individual payload mechanics
  and live per-player/per-mode toggle values remain unresolved. Replay `prepareGameInfo`
  serialization is not evidence of live online toggle transport.
- **Neutral spawns** compute Singles/Teams assignments. The fallback uses original
  authored spawn coordinates with computed IDs and recomputed facing; vanilla-order
  equivalence is not claimed.
- **PreventWobbling** selects the nonrecursive root wrapper/macro, with a Teams bypass.
  The nested Ranked wrapper is excluded; this is not a Ranked-only activation.
- **Stage/determinism changes** include Nana stick initialization, camera-independent
  Stadium monitor bounds, FD RNG save/restore, Stadium transformation/archive handling
  and Whispy direction filtering.
- **Other gameplay hooks** include existing player/stage-data zeroing, an offscreen-zone
  predicate and camera-KO velocity/model handling. The receipt enumerates exact paths,
  addresses and narrowed source summaries; hook names alone are not behavioral proof.

All 59 configured entries are mapped (52 default-selected). Selected folders resolve
to 198 files and 183 annotated ASM hooks; the parsed textual include closure contains
196 files, and 21 selected binary payloads are hashed. Of 183 hooks, 27 carry an
`affects-gameplay` tag and 156 do not. Six untagged ASM hooks received additional bounded
checks; **150 lack such checks**. Tag absence does not establish non-gameplay behavior.
The complete file inventory therefore does **not** establish a complete semantic inventory.

## Pins and license findings

- [ASM source fcf47f10](https://github.com/project-slippi/slippi-ssbm-asm/tree/fcf47f10dc244152c2ebaa3a9dec142ea42243b7)
  has a root [GPLv3 license](https://github.com/project-slippi/slippi-ssbm-asm/blob/fcf47f10dc244152c2ebaa3a9dec142ea42243b7/LICENSE).
- [Ishiiruka source 60f7b634](https://github.com/project-slippi/Ishiiruka/tree/60f7b63496fb6ec7b9180a04f16f3edc0ad89fe2)
  has GPLv2-or-later notices in its README and inspected EXI source header;
  its root license text is GPLv2 and repository metadata says GPL-2.0.
- The latest published release found was [v3.6.4](https://github.com/project-slippi/Ishiiruka/releases/tag/v3.6.4)
  at e7711b104b339a99385f2bb12b472d46140a7bc7. The inspected branch is later;
  these independently fetched heads are **not a verified compatible release bundle**.
- Selected `Binary/m-ex.bin` points to `JLaferri/m-ex` branch `support/slippi`.
  Its exact source commit, build provenance and license/rights are unresolved.

These are separate source notices, not a combined redistribution conclusion. Resolve
exact artifacts and reuse rights before porting. The project will keep Vanilla as
the offline accuracy reference and introduce Slippi rules as a separately pinned
profile using the unmodified 1.02 disc. No settings toggle or profile is implemented
by this inventory.

## Release/configuration and m-ex follow-up

A bounded follow-up identifies a source pairing: Ishiiruka v3.6.4 release source
`e7711b104b339a99385f2bb12b472d46140a7bc7` contains the exact same Netplay INI
as ASM `fcf47f10`: 145,690 bytes, Git blob `63e077eb6e60fdc5691ba4ea9550d80bcd676776`.
The pinned Windows workflow copies `Data/Sys` into its package. Matching successful
build/release run metadata is retained, but the release workflow selected the
latest branch artifact and that artifact has expired. **No archive or executable
was downloaded or verified.** This source/configuration match is not runtime/profile
acceptance. The machine receipt records the asset digest as API metadata only.

The previous mode analysis remains pinned to branch `60f7b634`. The release's
`SlippiMatchmaking.cpp` is byte-identical; the inspected EXI difference moves desync
recovery timer restoration after default timer setup. This narrow comparison does
not revalidate every mode or behavior against the released executable.

Selected `m-ex.bin` matches the tracked binary at
[m-ex commit ea6bddf8](https://github.com/JLaferri/m-ex/commit/ea6bddf8e288709953dd86f75ba689770d1ffebe):
6,992 bytes, Git blob `1ae51b97650ae95d5abd93ae8e7e69d2459d8227`.
This identifies where matching artifact bytes were checked in; it does not prove
which exact source/toolchain produced them. Its build recipe invokes Gecko without
a pinned version/environment. No root LICENSE/COPYING or license-like path was
found in the bounded source-tree search; two inspected source headers also lacked
license notices. Reproducible generation and contributor/reuse rights remain open.
The original source snapshot above is retained; this follow-up narrows its
provenance unknowns without changing the separate license conclusions.

## Next bounded source task

Verify a chosen released executable/configuration as a reference and resolve the
m-ex reuse question, then complete selected gameplay/UCF behavioral mapping before
porting. Private matchmaking policy remains explicit. The inspected current Gecko
generator establishes omitted JSON recursion as false; its historical executable
identity and exact assembled-code equivalence remain unknown. Source/hash closure
is not runtime activation or comparison evidence.
