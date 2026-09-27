# Browser save profiles

This document describes the save controls in the browser player and the
source-owned fields that define its Everything unlocked baseline. Runtime
support and validation remain separate from saved completion: a source unlock
does not mean its feature works in this browser build.

## Modes

Open **Settings** beside **Controls** to choose one of two modes:

- **Everything unlocked** starts from the completed baseline below. Changes
  made during that session are discarded. Export writes the same captured
  baseline even if a different session was previously active; it does not
  commit or replace Personal progress.
- **Personal progress** applies the locally stored profile before the original
  menus start. It autosaves changed card data in the current browser profile.

Changing modes while a disc is loaded restarts its source session so the chosen
profile is installed before CSS. With no loaded disc, the choice applies on the
next launch. Loading a GCI while a game is running follows the same restart
boundary. **Eject** takes a final Personal snapshot and waits for its storage
transaction before retiring the page. If storage rejects the write, Eject stops
and leaves the current page running with the reported error.

There is no manual save button. The browser samples persistent source fields at
the existing native command boundary while a scene is active, every 1.5 seconds.
It commits only when the bytes change and reports success after IndexedDB says
the transaction completed. A mode/profile import is committed in one
transaction. A revision check prevents an older tab from overwriting a newer
save. Strict durability is requested where the browser supports it; browsers
that do not accept that option use their regular read/write transaction.

## Local storage and recovery

The player uses IndexedDB database `webmelee-save-profiles-v1`, with separate
profile and mode records. Personal progress is stored as a versioned envelope
with a revision, generation ID, commit time and SHA-256 checksum. Each committed
profile also keeps the preceding verified generation. On read, the player
checks the current bytes and then the preceding copy. If only the preceding copy
passes, it applies that copy and says so in Settings. The damaged bytes remain
available for diagnosis until a later valid commit replaces them. If neither
copy verifies, Personal mode cannot start; choose Everything unlocked or load a
validated GCI to continue. Existing stored bytes are not silently reset.

The player requests persistent browser storage, but that request can be denied.
Browser profile cleanup, site-data deletion, device loss and private browsing
can still remove or prevent storage. Saves are not synced to an account or sent
to the operator. Export a GCI backup to move progress between browser profiles.

Each IndexedDB commit is atomic: after an interrupted transaction, the record
is either the preceding committed generation or the new committed generation,
and reads verify checksums before using either one. This protects committed
generations only. A forced browser or device close can lose progress that had
not yet reached a completed transaction. Browser profile cleanup, eviction,
device failure and storage exhaustion can also prevent recovery.

## Everything unlocked inventory

The baseline is created by first running Melee's original fresh-profile
initialization (`gmMainLib_8015F600(1..8, 1)`), then applying source routines to
that fresh profile. Its fields and bounds come from the pinned Melee source
(`dependencies.lock.json`), not from a retail save's unknown bytes:

| State | Source representation and boundary | Baseline value |
| --- | --- | --- |
| Unlockable characters and stages | The source's eleven-entry character and stage unlock tables, through `gm_80164F18()` and `gm_8016468C()` | All table entries unlocked; `fn_80173510()` derives the corresponding standard-stage completion flag |
| Event mode | `gmm_x1868.x1A68`; the source all-events check loops event IDs `0..50` | All 51 clear bits set; score/time fields remain fresh. `x1B3C` is set through the original setter for the final event's separately checked three-stock clear |
| Classic, Adventure and All-Star | Clear IDs from `gm_80160474()` for every `SELKIND_COUNT` selection | Recorded through `fn_80173834()`, which applies the source Zelda/Sheik paired mapping |
| Challenge completion | `gmm_x1868.x1C88`; result code records IDs through `fn_8016F140()`, and the aggregate check considers `0..255` | All 249 entries required by `gm_80173EEC()` are set. That source check explicitly omits IDs `9`, `0x29`, `0x42`, `0x43`, `0xB9`, `0xC9` and `0xCA`; it derives the all-challenges award `0x123` from the completed set |
| Trophies | `TY_TROPHY_COUNT` IDs and `trophy_flags`/`trophy_count` | Every authored trophy is awarded through `Toy_SetUnlockState()`; the source maintains the count and flag encoding |
| Other source unlocks and trophy rewards | The source debug-unlock helpers cover 66 notification IDs and its reward helper covers 300 ledger IDs | The same source routines (`gm_8017297C()` and `gm_801741FC()`) mark these completed. No save extent is filled with a guessed mask or maximum |
| Derived feature bits | `gm_80172898(0xFFFF)` and its `fn_8017280C()` derivation | Only the four feature bits produced by the source derivation are present; unknown upper bits are left at fresh defaults |

The baseline leaves match counts, per-fighter statistics, records, scores,
play-time thresholds, unknown flags and padding at the original fresh defaults.
It does not fabricate high scores or imply that every possible record has been
set. Personal progress without a stored profile also begins with the complete
original fresh initialization, not with this Everything unlocked baseline.

## Card data and GCI files

The native bridge snapshots the extents declared by the original card manifest:
the first `0x1790` bytes of SaveData and seven `0x1F2C`-byte name-bank rows. That
is `0xF1C4` bytes of persistent card data. It leaves transient pending prizes,
process pointers, scene objects and active-match state outside the profile. The
native owner keeps the complete source backing for the duration of a scene and
restores the caller's bytes on teardown.

The browser encoder creates an 11-block GCI containing the original Melee
SaveData/name-bank records, checksums, marker and redundant primary copy. It
exports without a banner or icon image. Import verifies the GameCube card header,
USA game ID `GALE01`, Melee internal file name, supported block count, original
HSD card-block checksums, record identities and the redundant copy before asking
for confirmation. Import does not accept another region's game ID or an
unrecognized manifest.

A GCI identifies the USA game code but has no field for the Melee disc revision.
The player separately checks the selected disc as USA revision 1.02 before
starting it. Matching `GALE01` alone is not proof that a save belongs to that
revision. Dolphin's **Tools → GameCube Memory Card Manager** can import the
downloaded file into a selected memory card; open a card in Slot A or B, then
choose **Import Save File(s)**. A console transfer still needs compatible
GameCube memory-card hardware and a transfer method such as homebrew. No
physical-console test is claimed.

Dolphin's GCI-folder mode can also load individual GCI files from the
configured USA folder. These are container-loading workflows; they do not by
themselves prove that Melee accepts or uses the save. Dolphin's guide describes
transferring a Dolphin-created save back with GCMM and an SD card. No
physical-console test is claimed. Reference workflow and format boundaries are
documented in Dolphin's [Memory Card Manager](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/DolphinQt/GCMemcardManager.cpp),
[GCI-folder loader](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/HW/GCMemcard/GCMemcardDirectory.cpp)
and [save-transfer guide](https://dolphin-emu.org/docs/guides/ripping-games/).
These references were inspected for interoperability behavior; no Dolphin
implementation code was reused.

## Dolphin interoperability evidence

The earlier Memory Card Manager check established that Dolphin 2609 accepted
the exported file as a GCI container. It did not establish that Melee loaded
the expected progress or could write a later change back. The bounded test for
this review is recorded in
[`evidence/save-profile-dolphin-capability-v1.json`](evidence/save-profile-dolphin-capability-v1.json).
It used an isolated Dolphin user directory, USA GCI-folder slot A, null video,
and disabled audio output. The GCI was present before boot and had the same
SHA-256 after the process was stopped at the 20-second bound. There was no
controller or movie input, no observed gameplay or save screen, and no later
GCI export or browser reimport. Therefore this is a bounded capability
reproducer, not the required Melee/Dolphin save round trip.

The shared pinned reference-capture tooling and its receipts were also
inspected. That observer route intentionally sets
`Session.Core.SaveDataWritable=False` and closes its SRAM and directory-card
backing paths without writing; its existing capture evidence separately lists
virtual memory-card comparison as untested. It is useful for original-game
observation, but cannot supply the requested persistent-save change. Do not
remove that read-only guard to turn this interoperability task into an
unreviewed mutation of shared reference state. The reproduction used a
separate Dolphin 2609 installation and an isolated profile.

The actual cross-emulator check remains open: after importing a WebMelee GCI,
Melee must visibly load the expected completed or personal state, persist an
ordinary in-game change, and export a GCI that WebMelee then loads and preserves.
The browser codec tests are independent format checks and are not a substitute
for these observations. The output baseline in the bounded run used default
name-bank contents; it does not demonstrate an identifiable user-created name
or non-default settings crossing the emulator boundary.

## Provenance and licensing

`gamecube-save.mjs` was authored independently from the observed card format and
the original source card manifest. It contains no copied Dolphin implementation.
The source extents were checked against a private local Melee GCI. The generated
iconless GCI was previously accepted by Dolphin 2609's Memory Card Manager into
an isolated test card, which establishes container acceptance only. The
separate bounded boot attempt and its exact limit are recorded above.

The browser codec, IndexedDB store, Settings controller and codec tests are
project-authored and listed in the root MIT file scope. The native owner/host
bridge calls into recovered Melee/HSD source and is not added to that MIT scope.
The GCI import/export boundary must not be read as a license for game code, save
contents, assets, trademarks or disc images. The rest of the project retains
the mixed-provenance boundaries in [License scope](../LICENSE_SCOPE.md).
