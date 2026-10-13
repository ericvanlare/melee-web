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
`affects-gameplay` tag and 156 do not. Across the original v4 inventory checks, the corrected prior six-hook set (v1 and v2
are versions of the same paths), the corrected v4 packet, the v5 packet, and the
v6 packet, 36 unique untagged hooks have bounded source checks; **120 lack such
checks**. The latest v6 packet records six exact source identities, selected-root
membership, observed behavior and unresolved dependencies. The v5 six-hook packet
remains a historical 30/156 checkpoint; its paths are counted once in the current
disjoint total. This remains source-only evidence: no client binary/profile was
inspected or accepted. Tag absence does not establish non-gameplay behavior.

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


## Bounded follow-up: online SSS Stadium choice

A v5 source checkpoint covers six active untagged hooks selected through the
recursively selected `Required: Slippi Online` → `Online/Menus` root in the
pinned generated profile. This is static configuration selection, not proof of
runtime activation. The hooks span stage-choice display and the online SSS
selection handoff.

| Hook path | Address | Source-only observation |
| --- | --- | --- |
| `Online/Menus/SSS/FrozenStadiumToggle/CheckForToggledOnStageName.asm` | `80259E34` | Reads the shared frozen-mode byte and switches the Stadium stage-name display between canonical ID 18 and display ID 31. |
| `Online/Menus/SSS/FrozenStadiumToggle/IncMaxNames.asm` | `80259FB4` | Changes one compare threshold from 30 to 32; caller continuation and table bounds remain unknown. |
| `Online/Menus/SSS/FrozenStadiumToggle/StageSelectTextProc.asm` | `8025AC6C` | Creates per-GObj state and animates the Stadium/alternate-stage label. |
| `Online/Menus/SSS/FrozenStadiumToggle/StoreNameId.asm` | `80259FAC` | Maps display ID 31 back to canonical Stadium ID 18 when storing stage-name GObj data. |
| `Online/Menus/SSS/ExitSSSUponStageSelect.asm` | `80259CC8` | On online SSS, passes the hovered external stage ID to unnamed `0x8025bc08` and requests a minor-scene change. |
| `Online/Menus/SSS/DisableLRSTART.asm` | `8025B8A4` | On online SSS, branches past the original LRA sequence. |

The active hooks do not show this stage-selection path serializing the selected
freeze mode or checking peer agreement. The separate `InitOnlinePlay` check
observes `MSRB_ALT_STAGE_MODE` copied into the shared mode byte; it does not prove
that this callback sets or validates that field. Exact hook/source hashes, selected
records and the disjoint path accounting are in the [machine receipt](../evidence/slippi-gameplay-profile-inventory-v1.json).

The frozen v5 packet is `slippi-untagged-gameplay-review-v5` under run
`match-rollback-feasibility-20261012-203633-e8f0c964`; its manifest SHA-256 is
`ace88153a5b652972f61ac6728043f0ba1aef6104df0631f774e1c1e2c754624`, review JSON
SHA-256 is `2e4af5a8747d8087e205e9bfa91517140fb538276b4cd408583102b04fcfc9bb`, and
remaining-list SHA-256 is `3ea13df8dd148d7f0b50e26b85de6eee082bc4bc1b80d88dfddbb57da0b243b1`.
The independent source/accounting review is
`slippi-untagged-gameplay-review-v5-independent-review-v1.json`, SHA-256
`a916ea3c39cf69e53544003221d1bd578c3a6aa6a700328b1735ca5572c06143`; it passed
with a receipt-sync note because the portable files still reported the prior count.
The v5 packet remains the historical checkpoint at 30 checked of 156. Its source
bytes and source/release/license findings are reused from the frozen v4 packet.
Release-binary validation remains unperformed. The current v6 source packet and
its bounded independent review are summarized below; portable-doc review of this
candidate remains pending. The
selected m-ex artifact's checked-in byte association is identified separately from
its unresolved exact generation, contributor/reuse rights and license status;
repository notices do not provide legal clearance. No profile has been ported or
accepted.


## Bounded follow-up: online route and match setup

A v6 source checkpoint adds six paths selected from the exact frozen v5 remaining
set. The pinned profile places them under recursively selected `Required: Slippi
Online` roots. This is static selection evidence only; it does not establish that
these hooks activate in a released client or online match.

| Hook path | Address | Source-only observation |
| --- | --- | --- |
| `Online/Slippi Online Scene/boot.asm` | `801BFA20` | Sets the first title-menu destination to online submenu 8 and selects the first unlocked item. The later online match/network activation is not established. Though a comment says Stadium transformations are preloaded, the observed loop calls `File_GetLength` on four paths; actual loading/caching is unproven. |
| `Online/Menus/InGame/InitInGame.asm` | `802F6694` | Gated to `SCENE_ONLINE_IN_GAME`; builds HUD/name/delay presentation and calls `FN_LoadMatchState` for displayed names. No gameplay simulation write is visible in the reviewed body. |
| `Online/Menus/CSS/FetchMatchInfo.asm` | `80260310` | Gated to `SCENE_ONLINE_CSS`; passes the CSS data table's MSRB buffer to `FN_LoadMatchState`, which sends `GetMatchState` through EXI and reads the response. This hook is a data-fetch path, not itself a simulation update. |
| `Online/Menus/CSS/SceneLoadCSS.asm` | `8026699C` | Allocates and zeroes CSSDT; allocates/stores an MSRB pointer with no MSRB zeroing call shown. In Teams mode it saves the prior team-toggle index; it also loads `slpCSS.dat`. This is menu/setup state and a file-backed chat resource. |
| `Online/Static/AdjustNullCharID.asm` | `80005694` | The helper body is only `blr` and does not write the buffer. Its caller copies match info to `VS_SSS_DATA` before calling it; a caller comment says later In-Game prep will copy that block, but this packet does not establish that later copy. Downstream buffer/sentinel use remains untraced, so the no-op is not classified as safely irrelevant. |
| `Online/Static/GetTeamCostumeIndex.asm` | `800056B0` | Maps team 3→green, 2→blue, 1→red, other values→blue. The Teams-gated online scene writes the result into VS splash setup; no simulation-rule change is established. |

The v6 packet is `slippi-remaining-source-inventory-v6-corrected-v1` in run
`match-rollback-feasibility-20261012-203633-e8f0c964`. Its actual inventory file is
`inventory-v6-corrected.json` (SHA-256
`3605b52de2ec868d4a9aa4eca588a9d7a2dc3de79504c7257fbfde78ab60486e`); the freeze
manifest SHA-256 is `da5105fbfcdf294e59a8bd608ad5d5d361aab2f310a7bd85b9af750d0e23b7f6`
and the corrected report SHA-256 is
`5290eab1b26b8140078d1efa0b89e26ffcb3d777df5d40ff26a08a395be56f35`. The
independent review `slippi-remaining-source-inventory-v6-corrected-independent-review-v1.json`
(SHA-256 `d32af9bd75b11198cc3d918c0a8b93a3cbf11beec52520eef15f2a431d4cd101`)
passed for bounded source-check publication only and verified 36/156 checks with
120 remaining. It also required the corrected inventory locator used above because
the frozen report names a stale filename. The source repository and GPL-3.0 root
notice are inherited from the pinned inventory; this is not legal clearance.
Runtime activation, release executable compatibility, peer agreement, full
semantics, and profile acceptance remain unresolved. No code or profile was
ported.

## Pins and license findings

- [ASM source fcf47f10](https://github.com/project-slippi/slippi-ssbm-asm/tree/fcf47f10dc244152c2ebaa3a9dec142ea42243b7)
  has a root [GPLv3 license](https://github.com/project-slippi/slippi-ssbm-asm/blob/fcf47f10dc244152c2ebaa3a9dec142ea42243b7/LICENSE).
- [Ishiiruka source 60f7b634](https://github.com/project-slippi/Ishiiruka/tree/60f7b63496fb6ec7b9180a04f16f3edc0ad89fe2)
  has GPLv2-or-later notices in its README and inspected EXI source header;
  its root license text is GPLv2 and repository metadata says GPL-2.0.
- The latest published release found was [v3.6.4](https://github.com/project-slippi/Ishiiruka/releases/tag/v3.6.4)
  at e7711b104b339a99385f2bb12b472d46140a7bc7. The inspected branch is later;
  these independently fetched heads are **not a verified compatible release bundle**.
- Selected `Binary/m-ex.bin` is annotated as generated from the `JLaferri/m-ex`
  `support/slippi` branch. The matching tracked artifact blob and last artifact-change
  commit are identified below; exact generation inputs/toolchain and contributor/reuse
  rights remain unresolved.

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

## Bounded UCF payload identity check

The independent v6 receipt verifies the eight pinned UCF payload identities
against the cached selected ASM files and generated INI blocks. For each payload,
the literal word prefix, generated length, complete word count, and exact trailing
padding match. These checks identify the bytes only: no disassembly or exact
instruction semantics were verified, and this does not establish activation,
per-player values, runtime behavior, or source-to-built-output pairing.

The eight files are in the recursively selected External/UCF 0.84/UCF subtree
under Required: General Codes. They are already among the 27 tagged gameplay
paths. This identity check adds no untagged semantic checks. Its recorded 30/156
baseline predates the separate v6 source packet; the current source-check count is
36 checked of 156, with 120 remaining. The other 19 tagged summaries were not
re-audited. The independent review corrected
the Pad Buffer + 1.0 Cardinals cached reference: its actual path contains a
literal plus sign; the earlier %2B spelling did not resolve.

The review receipt is
slippi-gameplay-profile-ucf-v6-independent-review-v1/review.json in run
match-rollback-feasibility-20261012-203633-e8f0c964
(SHA-256 7f0492a6175fafd1e4a60656f18df1ab242a8c163536269d45eddf2816712754).
It passes pinned payload identity only and holds novel author-source claims for
lack of retained primary source and metadata evidence. No additional author-source
semantics, tags, or license conclusion is established here.

## Next bounded source task

Verify a chosen released executable/configuration as a reference and resolve the
m-ex reuse question, then complete selected gameplay/UCF behavioral mapping before
porting. Private matchmaking policy remains explicit. The inspected current Gecko
generator establishes omitted JSON recursion as false; its historical executable
identity and exact assembled-code equivalence remain unknown. Source/hash closure
is not runtime activation or comparison evidence.
