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
`affects-gameplay` tag and 156 do not. Across the original v4 inventory checks, the
corrected prior six-hook set (v1 and v2 are versions of the same paths), and the
corrected v4 packet, 24 unique untagged hooks have bounded source checks;
**132 lack such checks**. The new packet records 12 more exact source identities,
selected-root membership, observed behavior and unresolved dependencies. This remains
source-only evidence: no client binary/profile was inspected or accepted. Tag absence
does not establish non-gameplay behavior.

The complete file inventory therefore does **not** establish a complete semantic inventory.


## Bounded rollback-core hook review

A second bounded source checkpoint covers six previously unchecked hooks under
the recursively selected `Required: Slippi Online` → `Online/Core` root. The
v4 source/config inventory records this root as selected by default and recursive;
all six paths have active generated-hook records without gameplay tags. This is
static selection evidence, not proof that the hooks execute in a particular
client binary.

| Hook path | Injection address | Source-only observation |
| --- | --- | --- |
| `Online/Core/ForceEngineOnRollback.asm` | `801A4DB4` | Uses rollback state to permit engine-loop work and copies changing rollback fields to stable storage. |
| `Online/Core/ForceInputRefetchOnAdvance.asm` | `80019614` | Calls the input-renew routine when the online frame-advance flag is set. |
| `Online/Core/LoopEngineForRollback.asm` | `801A5014` | Re-executes engine frames through the stored rollback end and reconciles sound logs. |
| `Online/Core/SkipNewInputFetchOnRollback.asm` | `80376A20` | Skips a fresh PAD read during rollback except while a state load is pending. |
| `Online/Core/TriggerSendInput.asm` | `80376A28` | Routes delayed local and received/predicted remote reports, exchanges frame data, and sets rollback state when comparisons differ. |
| `Online/Core/InitOnlinePlay.asm` | `8016E748` | Initializes online match state, input indices and RNG; copies the received Stadium-mode byte into the Freeze Stadium injection parameter. |

The exact per-hook source SHA-256/Git blob identities, source lines, summaries
and unresolved dependencies are in the machine receipt. The initialization
summary is intentionally narrow: ODB/RXB/SSCB receive explicit zero calls in
this routine; TXB/SSRB do not. Despite a “Clear A inputs” source comment, the
hook stores zero as a full 32-bit word at offset zero of each 68-byte-stride pad
entry for four ports; this source check does not establish an A-only mask or the
parent field semantics. The mode byte is passed to the Freeze Stadium injection;
this does not establish a fixed frozen-stage policy.

The bounded checkpoint reduces untagged hooks without a source check from 150
to 144: 183 active annotated hook paths comprise 27 tagged and 156 untagged;
12 untagged paths now have bounded checks (six prior and six new), while 144
remain without one. These checks do not complete semantic coverage. The independent
review receipt validated source/config selection, hashes and this accounting for
its v1 packet, and identified three initialization wording corrections now applied
in v2; it did not validate a binary or runtime behavior.

The corrected external packet is `slippi-untagged-gameplay-review-v2` under run
`match-rollback-feasibility-20261012-203633-e8f0c964`; its manifest SHA-256 is
`1a3c5e698c5cd46147703a8dfaf4ab65d55bf315ba7830167eff198bbeb67847`. The independent
review receipt is `slippi-untagged-gameplay-review-v1-independent-review.json` with
SHA-256 `fbd0480e519708211336d0657a3467ff3412e5c588009a6311c58c46f3d66e25`. This update
does not alter the earlier release/configuration pairing, v4 branch-scoped mode
findings, or the separate m-ex generation and contributor-rights unknowns. No
Slippi code or profile has been ported, and no binary/reference validation is
claimed. Offline Vanilla remains a separate profile decision from Slippi rules;
this online-only source review does not alter it.


## Bounded follow-up: online recovery and match flow

A corrected candidate adds 12 active untagged hooks from the pinned ASM commit
`fcf47f10dc244152c2ebaa3a9dec142ea42243b7`. Their generated records are under
default-selected recursive roots: `Common/FastForward`, `Online/Core`,
`Online/Slippi Online Scene`, and `Online/Static`. This establishes static source/config
selection only.

| Hook path | Address | Observed source behavior; remaining dependency |
| --- | --- | --- |
| `Common/FastForward/DynamicsFix.asm` | `8009E090` | After dynamics, recursively sets up matrices for qualifying fighter JOBJ nodes; source comments connect this to fast-forward skipping shadow rendering. JOBJ flags, caller/ABI, and any collision or hitbox use remain unknown. |
| `Online/Core/InitPause.asm` | `8016E904` | Installs client pause handling for online non-ranked/non-party play, including local pause and disconnect/LRA+Start fields; its ClientPause callback returns -1. Callback ownership, pause state effects, and disconnect convergence are unverified. |
| `Online/Core/LGLExceededGameEnd.asm` | `802F70C4` | Outside Teams/Party, if exactly one side exceeds the threshold of 45 ledge grabs, selects success/failure HUD text and extends the transition. This hook does not itself choose the winner; caller/result behavior is unverified. |
| `Online/Core/PauseCounter.asm` | `8016D26C` | Increments `ODB_PAUSE_COUNTER` and executes the replaced instruction. Its consumer and gameplay/network effect are unknown. |
| `Online/Core/PreventPauseStranding.asm` | `8016CD08` | After no unpause button, if disconnected and recorded pauser is remote, branches to original unpause code; otherwise uses the replaced `li r4,-1`. Branch target, pauser field, and cross-client behavior are unverified. |
| `Online/Core/ReportNoContestLRAS.asm` | `8016E9E8` | If not already game-over, sets the flag and calls the ODB handler on the LRAS path. Handler side effects and reachable cases remain unknown. |
| `Online/Core/StartEngineLoop.asm` | `801A4DE4` | During active rollback, loads external state only when current frame is later than requested; equal skips the external load but still reaches pad-ring rewind/flag-clear bookkeeping, while an earlier frame skips the rollback block. A load rewinds raw pad-ring bookkeeping, clears flags, reloads the global frame and renews inputs; it also updates finalized/checksum/player status, captures pending prediction state, processes remote despawn, and delays game-over across the seven-frame window. Disconnect and hard-desync branches call `SFX_Menu_CommonSound`, create HUD subtext and call `FN_END_GAME`; the soft-desync-risk branch creates HUD subtext. The helper writes exit/pauser/transition fields. Downstream audio/HUD/host effects remain unknown. |
| `Online/Core/Teams/PreventDeadStranding.asm` | `8016BE28` | In the asleep/dead predicate for disconnected online Teams, sets the input to the original compare to zero. Predicate and downstream life/respawn effect are unknown. |
| `Online/Slippi Online Scene/main.asm` | `801A45B8` | The call at line 652 to `SinglesDetermineWinner` is inside ranked `VSSceneDecide_ConnectionActive` and feeds ranked tie/winner/set bookkeeping. Reviewed helper code checks timeout/LGL, stocks and percent; source says LRAS is not handled. This does not establish non-ranked winner semantics. Reviewed sections also cover readiness, mode routing, ranked set/tiebreak and match-state loads. |
| `Online/Static/LoadMatchState.asm` | `80005610` | Writes the match-state EXI command and reads an MSRB buffer, without visible transfer-result validation. Buffer/field semantics and synchronization are unknown. |
| `Online/Static/LoadState.asm` | `8000560C` | Searches a seven-entry frame metadata ring, then requests external savestate load over EXI; missing frame logs and branches to address zero. Completion, restored-owner coverage and assertion behavior are unverified. |
| `Online/Static/SaveState.asm` | `80005608` | Writes frame metadata and requests external savestate capture over EXI with interrupts disabled. Captured state, completion and metadata consistency are unverified. |

The coverage count uses disjoint path sets: six hook checks from the original v4 inventory,
six unique checks in the corrected prior packet (v1 and v2 are versions of the same six),
and these twelve new checks. That is 24 of 156 untagged active hooks, leaving 132
without a bounded check. The Sol correction receipt
`slippi-untagged-gameplay-v3-independent-review-v2.json`
(SHA-256 `4c8acbc4e8d33d2e9b67f5f91f82c9bf1832fb7586b0f1f04aeb47fe36c70547`) records the
ranked call-scope and StartEngineLoop call-site corrections and verifies the disjoint
path union and exact remaining set. Root applied the corrected v4 packet after independent review and corrected the
portable summary to name ledge-grab counts and the ClientPause callback explicitly.
It is source-only and does not establish runtime activation, executable identity,
or profile acceptance. The corrected packet is `slippi-untagged-gameplay-review-v4` under run `match-rollback-feasibility-20261012-203633-e8f0c964`; its manifest SHA-256 is `6d72f7b52bf11766a4e2f6e9d0a17fab70c50f6d3ea20677cd65176dfc790025`, review JSON SHA-256 is `dd42ced93e1985d44ed581130f852a7bd98e16aa745d22097b1f3a1b33b14a05`, remaining-list SHA-256 is `baab798f42269356aa6402fc9c46750857fa3d5b2da807171a2de4129adb8e27`, and report SHA-256 is `96c0a7cf9cfc7bd6df88f611dd43828b8f8224486bdcf70477bd6dcb8980a8ea`. Exact hook/source identities are in that machine receipt. It does
not alter the v3.6.4 source/config pairing, branch-scoped mode findings, repository
license notices, or separate m-ex generation and contributor/reuse-rights unknowns.

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
