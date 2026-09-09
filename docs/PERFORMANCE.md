# Browser performance work

Performance evidence is kept separate from gameplay-accuracy evidence. A fast
run does not prove source equivalence, and a source lifecycle trace does not
prove that the browser can present every frame on time.

## Scene-entry cost model

The runtime reports these costs independently:

1. Source-owner construction hydrates archives, creates the mutable HSD/source
   objects for the scene, and enters the recovered lifecycle.
2. Render preparation draws the unchanged new scene with its source clock
   stopped. This discovers uploads and render-pipeline descriptors and waits
   for Aurora's queue to drain through two quiet callbacks.
3. Active callbacks report input, simulation/audio, begin, draw, and end time.
   Browser callback interval is reported separately because it is not CPU/GPU
   execution time.

Do not retain an entered `GameplayMenuWorld` across CSS/SSS transitions.
Recovered menu descriptors contain mutable cursor and animation state. A reuse
experiment changed the second source entry and failed the owned lifecycle
trace. The browser instead caches each decoded archive by name and external
resolution policy for the lifetime of one disc import. Menu and match entry
still instantiate fresh mutable source-owned objects. Cache hits verify the
original backing bytes and teardown verifies every shared archive against its
immutable baseline.

Aurora reports texture-upload bytes after the upload has completed. Live scenes
therefore do not pause in response to that statistic; such a pause delayed the
next source tick without hiding any work. New scenes still prime unchanged and
wait for uploads plus pipeline creation to settle. A live scene pauses only
while an asynchronous pipeline is actually queued.

## Pipeline seed and persistence

`web/initial_pipeline_cache.db.gz.b64` is a reviewed Aurora cache seed captured
from the Release browser runtime after the original CSS, SSS, Yoshi's Story,
Battlefield, and stock-loss/respawn paths had rendered. It contains one shader
record and 206 pipeline descriptors. It contains no textures, models, audio,
or other disc bytes. `scripts/materialize_pipeline_cache.py` verifies its SHA-256 digest and
materializes it for Emscripten's `/initial_pipeline_cache.db` preload.

Aurora merges that seed into the origin's optional IDBFS cache. Each completed
scene preparation schedules a coalesced IDBFS save outside the render callback.
This preserves newly discovered pipelines even when later gameplay aborts, so
a crash after a cold stage load does not force the same compilation work on the
next run.

The seed must be regenerated when Aurora's cache schema or pipeline descriptor
version changes. Its test checks the schema, digest, byte size, row inventory,
and descriptor payload size.

## Current measurements and open work

On the project Chrome profile with the Release build, clearing the origin cache
and loading the first 30-row seed changed first CSS entry from 357.48 ms of
pipeline settling to 22.31 ms total preparation, with 1.69 ms construction and
zero queued or newly created pipelines on the first draw. The then-current
121-row seed was captured from the CSS/SSS/Yoshi's Story route.

The seed-capture Yoshi's Story run exposed a separate cost: match preparation
took 933.09 ms, including 545.17 ms of source-owner construction, and a later
active frame spent 146.47 ms in draw across 251 draw calls. That frame created
no new pipeline and uploaded no texture. The expanded seed includes the 26
descriptors discovered after the first visible frame.

On that seed's next cleared-origin run, Aurora merged all 122 seed rows. CSS and SSS
reported no new pipeline work. Yoshi's Story preparation took 808.41 ms:
194.81 ms of construction and 613.60 ms of priming/scheduling, with all 122
pipelines already created and no queued pipeline at construction. The slowest
active callback was 16.89 ms and the 146.47 ms draw spike did not recur. The
remaining cold delay is therefore in source-owner construction and unchanged
scene priming, not missing pipeline descriptors in that measured run.

The archive-cache and 207-row seed run used a cleared origin and the Release
build. CSS, SSS, and Battlefield each reported zero queued and zero created
pipelines. Battlefield preparation took 322.78 ms: 263.75 ms of construction
and 59.02 ms of scheduling and priming. Its first draw took 5.70 ms. The full
four-stock diagnostic completed 1,916 source ticks, all three respawns, and the
return to CSS. No native active callback exceeded 33.3 ms; the worst was 25.97
ms. Computer-control waits did starve seven browser callbacks and caused one
timing pause, so those browser callback intervals are not runtime acceptance
evidence.

For every admitted character/stage pair, record both a cleared-origin run and a
second application load on named browser/hardware. The acceptance run must
include CSS, SSS, match entry, ordinary gameplay, a stock loss/respawn, and
return to CSS. Record preparations, active callbacks over 33.3 ms, audio
underruns, pipeline creation, uploads, and any automatic timing pause.

The deterministic retail-versus-Wasm state comparison described in
[issue 1](https://github.com/ericvanlare/melee-web/issues/1) is the accuracy
guard for deeper construction changes. The coverage and cold/warm acceptance
split described in
[issue 3](https://github.com/ericvanlare/melee-web/issues/3) should remain the
reporting format as more stages and fighters are admitted.
