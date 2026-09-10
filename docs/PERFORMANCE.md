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

Do not retain mutable CSS/SSS scene descriptors across transitions. Recovered
menu descriptors contain cursor and animation state; a reuse experiment changed
the second source entry and failed the owned lifecycle trace. The browser keeps
the menu service owner and original audio engine alive, runs the source OnExit,
destroys the old HSD world and descriptors, and hydrates a fresh next scene.
Decoded archives and SSM banks are cached for one disc import. Cache hits verify
the original backing bytes and teardown verifies every shared archive against
its immutable baseline.

HPS files are validated and indexed when their owner is created, while each of
the source stream's three transfer slots decodes its own ADPCM payload only when
the original loader requests that slot. This removed full-track PCM decoding
from match construction without changing the decoded samples. Match construction
is divided at ownership boundaries: world core, each fighter kind, effects,
items, HUD/audio, match/stage setup, fighter intro, and render/HUD/flow. Source
simulation and rendering remain stopped until every phase completes.

Menu audio uses one continuous 60 Hz producer clock across CSS and SSS. It keeps
the same audio generation, HPS progress, fractional clock phase, and AudioWorklet
queue while the visual scene is rebuilt. Visual teardown and hydration run in
separate callbacks. A match transition still acknowledges a paused worklet and
starts a new stage-audio owner.

Aurora reports texture-upload bytes after the upload has completed. Live scenes
therefore do not pause in response to that statistic; such a pause delayed the
next source tick without hiding any work. New scenes still prime unchanged and
wait for uploads plus pipeline creation to settle. A live scene pauses only
while an asynchronous pipeline is actually queued.

## Pipeline seed and persistence

`web/initial_pipeline_cache.db.gz.b64` is a reviewed Aurora cache seed captured
from the Release browser runtime after the original CSS, SSS, Yoshi's Story,
Battlefield, stock-loss/respawn, and Fox first-use paths had rendered. It
contains one shader record and 283 pipeline descriptors. It contains no
textures, models, audio, or other disc bytes.
`scripts/materialize_pipeline_cache.py` verifies its SHA-256 digest and
materializes it for Emscripten's `/initial_pipeline_cache.db` preload.

Aurora merges that seed into the origin's optional IDBFS cache. Pipeline
discovery marks the database dirty. The browser flushes it only after native
scene ownership has been torn down by **Unload** or application reload. An
earlier implementation scheduled `FS.syncfs` with a one-second idle timeout
after every settled scene. A captured Fox/Yoshi's Story entry showed the prior
SSS save still active at the match's first draw, and later first-use paths
scheduled more saves during gameplay. IDBFS serialization shares the browser
thread with input and frame submission, so it is excluded from live play.

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

The retained-audio/staged-construction run used the 207-row seed and a live
32 kHz AudioWorklet. CSS-to-SSS and SSS-to-CSS both completed with the same HPS
owner and zero audio underruns. The reverse menu preparation took 65.7 ms wall
time; its mutable teardown and hydration were 0.9 ms and 17.4 ms, and no active
browser or native callback exceeded 33.3 ms. A following Mario-versus-Mario
Battlefield entry took 206.5 ms wall time and 104.7 ms summed construction CPU
across eight callbacks, down from 263.8 ms in the earlier archive-cache run.
Eight seconds of gameplay reported a 22.8 ms worst browser interval, 15.1 ms
worst active native callback, no interval over 33.3 ms, zero audio underruns,
and no automatic timing pause.

The 283-pipeline seed was captured from the same Release browser profile after
Fox/Yoshi's Story entry, intro rendering, stock paths and a complete vertical
Fire Fox. The source database contained one shader and 283 unique pipeline
descriptors. This broadens first-use coverage; it does not prove that all Fox
motions, effects, costumes, opponents or stages are covered.

Browser interval accounting now records the callback that detects a timing
stall using the running state at callback entry. Previously that callback set
the native player to paused before the page sampled the interval, so the actual
stall could disappear from the displayed maximum. The page also records browser
long tasks separately from native simulation and render callback phases.

## Browser admission matrix

Every admitted character/stage pair needs a cleared-origin run and a second
application load on a named machine, browser/version, OS, resolution and power
configuration. Run the Release build through the original CSS and SSS; a direct
fixture entry does not cover player transitions. Preserve raw PAD inputs and
the source frame/motion at every reported hitch.

The versioned action inventory for each fighter is:

1. match intro and the first controllable frame;
2. walk, dash, crouch, jump, double jump, fast fall and at least ten repeated
   wavedash inputs in both directions;
3. shield, roll, spot dodge, air dodge, ledge interaction and recovery;
4. every jab/tilt/smash/aerial/grab/throw and every grounded and aerial special
   family, including charged/held and directional variants that select distinct
   source actions;
5. every fighter article and effect, hitting and being hit, shield contact,
   KO/stock loss, respawn and invincibility;
6. pause/resume, match exit, return to CSS and a second match without reloading.

Add stage-specific articles, effects, animated or moving collision, background
state transitions and alternate source paths to the same inventory. Record the
exact unexercised rows; content cannot be admitted on metadata, scene-entry or
one-action evidence.

The current browser gate is zero automatic timing pauses, zero active callback
intervals over 33.3 ms, zero browser long tasks over 33.3 ms, zero audio
underruns, and zero pipelines queued or created during live actions. Record
preparation wall time, native phase timings, first-use draws, texture uploads,
heap growth and the first failing source frame/motion. These numbers establish
performance only for the recorded configuration; retail state/render/audio
equivalence remains a separate gate under [ACCURACY_CONTRACT.md](ACCURACY_CONTRACT.md).

No optional persistence, archive decode, resource destruction, cache/database
serialization or pipeline compilation may run on the interactive callback
path. New immutable renderer resources can be prepared while the source clock
is visibly stopped at a scene boundary. Do not run hidden simulation, consume
RNG, synthesize input or skip a source effect to warm a cache. Add pipeline
descriptors discovered by the visible matrix to the reviewed seed, then repeat
the cleared-origin and warm runs before admitting the content.

The deterministic retail-versus-Wasm state comparison described in
[issue 1](https://github.com/ericvanlare/melee-web/issues/1) is the accuracy
guard for deeper construction changes. The coverage and cold/warm acceptance
split described in
[issue 3](https://github.com/ericvanlare/melee-web/issues/3) should remain the
reporting format as more stages and fighters are admitted.
