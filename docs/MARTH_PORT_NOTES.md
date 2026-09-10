# Marth source-port notes

These notes record the GALE01 revision-2 contract used to add Marth. Extracted
disc files remain local; the repository pins only their identities and decoded
metadata.

## Source and asset identity

| Role | Revision-2 identity |
| --- | --- |
| Character/fighter kinds | `CKIND_MARS` 9, `FTKIND_MARS` 18 |
| Fighter data | `PlMs.dat`, public root `ftDataMars`, 327 action rows |
| Animation container | `PlMsAJ.dat` |
| Costumes | `PlMsNr.dat`, `PlMsRe.dat`, `PlMsGr.dat`, `PlMsBk.dat`, `PlMsWh.dat` |
| Effects | `EfMsData.dat`, `effMarsDataTable`, bank 16, two entries |
| Audio | `audio/us/mars.ssm` |
| Article table | Null in the fighter DAT |

The costume model roots are `PlyMars5K_Share_joint`,
`PlyMars5KRe_Share_joint`, `PlyMars5KGr_Share_joint`,
`PlyMars5KBk_Share_joint`, and `PlyMars5KWh_Share_joint`. The runtime uses the
typed content row for all costume, effect, and audio ownership.

Marth adds the exact 0x98-byte `MarsAttributes` extension. The field map is
shared by the portable DAT reader and the Wasm32 source owner so a new consumer
cannot silently reinterpret its Counter descriptor or sword parameters.
Marth's DAT has three dynamic-bone chains and five authored mode rows. Those
mode entries are small integer chain cutoffs stored in pointer-width source
fields; the port preserves that ABI instead of fabricating animation objects.

## Action coverage and runtime findings

The checked action range includes special rows 295 through 326. The integrated
trace constructs Marth in all five costumes and executes jab, a charged neutral
special, Dancing Blade entry and continuation, Dolphin Slash, Counter, an
aerial, pause, match exit, and repeated teardown. Jab exposed command opcode 49;
the linked original `ftAction_80072B3C` is now the checked handler.

The Release browser gate selects Marth through the original CSS and Dream Land
through the original SSS, then runs the versioned 46-case
`marth-visible-actions-v1` inventory. It covers every grounded normal,
shield/roll/dodge/grab, all five aerials, air dodge, ten wavedashes in each
direction, Shield
Breaker charge/release, Dancing Blade, Dolphin Slash, Counter, and 4,200 or more
Dream Land source frames. The runner recenters Marth through ordinary source
PAD input so movement cases cannot silently fall offstage, and it checks the
expected source motion IDs for every case.

The exhaustive run found source command opcode 31 in roll motion 234. The native
command decoder already preserved its signed DObj visibility operands and the
linked original handler was present; the strict admission list now allows that
checked handler, with an operand regression test. An earlier representative run
had not exercised roll and therefore missed it. The same broader run exposed
`GXSetTevClampMode` in Marth's live draw path. The GALE01 symbol is four bytes
and returns immediately, so the browser bridge matches that retail no-op instead
of calling the assertion present in the SDK source drop.

The first cleared-origin 46-case capture failed as intended: seven callback gaps
(worst 284.295 ms), 404 audio-underrun frames, live pipeline creation and timing
pauses, while native callbacks stayed below 17.64 ms. Eleven descriptors were
captured after visible play and teardown. With the resulting 344-pipeline seed,
the cleared-origin rerun passed 6,070 source frames with a 22.785 ms worst browser
interval and 19.38 ms worst native callback, and zero gaps, long tasks, audio
underruns, live pipeline creation, automatic pauses or focus loss. Wasm heap
growth is recorded separately in every report so a future growth event can be
correlated with a timing failure instead of being mistaken for CPU time.
After teardown and a complete application reload, the warm run passed all 46
cases across 6,109 source frames with a 21.06 ms worst browser interval and
15.325 ms worst native callback; every hard gate remained zero.

A final stricter rerun excluded recovery/recentering motions from each following
case's expected-motion observation. It passed all 46 cases across 6,289 source
frames with a 26.3 ms worst browser interval, 22.7 ms worst native callback, and
every hard timing/audio/pipeline/focus gate at zero. The report recorded
113,508,352 bytes of live Wasm heap growth without a correlated timing or audio
failure.

## Owned asset hashes

```text
0c4b7e49c8d18bfb3f5fd8c463d7c00b70292fa971b1ee94caf99548ba5b45b2  PlMs.dat
a9560b7fdfca328dd3deeb6e999edc7ea1e89abe837c3846afdd46972b2ca308  PlMsAJ.dat
e4f2b635373f23510d81cdb395b9633501ca52135f7694eb9a31af7aebe38c72  PlMsNr.dat
930cd89313b0e73b3113d9e9421fae55998e1aa69abbaab5953fda762ce89eb4  PlMsRe.dat
fa8771bd99690868dfd0fb3508be131a5ccb1b904aa348955d4176aa4315974c  PlMsGr.dat
3e54661fe43c8edba617e9f0cdcac0c0cd50172ee67953c413ac26626f53e8c5  PlMsBk.dat
51bf4a608510b43a74c71cb5f22637aab33ab4dd66658c88506af22c44a5bfad  PlMsWh.dat
efad8db0868dcb7e8b1ed24f31cc9101cbf9a3659582903f3619f52c1083e8a1  EfMsData.dat
458c377ed374baf51029498651b57ba06c9b5dff22e55ff492ef60908aa740b2  mars.ssm
```

Run the portable identity check and complete source-match matrix with:

```sh
python3 -m unittest -v tests.test_marth_real_assets tests.test_gameplay_content_match
```

For the next character, start with the same gates: exact asset identity, typed
extension and dynamics decoding, checked action construction, integrated source
execution across every costume and admitted stage, then a complete versioned
Release action inventory from cleared and warm origins. Capture first-use
pipelines after teardown, update the seed, and repeat both runs before admission.
