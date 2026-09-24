# Bowser development checkpoint

Bowser is a development candidate on `codex/full-game-integration`. This
checkpoint preserves his original `CKIND_KOOPA` / `FTKIND_KOOPA` identity,
four costumes and authored source callbacks. It is not independent original
equivalence, full fighter acceptance or a deployment change.

## Source data and shared boundaries

`PlKp.dat` supplies `ftDataKoopa`, the 0xa0-byte `ftKoopaAttributes` extension
and 316 serialized action rows. The extension retains the original signed
words at x4/x20, unsigned words at x2c/unk50 and floating-point fields. Both
portable and native decoders are exercised with negative signed values and a
high-bit unsigned value. Native compile-time assertions bind every field's
offset, width and scalar type to the pinned source structure.

The original animation archive is `PlKpAJ.dat`. Costume owners use
`PlKpNr.dat`, `PlKpRe.dat`, `PlKpBu.dat` and `PlKpBk.dat`. Effects come from
`EfKpData.dat`, `effKoopaDataTable`, source bank 12 with four static rows;
the complete-null fourth row keeps its source identity. Audio uses
`koopa.ssm`.

The x48 Article table has exactly one authored slot. Slot zero registers
`It_Kind_Koopa_Flame` (0x50): six source floats, one serialized command state,
and a present model descriptor with an authored null joint. The existing
source item constructor supplies its identity JObj. The generic publisher
continues to preserve scalar flags and reject unsupported bone, attachment
and animation dependencies; it does not invent an empty Article or read
beyond the one-slot table.

Koopa Klaw requires the victim's original common submotions 278–283 as well
as Bowser's self rows 295–315. The source motion-state table maps capture,
damage, wait and ground/air throws to those submotions or to `SM_None`.
Loading only Bowser's own actions would leave the opponent's command graph
unavailable. A read-only observer checks the actual `victim_gobj`, fighter
identity and both source motion IDs.

Whirling Fortress exposed a separate common-command gate: FallSpecial uses
opcode 36 to assign the original `Fighter.x221E_b4` visibility flag. Its
decoder and source consumer already existed. Native admission now permits
that command, and a focused test executes the original consumer for both
values while checking command advancement and the adjacent flag.

## Native evidence

The forward and reverse Final Destination fixtures pass all four Bowser
costumes, original Ready, combat, pause, No Contest, teardown and subsequent
construction. Five fixture lifetimes also cover Mario's five costumes; the
fifth forward lifetime repeats Bowser's neutral costume.

The first forward lifetime additionally observes:

- Ground Flame creation, damage to Mario, release and Article cleanup.
- Aerial Flame creation in motion 345, landing into ground Flame 342 and
  ground End 343, followed by Article cleanup.
- Ground and aerial Whirling Fortress, returning through source recovery.
- Ground and aerial Bowser Bomb, including descent and landing motion 363.
- A real grounded Mario capture, held-victim wait and forward throw.
- A real aerial Mario capture and backward throw.

The aerial Flame recipe lands before its minimum source hold finishes. An
earlier check expecting aerial End 346 fails and remains retained; the
passing check specifically establishes the source landing/end route. It does
not cover aerial End without landing. The input fixture holds X through
Bowser's eight-tick jump squat and waits for thrown Mario to recover before
walking toward him. Neither correction writes fighter state or changes source
physics, RNG or timing.

The local logs are `work/full-game/koopa-native-moves-v4.log` and
`work/full-game/koopa-native-reverse-v1.log`. Earlier observer, test-extraction,
input-recipe and opcode-admission failures remain alongside them.

## Browser and remaining gates

The versioned `bowser-visible-actions-v1` inventory contains 30 cases and a
5,600-frame minimum per round. Both discovery rounds complete every case.
Cold fails with three hard callback gaps, 615.295 ms native / 628.020 ms
browser maxima, 149 audio underrun frames and 18 live pipelines. It also
needs two diagnostic resumes before Ready and two during the sweep. Warm
passes all 5,600 frames with 8.655 ms native / 26.365 ms browser maxima and
zero timing, audio, pipeline or heap-growth failures. These are action-window
measurements, not original-equivalence or complete-match evidence.

The cold export contains 36 new portable descriptors across entry and actions.
The reviewed merge preserves all 810 previous shader/pipeline records without
payload conflicts, adding only those type-1 descriptors. The resulting seed
contains one shader and 845 pipelines, 3,518,464 bytes, SHA-256
`8df6a998cef19b88a3eff0ee61f666e42791f5f73841818961aa3b224cad2c4b`.
Both fresh cold/warm rounds pass after that preload correction: 5,600 and
5,601 source frames, with native/browser maxima of 8.445/23.405 ms cold and
9.115/25.995 ms warm. Both have zero target misses, hard gaps, long tasks,
audio underruns, live pipelines, timing resumes, focus losses or heap growth
in the action window, and neither needs an entry timing resume. The failed
discovery remains retained at `work/full-game/koopa-browser-discovery-v1/`;
the passing pair is `work/full-game/koopa-browser-discovery-v2/`.

Independent original Bowser comparisons, pixels, PCM, physical controllers,
complete ordinary-input matches, all move/costume/stage combinations and
broader performance remain open. The browser side-special cases check startup;
the linked-victim capture/throw checks above are native evidence, not drawn
interaction performance. The separate Donkey Kong Battlefield Pass
crash remains unresolved. Results and replacement audio remain separate draft
dependencies. Follow the [full-game inventory](FULL_GAME_PORT.md) and
[accuracy contract](ACCURACY_CONTRACT.md) for the remaining product boundary.

The current import plan requests 177 FST files totaling 127,314,690 bytes,
leaving 6,903,038 bytes below the 128 MiB request limit. Generated font and DSP
coefficients are separate. This is requested disc data, not a measurement of
resident heap. The remaining roster requires the explicit scene asset and
teardown work described in the inventory; this checkpoint does not raise the
limit or make required assets optional.
