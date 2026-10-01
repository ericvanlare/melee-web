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

### Preserve saved preferences across browser startup

The source menu host temporarily applies the browser's supported gameplay
configuration on ordinary scene entry. It sets item frequency to the source
default, enables every item in the item mask, enables rumble for controller
ports 1 and 2, and selects the USA saved language. Those writes affect the live
source profile; they are not edits made by the player to a Personal save. Before
that entry, the native owner captures the persisted preferences from the active
source profile and overlays only those typed fields onto each live source
snapshot. The remaining SaveData and name-bank bytes still come from the current
source state, so this does not freeze the imported profile or suppress progress
autosaves.

The captured fields are `gmm_x1CB0.item_freq`, `item_mask`, all four
`rumble_enabled` bytes, and `saved_language`, at SaveData offsets `0x448`,
`0x450–0x457`, `0x458–0x45B`, and `0x45E`. The same path captures source
defaults for a genuinely fresh Personal profile before the first CSS entry, so
its first commit does not inherit the temporary browser startup writes. Imports
capture their values only after the validated profile has been applied.

The startup audit found no browser-startup writes to `sound_balance`,
`deflicker`, rumble ports 3–4, or the saved `stage_mask`; these continue to come
from live source snapshots. The runtime language selector in `gmm_x0` is outside
the card data and is restored at the scene boundary. This change does not add
original Settings menus or claim that browser-supported runtime features match
every saved option.

The production audio-player regression uses a synthetic GCI with a non-default
item mask, item frequency, and rumble pattern. It confirms the preferences,
source progress fields, and name through confirmed Settings import, live
autosave, export, browser reload, and source relaunch. Its separately scoped
receipt is [`evidence/save-profile-startup-preferences-v1.json`](evidence/save-profile-startup-preferences-v1.json);
the fixture is clearly distinguished from the game-written Dolphin evidence
below.

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

Challenge completion also raises transient, out-of-card trophy notifications
while the source routines run. The baseline clears those session-only notices
with Melee's `gm_80172174()` after recording the completed state. This leaves
the completed SaveData intact and avoids replaying newly-created completion
notices on Title entry.

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

The Settings file picker checks the selected file's exact 90,176-byte GCI
extent using the codec's exported `MELEE_GCI_FILE_BYTES` constant before
calling `File.arrayBuffer()`. Wrong-sized inputs are rejected before parsing,
confirmation, save writes, or source restart; exact-size files still pass the
full format and checksum validation above. The [bounded browser regression
receipt](evidence/save-profile-oversized-import-v1.json) records an oversized
input rejected without reading bytes and a synthetic exact-size GCI that
continues through confirmed import. This fixture is controller-level input
validation, not game-written interoperability evidence.

Reproduce it with the installed headless browser runtime:

```sh
node tests/save_profile_oversized_import_browser_test.mjs \
  --out work/save-profile-oversized-import \
  --playwright <installed-Playwright-package>
```

The output directory must be new. `scripts/browser_tools.mjs` selects the
installed Chrome binary and keeps host audio muted.

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

The completed headless round trip and its exact identities, input, hashes,
field comparisons and observations are recorded in
[`evidence/save-profile-dolphin-roundtrip-v1.json`](evidence/save-profile-dolphin-roundtrip-v1.json).
The reusable Dolphin and browser harnesses are
[`scripts/save_profile_dolphin_interop.py`](../scripts/save_profile_dolphin_interop.py)
and
[`tests/gamecube_save_interop_browser_test.mjs`](../tests/gamecube_save_interop_browser_test.mjs).

WebMelee exported its Everything unlocked baseline (characters `07ff`, stages
`07ff`, derived features `0f`). An isolated Dolphin run used normal controller
input in Name Entry and wrote the identifiable full-width name `ＡＡＢＢ`
(normalized display `AABB`) into name bank 0, record 0. Settings imported that
game-written GCI, confirmed the switch to Personal progress and restarted the
player. The reloaded Personal export retained the completed masks, name, item
frequency 2 and all four enabled rumble flags.

A fresh isolated Dolphin user and USA GCI folder then loaded that re-exported
Personal GCI. The observer recorded Melee reaching its title and main menu; no
corrupt-save or reset path appeared. Ordinary Pipe controller input opened
Settings → Rumble, toggled Controller 1 rumble off, then exited both menus. The
resulting GCI changed from SHA-256
`5184f7f9bfcbd35ea7cc07904cbed557b8a7fc9e624a05aa02c8d1d308d4d729` to
`5aeb2c9dd8b274a66898ea7640feed36009422c55267d0eed252688cf8778fb3`.
Melee's output retained character/stage masks `07ff`/`07ff`, feature byte
`0f`, item frequency 2 and name bytes `8260826082618261` (`ＡＡＢＢ`). Its
Controller 1 rumble value changed from 1 to 0. The source-named SaveData
PowerCount and PowerTime fields also advanced from 2 to 3 and 16 to 28 during
the game session. Melee's physical card slots held logical records 1 through 8
in slots 1 through 8, the free marker in slot 9, and a redundant SaveData
record 1 in slot 10. The duplicated SaveData records used sequence 2 while the
unchanged name banks remained at sequence 0; import selected by logical ID and
the source sequence ordering. The reader requires all eight logical records,
one free-block marker and the redundant SaveData record. It ignores a
checksum-invalid block only when the authenticated SaveData copy remains; a
missing name bank, invalid marker, malformed identity or conflicting
same-sequence copy still fails import. The wrap comparison follows HSD's
`fn_803ACB74`.

Settings then imported the Dolphin-written GCI with the confirmation dialog,
switched to Personal progress and restarted. A browser reload retained Personal
mode; its exported profile bytes matched the Dolphin-written profile across
the full declared SaveData and seven name-bank extents. The visible headless
Chrome capture shows the completed roster after the import. The receipt records
the rendered-browser checks and the Melee observer's scene/menu trace
separately; Dolphin video frames and audible fidelity were not measured.

The isolated client reported `Dolphin 2606a-dirty`, binary SHA-256
`087e212bc1537f0bb79c9e9375311f75d773aaa5cb36cf4bb32a864da21c185b`, based on
source commit `c77bbaa0f372c3f72281602a8b087206706542cb`. It includes the
repository's passive observer and Pipe controller test overlay plus a
`SaveDataWritable` guard used by the shared read-only capture mode. This run
explicitly set `Session.Core.SaveDataWritable=True`; it used a new Dolphin user
directory and disposable USA GCI folder, `-v Null`, and `No Audio Output`. No
personal Dolphin profile was used. The guard was not removed from shared
fixtures. The Melee disc is USA revision 1.02, disc SHA-256
`b7de482eb955c8a96b6746dfa043b69ae7bf6c7c2a09ac382b9da126faa7055c`, and
`main.dol` SHA-256
`dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646`.

The initial bounded, no-input boot attempt remains useful only as a capability
reproducer; its earlier limitations are preserved in
[`evidence/save-profile-dolphin-capability-v1.json`](evidence/save-profile-dolphin-capability-v1.json).
The successful round trip above supersedes it as compatibility evidence. No
Dolphin implementation code was copied into project code.

## Provenance and licensing

`gamecube-save.mjs` was authored independently from the observed card format and
the original source card manifest. It contains no copied Dolphin implementation.
The source extents were checked against a private local Melee GCI. The generated
iconless GCI was previously accepted by Dolphin 2609's Memory Card Manager into
an isolated test card, which established container acceptance only. The
subsequent no-input boot attempt also did not establish interoperability; both
limited checks are superseded by the ordinary-input, game-written round trip
above.

The browser codec, IndexedDB store, Settings controller and codec tests are
project-authored and listed in the root MIT file scope. The native owner/host
bridge calls into recovered Melee/HSD source and is not added to that MIT scope.
The GCI import/export boundary must not be read as a license for game code, save
contents, assets, trademarks or disc images. The rest of the project retains
the mixed-provenance boundaries in [License scope](../LICENSE_SCOPE.md).
