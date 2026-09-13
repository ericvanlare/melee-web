# Owned persistent save prerequisite

The cold boot menu driver requires an ordinary persistent GameCube card that is
already usable by the pinned original GALE01 USA 1.02 run. The driver does not
write unlock state, load a save-state, use codes, or substitute a synthetic
card. This note records the bounded local search and the input still required.

## Bounded search result

The read-only search covered the installed Dolphin support tree, its GC card
directory, the known macOS Slippi Launcher/Slippi profile locations, common
Dolphin/Slippi configuration locations, and the repository's known reference
work directories. It found 1,010 save-like artifacts: 5 unique GCI contents,
11 unique raw metadata contents, and 14 unique save-state contents. The raw
records were 68-byte Dolphin metadata files, and save-states are not persistent
card inputs.

The five unique GCI SHA-256 values were:

- `2719a260a3abfadd3a07e25b5d94ebae9228e231c2b532d702cd3cf97bb57b85`
- `4f4f437acfa15fd6bf0d17226db61cbcaa1a8d49df7afd6d3d9abd37d60a88bd`
- `822ec2f069537aac668d2948a43c397ebbdba057aa35b7acdb27439c39d5de4d`
- `83aaacc5f5a1e1264af98ecfe965a7a15a6d6fb47567055bf837686ab827c19a`
- `d5e3a3febad69adfe1fd1876433bec5acf06772a7b76349303f1a4d34e2d1363`

Four had the expected GALE01 revision-2 card metadata and one did not. Every
candidate had zero character and stage unlock masks. GCI header metadata does
not establish revision-1.02 disc, DOL, or boot compatibility; no candidate
passed an original unlock screen, so actual ordinary boot/menu compatibility
was not verified. No unlock playthrough was performed.

## Minimal prerequisite checklist

This is a source-grounded prerequisite checklist, not a claim that the listed
route is globally fastest:

1. Use an owned original GALE01 USA 1.02 disc/DOL and an ordinary GameCube
   memory card with normal saving enabled. Disable codes and other unlock
   mechanisms.
2. Complete the ordinary roster dependencies. The source VS result check
   `gm_80172E74` requires the 14 starter-character completion flags before it
   triggers Marth (`src/melee/gm/gm_16F1.c`). The 100-Man result path checks
   Falco through `gm_80173460` (`src/melee/gm/gmmultiman.c` and
   `src/melee/gm/gm_16F1.c`). These checks only trigger challenger encounters;
   they do not themselves prove that the unlock was committed.
3. Defeat each triggered challenger in the original game. The successful
   challenger result path calls `gm_UnlockCKind` only after a non-retry result
   with the human player still holding a stock (`src/melee/gm/gm_1BFA.c`).
4. Complete the ordinary stage dependencies: the source checks all `0x19`
   target-test completion flags for Dream Land, completes All-Star before
   committing Battlefield, and requires all `0x33` event-match flags before
   committing Final Destination (`gmMainLib_8015D5DC`, `gm_8017CBAC`,
   `gm_8017335C`, and `gmevent.c`).
5. Let the original game save its progress to the enabled card. Resolve its
   normal overwrite/continue prompts through the original UI, then quit and
   reboot the original retail run with codes disabled.
6. From the fresh original boot, verify through the original CSS and SSS that
   Mario, Fox, Falco, and Marth are selectable and that Final Destination,
   Battlefield, Yoshi's Story, and Dream Land are selectable. Only after all
   eight checks pass should the card be frozen/copied for a reproducible run.

The search and this project did not perform the unlock playthrough or write any
save data. No internet source or downloaded save is an acceptable substitute
for the owned persistent input.
