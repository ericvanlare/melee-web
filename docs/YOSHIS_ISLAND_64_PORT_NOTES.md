# Yoshi's Island 64 source-port notes

This note records the checked scalar boundary and bounded native integration
for the pinned GALE01 revision 2 source. The native stage and entry traces pass
their declared scopes; they do not claim complete playable, browser or
original-reference acceptance for Yoshi's Island 64.

## Source and archive identity

- `St_Kind_OldYoshi = 0x1D`; `Gr_Kind_OldYoshi = 0x1D`.
- `grOy_StageData` and its six callbacks are defined in
  `melee/gr/groldyoshi.c`.
- `/GrOy.dat` is 247,034 bytes, SHA-256
  `31338847be3e4d0ed52d8c404d2b750369a4cb76e31a7217071ef186283d8d8c`.
- The source `StageParam` row selects primary BGM 59 (`audio/old_ys.hps`);
  both alternate IDs are `-1`.

## Checked yakumono ABI

The anonymous source struct at `groldyoshi.c:54-65` is exactly 28 bytes:

```c
s16 x0; s16 x2;
float x4; float x8; float xC;
s16 x10; s16 x12; s16 x14; s16 x16; s16 x18;
```

The portable header preserves those offsets and scalar types and names the
compiler's final two padding bytes explicitly. The decoder reads only this
0x1c-byte source region through `MeleeWebNativeDat`; it does not reinterpret
following archive data.

The owned `GrOy.dat` values are:

`x0=120`, `x2=180`, `x4=0.15`, `x8=0.15`, `xC=6`, `x10=30`, `x12=30`,
`x14=3000`, `x16=4000`, and `x18=30`.

The checked source bounds are limited to uses visible in `groldyoshi.c`:
nonnegative counter and RNG fields (`x0`, `x2`, `x10`, `x12`, `x14`, `x16`),
nonnegative velocity/displacement bound `x4`, positive acceleration step `x8`,
and nonnegative displacement ceiling `xC`. Floats must be finite. No ordering
check is imposed between `x14` and `x16`: `gr/inlines.h:193-202` implements
`rand_range(a,b)` by normalizing either argument order. In the authored data,
`x14=3000` is the inclusive lower bound and `x16=4000` is the exclusive upper
bound, even though the source calls `rand_range(x16, x14)`.

The source consumers are:

- `x0`: contact-frame threshold before a cloud leaves state 0.
- `x2`: hidden-state reappearance delay before state 2 advances.
- `x4`: signed cloud velocity clamp passed as `[-x4, +x4]`.
- `x8`: acceleration step used by `grOldYoshi_8020F31C`.
- `xC`: contacted-cloud downward displacement ceiling.
- `x10`: state-1 delay before `mpLib_80057BC0` removes collision.
- `x12`: state-3 delay before `mpJointListAdd` restores collision.
- `x14`/`x16`: authored guest wait lower/upper bounds described above.
- `x18`: signed guest vertical displacement amplitude multiplied by
  `(HSD_Randf() * 2 - 1)`.

## Verification and native scope

`tests/test_old_yoshi_yakumono.py` compiles the decoder, extracts the exact
anonymous source declaration for offset/type/size assertions, exercises
synthetic finite/bounds/null-reader cases, and runs the decoder against the
owned `GrOy.dat` values. The native stage trace passed in
[old-yoshi-native-v5](../work/full-game/old-yoshi-native-v5.log): two complete
lifetimes, each with 5,000 source scheduler ticks, six map objects in source
creation order, cloud collision collapse/reappearance, guest selection and
teardown. The source match-entry trace passed five costume cycles in
[old-yoshi-match-entry-v1](../work/full-game/old-yoshi-match-entry-v1.log),
but that receipt is entry-only evidence and does not pass the full combat
recipe.

The retained full-combat attempt
[old-yoshi-match-native-v3](../work/full-game/old-yoshi-match-native-v3.log)
still fails its Final Destination laser recipe because the selected fighter
left the ground before the check. Earlier attempts retain the source-base
palette failures in [match native v1](../work/full-game/old-yoshi-match-native-v1.log)
and [match native v2](../work/full-game/old-yoshi-match-native-v2.log).

The generic TIMG-only source-base TLUT correction is covered by the 14 passing
tests in [old-yoshi-palette-tests-v1](../work/full-game/old-yoshi-palette-tests-v1.log).
The source-derived boundary receipt is
[old-yoshi-palette-first-boundary-v1](../work/full-game/old-yoshi-palette-first-boundary-v1.json);
this is a shared material lifetime fix with strict malformed-index rejection,
not a Yoshi-specific asset exemption.

## Native lifecycle fixture boundary

`tests/gameplay_stage_old_yoshi_trace.c` is the source-owned probe for the
integrated stage owner. It observes the six original map objects in
`grOldYoshi_8020E79C` creation order `0,1,4,5,2,3`, verifies each callback
identity, and checks the three map-2 collision joints and their callback
userdata. Its contact stimulus calls the original joint callback with
`CollData.x34_flags.b1234=1` once per source tick while cloud 0 remains in
state 0; the source proc then owns the collapse, collision removal, hidden
state, reappearance, and collision re-add transitions. The fixture also
observes the map-3 guest countdown, source-selected child, and child
visibility, then repeats the complete lifetime and checks archive immutability
and object/yakumono teardown.

The p-link 5 probe preserves that same order: `GObj_Create` selects
`gobj_first_lower_prio`, whose equal-priority scan stops at the existing tail,
so `GObj_PReorder` appends each stage object.

The C probe includes the pinned source headers needed for `Ground`,
`CollData`, `CollJoint`, `HSD_GObj`, and `HSD_JObj` layout. The C++ companion
`tests/gameplay_stage_old_yoshi_trace.cpp` uses the same host stage/runtime
objects as the Fountain and Temple traces and must link the central target's
existing `GameplayWorld`, archive, stage, collision, light, bootstrap, and
native DAT services plus both new trace translation units. The stage owner
provides `melee_web_groldyoshi_exchange_yakumono(void*)`; the fixture only
observes the published `stage_info.yakumono_param` pointer and does not repair
source state.

## Browser discovery

The first [cold/warm discovery](../work/full-game/old-yoshi-browser-discovery-v1/report.json)
reaches original CSS/SSS, Ready/Go and 30 advancing Mario/Mario source frames.
The cold run requires one recorded diagnostic timing resume and retains
1,604.010 ms native / 1,615.710 ms browser maxima, three browser gaps and one
68 ms browser long task. It is a failed performance run. The warm entry needs
no resume and has 8.320/26.760 ms native/browser maxima, zero gaps/long tasks
and no audio underruns. This short entry does not exercise the later cloud or
guest scheduler in the browser.

The reviewed export adds 32 portable descriptors while preserving the
preceding fighter seed. Together the three additions preserve all 750 previous
rows and add 45 pipelines. Native source order, asset bytes and gameplay RNG
are unchanged. The [fresh cold/warm entry pair](../work/full-game/old-yoshi-browser-entry-pair-v1/report.json)
passes the functional route on the corrected seed without diagnostic timing
resumes, browser gaps, native target misses or audio underruns. Cold/warm
native maxima are 14.670/7.480 ms and browser maxima are 31.290/23.320 ms.
The cold run retains one 69 ms browser long task; this remains a functional
entry result, not a stage-performance pass. Both runs unload the original
world. The discovery stall remains retained.

These are bounded native and browser-entry checks. The receipts do not establish full match,
ordinary input, rendered pixels, audio PCM, independent original equivalence,
performance, or acceptance of the stage as a complete vanilla port.
