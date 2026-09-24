# Mewtwo development checkpoint

Mewtwo is a development candidate on `codex/mewtwo-integration`. This
checkpoint preserves the original `CKIND_MEWTWO` / `FTKIND_MEWTWO` identity,
four costumes and authored source callbacks. It is not independent original
equivalence, full fighter acceptance or a deployment change.

## Source data and shared boundaries

`PlMt.dat` supplies `ftDataMewtwo`, the 0x88-byte `ftMewtwoAttributes`
extension and 314 serialized action rows. The extension keeps the original
signed words (Shadow Ball charge iterations/release lag, Teleport duration
and angle clamp, Confusion reflection max damage) and the reflection bone id
as unsigned; every other serialized word is a float. Both portable and native
decoders are exercised with negative counter values. Compile-time assertions
bind every field's offset, width and scalar type to the pinned source
structure. The extension region retains its exact 0x88 authored bound.

The original animation archive is `PlMtAJ.dat`. Costume owners use
`PlMtNr.dat`, `PlMtRe.dat`, `PlMtBu.dat` and `PlMtGr.dat` with the
`PlyMewtwo5K[_Re/_Bu/_Gr]_Share_joint` and `_matanim_joint` symbols. Effects
come from `EfMtData.dat`, `effMewtwoDataTable`, source bank 13 with four
static rows. Audio uses `mewtwo.ssm`.

The x48 Article table has exactly two authored slots. Slot zero registers
`It_Kind_Mewtwo_Disable` (0x6e): the two-float `itMDisableAttributes` record
(lifetime and horizontal velocity) with one serialized state row. Slot one
registers `It_Kind_Mewtwo_ShadowBall` (0x70): a twelve-word special record —
the doldecomp header's x30..x3C tail is not serialized by the disc record —
with ten serialized animation rows. Mewtwo is the first fighter whose
authored costume texture map contains four TObjs rather than two; the
original eye telemetry requirement "exactly two eye TObjs" was a sibling
assumption. The match-stats owner now requires the runtime collection to
match the fighter's authored `ftData` texture map (the original collector
stores at most five costume TObjs and asserts beyond that), validates every
authored TObj against the owned costume
descriptor graph, and keeps the declared two recorded eye slots and all
existing comparison fields unchanged.

Mewtwo's authored dynamics are one body chain with a single authored selector
row (table at 0x29b0), admitted under the same one-chain mode constraint as
Donkey. Shadow Ball charge holds through Start/Loop/LoopFull and releases
from a fresh B edge (like Donkey's Giant Punch) or cancels through the
shield; Teleport, Confusion and Disable are the other special families.

## Native evidence

The forward and reverse Final Destination fixtures pass all four Mewtwo
costumes, original Ready, combat, pause, No Contest, teardown and subsequent
construction. The first forward lifetime additionally observes:

- Ground Shadow Ball full charge, fresh-B-edge release, 25 damage to Mario,
  and Article cleanup through its source lifetime.
- Aerial charge through the source air Loop motion, fresh-B-edge release in
  the air (air End 350), and Article teardown.
- Ground and aerial Teleport, returning through source recovery.
- Ground and aerial Confusion.
- Ground and aerial Disable, including the Disable projectile's creation and
  source lifetime teardown.

Earlier recipe failures are retained: the release-on-button-release routes
left Mewtwo held in LoopFull because the source releases on a pressed edge;
the native trace recipes now re-press B. The real-attribute boundary test
covers the 0x88 extension, signed/unsigned typed words, authored values,
dynamics facts and rejection paths. The action-store trace admits Mewtwo's
authored self rows 295–313 over the 314-row table.

## Browser and remaining gates

The versioned `mewtwo-visible-actions-v1` inventory contains 32 cases and a
6,400-frame minimum per round. Two recipe corrections were required before
the inventory could run: Mewtwo's authored Turn outlasts the shared 12-tick
walk windows (both directions use 60-tick holds), and the aerial Shadow Ball
charge needs a full hop — the short hop ends before the source Start/Loop
charge gate completes. No source timing or state is changed by these
diagnostic recipes.

The first full discovery (v5) completed all 32 cases; cold failed the
acceptance gate with browser callback gaps, native callbacks over 33.3 ms
and audio underruns. Its fresh-origin cold export contains 12 new portable
type-1 descriptors across entry and actions, with zero payload conflicts and
all 846 previous records preserved field for field. The reviewed merge
produces a seed with one shader and 857 pipelines, 3,567,616 bytes, SHA-256
`f85858ff99f368d38ca16fa18727a12f778d57543e2898d29507f92585db2c2b`.

Both fresh cold/warm rounds pass after that preload correction: 6,400 source
frames and callbacks each, native/browser maxima 6.645/24.720 ms cold and
5.920/21.710 ms warm, zero callback gaps, long tasks, callbacks over budget
or over 33.3 ms, audio underruns, live pipelines queued or created,
preparation pauses, automatic timing resumes, focus losses or wasm heap
growth. The failed v5 cold discovery remains retained at
`work/full-game/mewtwo-browser-discovery-v5/` with its provenance manifest
and cache review; the passing pair is
`work/full-game/mewtwo-browser-discovery-v6/`.

Independent original Mewtwo comparisons, pixels, PCM, physical controllers,
complete ordinary-input matches, all move/costume/stage combinations and
broader performance remain open. Follow the [full-game
inventory](FULL_GAME_PORT.md) and [accuracy contract](ACCURACY_CONTRACT.md)
for the remaining product boundary.

## Staging side-special crash (fixed 2026-09-21)

A staging report showed the player stopping ("Player stopped. Reload to
recover.") exactly when side-specialing with Mewtwo. Three fixes went into
commit `1af6c92`; each is retained with its reproduction:

1. **Mewtwo's side special is a source command grab.** When Confusion's hit
   connects on the ground, `ftCo_800BCF18` runs `CaptureMewtwo` and the victim
   enters the common `ThrownMewtwo`/`ThrownMewtwoAir` submotions (SM 292/293).
   Those command rows were never admitted to any action store, so the victim's
   row dispatched the unsupported-command sentinel and aborted mid-match.
   They are now admitted for every store under the same rule as Koopa's
   capture rows: these source command graphs belong to every possible victim.
   The trace regression is `Mewtwo ground Confusion capture lifetime passed`
   in `gameplay_content_match_trace` (runs per costume cycle, both Mario and
   Young Link opponents).
2. **The source boot-time rumble interpreter pool was missing.** Source
   `gmMain_8015FD24` installs a 12-entry rumble list pool before any scene
   runs; the port only installed it inside match begin, leaving the pad
   library's `rumble_info` uninitialized for every menu scene. The world
   now installs the pool at startup (`melee_web_rumble_begin`). The merge
   review found that the initial repair reached match worlds only; the menu
   world now loads and owns `LbRb.dat`, publishes the pool before source
   OnEnter, and advances the original rumble interpreter before renewing PAD
   samples. Teardown releases queued programs before their arena is freed.
3. **Menu scene gobjs leaked into later scenes.** The original game-mode
   layer re-initializes the HSD gobj library at every scene change
   (`gm_801A4BD4`); the retained one-world port does not, so CSS gobjs —
   including the confirm-tag processor `fn_80262F44` whose confirm rumble
   faults against the next world's rumble state — kept running inside the
   match. The menu host now snapshots every retained GObj before the scene's
   original enter and destroys new objects on leave using the source teardown
   primitive. The configured source link bound includes the SSS fog on link
   15. Full membership also finds new equal-priority objects inserted after a
   retained object; the earlier list-head sentinel missed those objects.

The reproduction evidence is retained under `work/full-game/`: the runtime
reflect sweep (`mewtwo-sideb-probe/progress.log`) reproduces the staging
stop on the pre-fix build — the aborting chain was
`melee_web_command_require_supported` ← `ftAction_80073240` ←
`Fighter_ChangeMotionState` ← `ftCo_800BD0E8` (`CaptureMewtwo`/`ThrownMewtwo`)
— and the post-fix Release sweep completes all 15 rounds; the local suite
runs 1,131 tests OK (`mewtwo-crashfix-suite-v5.log`).

## Merge review follow-up

The earlier repair did not initialize rumble for `GameplayMenuWorld`; changing
build configuration could conceal that missing owner. A new real-asset menu
regression failed before the fix with `Menu startup did not publish an available
source rumble pool`. After publication and per-sample interpreter renewal were
added, the same native trace completes both original CSS/SSS/four-stock-match/CSS
cycles, including SSS cancellation, No Contest, interrupted match teardown and
repeat construction. This is **Native traced**, not retail or timing acceptance.

The focused ownership regression links the pinned `gobjplink.c` implementation
and covers equal-priority insertion behind a retained object, source link 15,
and abort after the scene has already closed. The rumble regression uses the
world's actual pool and checks teardown with an outstanding borrowed program.
Builds, browser runs and the integration suite are recorded in the
[merge review receipt](evidence/mewtwo-crash-merge-review-v1.json).
