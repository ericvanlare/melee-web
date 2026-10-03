# Link and Young Link development candidate

Link and Young Link are enabled through the original CSS/SSS and shared native
runtime, merged in PR #32 at `2ee80ab`. Both retain their original
identities, five costumes, attributes, actions, articles, sword effects and SSM
files. Real-asset lifecycle checks cover both player orientations and all five
costumes, including reconstruction and teardown.

The physical four-stock Link/CPU2 Young Link recording on Yoshi's Story is
**retail compared under its recorded controller-queue schedule**: all 7,070
state updates and 7,064 draw boundaries match. The compared fields cover both
fighters, input/PAD/RNG, CPU decisions, camera, subject bones, HUD, magnifier and
match outcome. A fresh original replay independently matches 38,108 semantic
events. Source fixes address hookshot stack corruption and restore the original
inlined fused arithmetic; comparator fields and tolerances are unchanged.
The final reviewed integration replay again matches all 7,070 updates and
7,064 draws; the PR #32 integration suite passed 956 tests with 44 skips. That Release
state capture records 145.105 ms native / 153.620 ms browser maxima, including
140.830 ms of staging waits, and 342 audio underrun frames. It is not a
performance pass; the earlier port receipt remains separate evidence.

Performance, pixels, PCM, live scheduling and unexercised moves remain open;
Young Link human-controlled retail coverage is not implied by this match.
The PR #32 build was deployed at [webmelee.gg](https://webmelee.gg) as the silent
alpha. Its exact hosted bytes and public browser smoke passed. Both Link/Young
Link orientations also complete a bounded ordinary-keyboard functional smoke
on staging, with cold timing pauses retained under
[#33](https://github.com/ericvanlare/melee-web/issues/33); this does not broaden
character or performance admission. See the
[deployment receipt](../evidence/public-link-release-v1.json),
[port notes](../content/LINK_PORT_NOTES.md)
and [bounded evidence receipt](../evidence/link-young-link-replay-v1.json).

The new [GPU compilation diagnosis](../history/LINK_GPU_STALL.md) correlates a
482.785 ms Link staging wait with synchronous Metal compiler work for four
missing first-draw descriptors. The reviewed seed preserves all existing records
and adds 78 reviewed descriptors. Its full 7,070-update/7,064-draw state replay
is exact with zero live pipeline creation; both Release builds, 957 tests
(44 optional skips), package/HTTP audit and ten public browser checks pass.
The state capture retains a browser gap and audio underruns. Four separate
bounded public segments (both player orders cold/warm, 450 updates each) pass
without timing pauses, live pipelines or native target misses; native/browser
maxima are 11.370/25.245 ms. These silent functional checks do not establish
full-match performance. The historical a822 stall remains unresolved. The first
current-runtime holdout campaign failed; a separately frozen replacement
campaign passes with the two-descriptor correction and fresh inputs recorded
above. PR #38 is merged, and this
fix remains deployed at [webmelee.gg](https://webmelee.gg). Its original production
bytes and ten headed browser checks pass on both the immutable origin and apex;
see the [PR #38 release receipt](../evidence/public-link-pipeline-release-v1.json).
