# Luigi development integration

Luigi is the eleventh development fighter on `codex/full-game-integration`.
Native construction and repeated match lifecycles pass in both player orders.
The browser reaches original CSS/SSS and passes all 30 visible action cases;
the first discovery run fails timing, audio and live-pipeline gates. A reviewed
preload correction clears both cold/warm reruns. Original comparison and
broader acceptance remain open.

## Source contract

Luigi is `CKIND_LUIGI = 7` and `FTKIND_LUIGI = 17`. The pinned source uses
`PlLg.dat` / `ftDataLuigi`, `PlLgAJ.dat`, four model costumes, and 312 action
rows. Its extension is the exact 0x98-byte `ftLuigiAttributes` record. The
single source table in `src/gameplay_luigi_schema.h` preserves every float and
the signed integer fields at offsets `0x88` and `0x94`.

The complete extracted source contract, archive hashes, Article/effect/audio
roots, nullable fields, and command graph are retained in the ignored
[Luigi source contract](../work/full-game/luigi-source-contract-v1.md).

## Attribute evidence

The C and C++ owners use the same field table. The C path checks source and
portable offsets, widths, and scalar categories at compile time. The C++ path
stores Luigi in a distinct typed optional and bounds the extension by its
authored 0x98 referenced region.

Evidence:

- [tracked runtime test](../work/full-game/luigi-focused-runtime-test-v2.log)
  passes both the existing six cases and the real `PlLg.dat` case. The real
  case checks 312 actions, a distinct Luigi optional, the 0x98 region, Green
  Missile misfire chance `8.0`, charge values, and signed Cyclone values `3`
  and `0`.
- [tracked native-data test](../work/full-game/luigi-focused-data-test-v2.log)
  passes the Wasm C decoder compilation and existing native data regressions.
- [source/portable ABI check](../work/full-game/luigi-source-abi-check-v2.log)
  compiles the pinned `ftLuigiAttributes` and portable table to Wasm32 and
  passes every offset, width, and scalar-type assertion.

## Articles, assets and actions

The Article owner admits `It_Kind_Luigi_Fire = 0x69` with its real
16-byte/four-float special block, one state, model/material resources and
source command semantics. `PlLg.dat` relocations place the special block at
14964 and the command root at 14980, so the first command word
`0x2c000006` is not a fifth attribute. The source Luigi callback reads
special x0/x4/xC; its shared collision helpers read common ItemAttr data and
do not consume special x8. The source Article table has one authored slot; the
decoder and C++ asset-owner validation use that exact one-slot bound and do
not fabricate missing Articles.

The asset owner preserves Luigi's authored non-null 61-joint guard graph
(target 36608; no DObjs), effect bank 18 with two entries, the original shared
Mario effect consumers and English/Japanese audio identities. All four costumes
retain their authored material-animation tables. The action owner admits the
312-row table with self actions 295–311 after the checked common/self command
graph. Unexercised command/service paths, including opcodes 42 and 45, remain
explicit future gates; action-table admission is not proof every move branch ran.

## Native and browser evidence

The selected-player native fixture covers Luigi/Mario over five costume cycles
(Mario has five costumes; Luigi cycles through all four). It observes ground
and aerial Fireball Article creation/removal, six damage from the ground
projectile, and source Green Missile, Super Jump Punch and Cyclone state entry.
Luigi keeps four stocks in this bounded native recipe. Pause, L+R+A+Start
No Contest, original completion and repeated teardown pass. The reverse
Mario/Luigi fixture also passes all costume cycles; its actively driven player
is Mario, so it does not duplicate Luigi's selected-player special coverage.
See [selected-player log](../work/full-game/luigi-native-v3.log) and
[reverse-order log](../work/full-game/luigi-native-reverse-v1.log).

The first native attempt exposed an unused fixture jump helper. The second
stopped observing at aerial Fireball state entry before its source command
spawned the Article. Calling the existing jump helper and observing the source
Article through its actual spawn fixes the fixture; no gameplay timing or RNG
was adjusted. Green Missile retains its original random misfire choice.

The first browser attempt used the prototype wrapper URL with the development
driver and never imported the disc. Correcting the route to `runtime.html`
reaches original Luigi/Mario Final Destination gameplay. All 30 action cases
pass over 5,200 drawn source frames, including ground/aerial specials and
Green Missile charge/release. The visible ground missile leaves the stage and
loses a stock; this differs from the bounded native state-entry recipe.

The [discovery report](../work/full-game/luigi-browser-discovery-v2/report.json)
retains two browser callback gaps, three native callbacks over budget (two over
33.3 ms), 21 audio underrun frames and four live pipelines. Native/browser maxima
are 82.995/87.520 ms on M4/macOS 26.6.2/Chrome 153.0.8010.50, DPR 1. There are no
timing resumes or focus losses. Entry metrics are separate and retain their
own timing/audio failures. This run is diagnostic browser evidence, not a
performance pass.

A [reviewed cache candidate](../work/full-game/luigi-cache-review-v1.json) adds
six portable descriptors from entry/actions, preserving all 716 previous rows
and their payloads. The updated cache has one shader and 721 pipelines.
The [cold/warm report](../work/full-game/luigi-browser-pair-v1/report.json)
passes all 30 cases in each 5,200-frame run, with zero native budget misses,
browser gaps/long tasks, audio underruns, live pipelines, focus losses, timing
resumes or Wasm heap growth. Native maxima are 6.940/8.090 ms and browser
maxima are 22.805/22.750 ms. Both runs unload the owned world; the warm run
uses a complete application reload with persisted cache.

After the shared map-light/OnLoad correction and Fountain's reviewed seed
update, the [fresh cold/warm regression](../work/full-game/luigi-browser-pair-v2/report.json)
again passes all 30 cases and 5,200 frames per run, with the same zero hard
failure counters and no heap growth. Native maxima are 8.880/7.570 ms and
browser maxima are 26.580/22.240 ms. Live texture uploads are recorded separately
at 1,444,352/1,132,032 bytes. The corresponding refreshed native checks pass in
[Luigi/Mario order](../work/full-game/luigi-native-v4.log) and
[Mario/Luigi order](../work/full-game/luigi-native-reverse-v2.log).
This evidence covers the frozen development build and action-sweep windows.
Original-game comparison, a complete ordinary menu round trip,
independent action coverage, pixels, PCM, physical input and live scheduling
remain separate gates.
