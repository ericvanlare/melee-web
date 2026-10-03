# Historical GPU stall investigation

A new exact-historical-runtime diagnostic on Chrome for Testing 153.0.8010.36
reproduced a 104.370 ms native callback with 94.815 ms of staging waits. Its
retained Chrome event excerpt identifies browser UI raster pipeline work;
recovered native kernel evidence encloses the entire failure. The original
full Chrome trace/report were lost during analysis, with partial observations
and the recovered kernel recording retained explicitly. The second bounded
slot did not reproduce the stall and failed focus. This is diagnostic evidence,
not a fix or acceptance pass. See the [investigation and recovery limits](../BROWSER_RASTER_STALL.md).
The original failure remains causally unassigned. The project owner approved a
separate [current-runtime holdout gate](../HITCH_CAPTURE.md#approved-current-runtime-scope--2026-09-19)
on September 19, retaining that historical failure and every hard threshold.
Issue #33's approved current-runtime gate now passes. The first campaign failed
on two live pipelines and remains preserved with its final three slots unstarted.
PR #47 adds exactly the two recovered portable descriptors while preserving all
626 previous records. Its 7,347-update development regression matches declared
state and timer with zero live pipelines. Both Release builds, 1,006 tests
(44 optional skips) and full GitHub Verify pass; Verify took 6m 06s.

Two replacement inputs were reserved from header/input-only evidence before
tuning. Independent original reference pairs and browser comparisons match
6,432 Marth/Marth Battlefield and 8,561 Falco/Falco Final Destination declared
state updates and exact timers. All four frozen unprofiled cold/warm runs pass:
29,986 updates/draws, no native target misses, hard gaps, long tasks, audio
underruns/overflows, live pipelines, timing resumes, focus losses or browser
errors. Native/browser maxima are 13.695/29.000 ms on Apple M4, macOS 26.6.2,
Chrome 153.0.8010.50, 640×480 at DPR 2. Preparation takes 149.600–166.200 ms;
Marth retains 66,846,720 bytes of live heap growth and Falco zero. Both inputs
exhaust their frozen cap before original match ending; neither is a complete
original match. These are `per_tick` measurements, without original draw-cadence,
live-controller, pixel or PCM admission. See the
[failed campaign, correction and accepted replacement evidence](../CURRENT_RUNTIME_HOLDOUTS_20260919.md).

PR #47 is merged and deployed at [webmelee.gg](https://webmelee.gg). Both
production origins pass exact HTTP verification and all ten public browser
checks. The public alpha remains deliberately silent; its hosted functional
checks are separate from the audio-enabled development performance gate.
See the [current release receipt](../evidence/public-marth-pipeline-release-v1.json).

The current PR #38 development Release completed two cold/warm rounds on
Fox/Marth Dream Land and Marth/Falco Yoshi's Story: 37,920 source updates/draws,
zero hard hitch failures and one retained native target miss at 18.300 ms
(worst browser interval 30.675 ms). A concurrent audio build interrupted the
initial plan; its contended and incomplete attempts remain recorded, and a
separately frozen six-slot recovery supplied the affected cache pairs and
unfinished coverage. No historical failure was reclassified. See the
[complete timing inventory, memory/preparation evidence and remaining decision](../DEVELOPMENT_TIMING_20260919.md).
