# Dr. Mario and Roy source-port notes

These notes record the GALE01 revision-2 source identities and the local asset
contract used by the focused real-asset gate. Extracted files stay under the
ignored `assets-local/next-gate` directory; only their hashes and decoded
metadata are tracked here.

## Source identity

| Role | Dr. Mario | Roy |
| --- | --- | --- |
| Character kind / fighter kind | `CKIND_DRMARIO` 22 / `FTKIND_DRMARIO` 21 | `CKIND_EMBLEM` 23 / `FTKIND_EMBLEM` 26 |
| Fighter DAT / public root | `PlDr.dat` / `ftDataDrmario` | `PlFe.dat` / `ftDataEmblem` |
| Action rows | 303 | 327 |
| Animation container | `PlDrAJ.dat` | `PlFeAJ.dat` |
| Effects / root / bank / entries | `EfMrData.dat` / `effMarioDataTable` / 1 / 2 | `EfFeData.dat` / `effEmblemDataTable` / 49 / 2 |
| Audio | `drmario.ssm`, `ssm_files` slot 9 | `emblem.ssm`, `ssm_files` slot 31 |

Dr. Mario uses Mario's original special routines and the Mario extension ABI.
The source extension field at offset `0x14` selects `It_Kind_DrMario_Sheet`
(`0x54`). His fighter Article table has only slots 1 and 3: slot 1 is
`It_Kind_DrMario_Vitamin` (`0x31`), and slot 3 is the Sheet article selected by
the extension field. The Vitamin article stores five native 32-bit scalars in
its 20-byte special block and six serialized DAT state rows (96 bytes). The
Sheet stores a four-byte special block and two DAT state rows (32 bytes). The
source initializer's seven `ItemStateTable` entries select animation indices
0 through 5; the DAT table is the six-row representation checked here.

Roy calls the original Marth initializer and uses the exact 0x98-byte
`MarsAttributes` ABI. His Article table is null. The source dynamics header has
three authored parameter chains, no spheres, and six native mode pointers.
The mode rows are `{2,2,2}`, `{0,0,2}`, `{2,0,0}`, `{1,1,1}`, `{2,1,1}`,
and `{3,0,0}`. The earlier five-row interpretation was incorrect: Roy's
authored blend selectors use mode 5 for actions 239–241. The decoder now
derives the required extent from those selectors and validates each relocated
row. Each character's effect DAT has a two-entry table with the
native 20-byte entry stride.

## Costumes and model ownership

The Dr. Mario costume archives are `PlDrNr.dat`, `PlDrRe.dat`, `PlDrBu.dat`,
`PlDrGr.dat`, and `PlDrBk.dat`. Their model roots are respectively
`PlyDrmario5K_Share_joint`, `PlyDrmario5KRe_Share_joint`,
`PlyDrmario5KBu_Share_joint`, `PlyDrmario5KGr_Share_joint`, and
`PlyDrmario5KBk_Share_joint`; each matching `_matanim_joint` material root is
also checked.

Roy's five archives are `PlFeNr.dat`, `PlFeRe.dat`, `PlFeBu.dat`, `PlFeGr.dat`,
and `PlFeYe.dat`. Their model roots are `PlyEmblem5K_Share_joint`,
`PlyEmblem5KRe_Share_joint`, `PlyEmblem5KBu_Share_joint`,
`PlyEmblem5KGr_Share_joint`, and `PlyEmblem5KYe_Share_joint`, with matching
material roots. The C++ trace constructs the source identity for both base DATs,
decodes representative common and special actions, and checks all ten costume
model/material archives. The same `asset_check` parser used by `scripts/check_assets.py` independently
parses each model root; the focused test compiles it once for all ten costumes.

## Focused evidence

`tests/test_clone_fighters_real_assets.py` verifies the owned SHA-256 values,
compiles the portable DAT parser, and runs
`tests/clone_fighters_real_asset_trace.cpp`. The trace checks source kinds,
archive symbols, full action counts, Mario/Mars extension fields, Article slot
ownership and native row extents, all special-action animation/dependency
links, effect roots and counts, audio presence, and every costume's model and
material roots.

The source checks pass locally. The Release `runtime`,
`gameplay_content_match_trace` and `native_menu_host_trace` targets build.
The native mixed match trace passes Dr. Mario/Roy and Roy/Dr. Mario on Final
Destination across all five selected costumes, with source specials, damage,
pause, teardown and reconstruction. The full suite also runs both clone
orientations on Yoshi's Story. Dr. Mario's taunt checks actual Vitamin creation
and cleanup; his side special checks actual Sheet creation. Roy retains the
original Mars state machinery and his own attributes, dynamics and effects.
The existing Mario/Falco original CSS → SSS → match → CSS loop passes twice on
Final Destination and Battlefield after the menu audio additions.

The repository-wide run executed 748 tests (38 optional-fixture/target skips).
It initially had two stale four-fighter prototype expectations and one stale
action-inventory expectation. Those failures remain in the local log; all
corrected tests pass in focused reruns. The new owned clone-asset test passes
separately. The source registry check, disc-manifest validation, native upload
allowlist parity, generated roster, JavaScript syntax and diff checks pass.
There is no claim that the initial full-suite run was green.

Visible Chrome checks use real source PAD samples to select each clone against
Mario through original CSS/SSS on Final Destination, then run a named subset:
jab and grounded specials, plus Dr. Mario's pill taunt. The entire action
inventory, all special variants, other stages, a browser clone-versus-clone
match, and retail comparisons remain pending. Native evidence for the clone
pair is broader than the current browser evidence.

## Retained failures and next capture

The final bounded Release browser sweeps on Apple M4 / Chrome 153.0.8010.36,
1280×1100 viewport, DPR 1 measured the following gameplay maxima. “Cold” clears
application-origin storage; the browser/driver cache is uncontrolled. Every
listed source action reaches its expectation in these four runs.

| Route against Mario on FD | Native max | Browser max | New live pipelines | Audio underrun frames | Result |
| --- | ---: | ---: | ---: | ---: | --- |
| Roy cold | 7.270 ms | 21.135 ms | 1 | 0 | Pipeline coverage fails |
| Roy warm | 9.695 ms | 25.905 ms | 0 | 0 | Bounded sweep passes |
| Dr. Mario cold | 110.535 ms | 25.370 ms | 9 | 0 | Native deadline and pipeline coverage fail |
| Dr. Mario warm | 11.560 ms | 26.875 ms | 0 | 64 | Audio gate fails |

These final sweeps have no automatic timing resumes or browser long tasks.
Roy's Wasm capacity grows by 66,846,720 bytes during each fresh application's
sweep; Dr. Mario's does not grow in this subset. This is capacity, not proof of
a leak or repeated-match stability. Scene preparation/startup are separate
from these gameplay maxima and remain in the local entry logs. Full compact
results, exact build/source hashes and all attempted-run identities are in
[the development evidence](evidence/roy-dr-mario-development-v1.json).

This branch is a development candidate, not a production admission. Initial
browser attempts caught a missing native upload allowlist entry, insufficient
CSS token-confirmation wait, a stale copied action inventory, and fixed-dash
positioning that failed to center Roy. The upload parity test now catches that
manifest mismatch before a build; the action driver now walks using source
position feedback. A test started during a relink fetched incompatible JS/Wasm
and failed to instantiate; subsequent runs use the completed frozen build.
All failed attempts remain local and are summarized in the evidence record.

Cold browser attempts discover pipelines outside the existing 508-member seed.
An early Dr. Mario action sweep reached all six expected actions but measured
405.145 ms native and 158.870 ms browser maxima, nine live pipeline creations,
and one automatic timing resume. An early Roy match-entry attempt paused at
140.58 ms native / 154.65 ms browser. These are retained failures, not accepted
performance or an excuse to change simulation timing. Renderer descriptors
and raw per-draw diagnostics remain local; this pass does not rewrite the
existing certified seed or its bindings.

The next useful donor is a complete **P1 Roy versus P2 Dr. Mario on Final
Destination**, then the reverse orientation if convenient. Use the capture
workflow's supported vanilla four-stock setup and retain its exact timer,
ports, costumes and other recorded settings. Include normal combat and each
special naturally; do not force a giant checklist into one match. The actual
recording determines the comparison scope. Reserve its hashes and compare
source state first, then prepare only newly observed renderer descriptors and
replay the same workload cold and warm. Do not open the existing human holdouts
or change their acceptance state.

The existing normalized donor and MWRC replay path already accepts these
source character IDs; its native admission gate uses the shared content table.
No new transport format or four-fighter allowlist change is needed for this
capture. Existing restrictions on the chosen capture mode still apply.

Clean cold preparation, complete-match timing, retail accuracy, audible output,
other stages/costumes in a browser, remaining Roy special variants and the
historical GPU-stall investigation are open. Production and the other
integration owner's frozen branches are unchanged by this pass.

## First human capture comparison — September 15

The user's four-stock P1 Roy versus level-9 P2 Dr. Mario capture on Final
Destination, default costumes, contains 6,965 source ticks and 6,959 draws.
The original replay reproduces all 37,825 semantic events exactly, including
CPU decisions, RNG, fighter state, camera/HUD observations, result and teardown.
Roy wins with three stocks left. This is a repeatable diagnostic pair; it is
not gold admission or a pixel/PCM comparison.

One visible Chrome replay uses the frozen clone Release build at
`c4695cee35198aec9ab3f966c6c8f8eaeaa7e948`. It consumes the complete input
recipe, with all human samples and PAD history matching, but fails gameplay
equivalence:

| Boundary | Original | Browser |
| --- | --- | --- |
| Match construction complete: RNG | 1439421057 | 2325449750 |
| Tick 154 | Doc's recorded input | First differing fighter input |
| Tick 636 | Doc's position and action | First positional/action divergence |
| Tick 672: Roy damage | 8% | 12% |
| End of the 6,965-tick input stream | Roy wins, stocks 3–0 | Match unfinished, stocks 2–3 |

Both start from RNG seed 1264785038. Using the original HSD generator, the
two construction-complete states correspond to nine versus eight advances.
That comparison identified initialization RNG consumption as the first boundary
to trace. The follow-up below identifies the responsible source call. Do not
force a seed or add an unexplained RNG call to compensate for it.

The state run also retains performance failures: 165.130 ms maximum native
callback, 178.070 ms maximum browser interval, 30 live pipeline creations,
106 audio underrun frames, one instrumented timing resume, and 66,846,720 bytes
of Wasm capacity growth. The worst native callback contains 161.155 ms of
staging-slot wait and creates no pipeline itself. Match preparation reports
198.840 ms separately. These instrumented timings are diagnostic evidence;
no clean cold/warm performance acceptance follows from this run.

The browser harness correctly exits with failure because the source match has
not completed, despite the UI's input-stream completion flag. The strict CPU
sidecar comparator also rejects a repeated draw `source_index` of 10; the
overall comparison remains `invalid_input` while preserving the independent
core-state divergence. No CPU observation was discarded to make it pass.
Raw captures, failed browser output, screenshots and reports remain private;
only the [compact hashes and findings](evidence/roy-dr-mario-retail-diagnostic-v1.json)
are tracked. No gameplay code, renderer catalog, production deployment or
holdout state changed for this comparison.

### Initialization RNG cause

A bounded read-only Dolphin debugger run reproduces the captured entry seed
and records all nine initialization RNG calls. The first eight agree with the
port: four calls from Final Destination setup and two from each fighter's
initialization. The ninth is `HSD_Randi` at `0x80380580`, returning to
`0x801c26b0` in `Ground_801C24F8`, through `Ground_801C28AC` and
`Stage_80225074`. This is the original stage-music selector, called near the end
of `fn_8016E730` after fighter creation.

Final Destination's actual stage row uses selector case 6: when all unlockable
characters are unlocked, it makes a 12-percent alternate-music choice. The
captured save decodes to character mask `0x07ff`. The port starts a fixed music
ID before match construction and never runs that selector. Both the omitted
routine and its save-state dependency matter. The current input-only MWRC
versions do not carry that unlock state, and the menu host restores its unlock
scope when leaving the menu. A general fix must preserve the input profile
through match construction and use the actual selected stream; assuming every
older capture has the new unlocked save would be incorrect.

An ignored linker-wrapper experiment calls the original selector after the two
captured fighters are constructed, with the character mask decoded from the
hash-verified prepared save. It uses the captured route's music flags, never an
expected RNG value or an extra unexplained random call. The source chooses music
ID 78, the primary track, while consuming the missing random choice. Both
construction-complete RNG `1439421057` and first-tick RNG `141382586` now match.

The next bounded experiment requests a 700-tick prefix. Initialization and all
436 completed ticks (indices 0–435) match exactly for RNG, match frame, PAD
history and every recorded fighter field, including the input difference that
previously appeared at tick 154. The process then exits 1 with a SAFE_HEAP memory
fault during tick 436, in `ftCo_8009E7B4` from the attack animation transition.
That failure is retained. This headless experiment excludes GPU draws, CPU
sidecar comparison, pixels and PCM; no browser or full-match pass is claimed.
The first debugger attempt missed breakpoints because JIT debugging was off;
the second omitted Randi's inlined RNG calls; both attempts remain preserved.

The compact [cause and proof record](evidence/roy-dr-mario-rng-cause-v1.json)
pins the scripts and diagnostic artifacts. The experiment is not a shipped
route-specific workaround: production/runtime code is unchanged. General music
selection and save-profile ownership, the animation fault, and the previously
recorded browser/CPU/performance failures remain open.

## Integrated replay repair

The original stage-music selector now runs at its original match-entry boundary,
using save unlock masks retained from live CSS/SSS or bound from the exact capture
fixture in MWRC v4. Selected HPS data is loaded before the original audio request;
alternate tracks are available and the save profile is restored on teardown,
including failed construction. The source selector still executes in silent
builds because it consumes gameplay RNG. Dream Land's primary music metadata
also now names the original `old_kb.hps`.

Roy's tick-436 crash was a decoder error: his motion blend selectors reference
dynamics mode 5, but the native allocation contained only five rows. Deriving
the extent from all authored selectors retains Roy's sixth row while keeping
Marth's five-row table bounded. No original animation or input was changed.

The corrected 700-tick native prefix is exact for all core fields. A complete
visible browser replay then matches **all 6,965 core-state ticks**, CPU decisions,
HUD, magnifier and match state. Roy wins by elimination with stocks **3–0**, as
in both original runs. The Release build and 754 unit tests (37 skips) pass.
The [integrated evidence](evidence/roy-dr-mario-integrated-replay-v1.json) binds
the unchanged HTTP artifacts, save profile, checks and retained failures.

This is still an expanded-comparison failure: preparation source draws alter
camera clip planes before tick zero, bone/camera float bits differ, and the
original batches six pairs of controller ticks with one draw each while the
port draws every tick. The state-capture timing also retains a 206.940 ms native
callback and 218.770 ms browser gap, 23 live pipeline creations and 128 underrun
frames. The historical GPU stall remains open. No pixels, PCM, production,
other-route or human-holdout acceptance is claimed.

## Owned revision-2 asset hashes

```text
dbd729b1e038a6da6a90a732416aca164f5e55dc2ec7024555a6eaa0a926bb8c  PlDr.dat
f718e88d7d1188d4e55ce66111788d7db6c6fcaeaf2db7c728e2cca5aa0931b3  PlDrAJ.dat
c1be714a4f9c4a5ef24770f427fc13d9b9d315e6ef7eb71a13b9d78d67d7f672  PlDrNr.dat
a791283f1e5746f24376aefaaaa0292070eda7e255ae0863c87175cc1f6a6acb  PlDrRe.dat
0a35e13100f8f2fb2d9cc519501800a5bce069b9e7fb15422d48197dcc1d7b1f  PlDrBu.dat
4cc8279000b4b50aad68685a9c9264deb8d449972dd7844a273d841a5f48b088  PlDrGr.dat
8d8132347a5651158aaa425c1f01f13e41b23fc845002cce3040e7e99d837139  PlDrBk.dat
39ccac18137b108b31bb4d832475ba60dc45e381dce9005e14116848e465c669  EfMrData.dat
1703530fb3bec04fec6b3ac7e19a28ff84340c92e37e3b20df6b8876cd315890  drmario.ssm
5d8ec1eb2821e8700ee3ed8020d1b57ca5f857468dad3eddfc28bbc398169648  PlFe.dat
8c235de3cd367e91c4db74e902f7b064dc8f1a2e34bb8515006c19f8e13fbd99  PlFeAJ.dat
0e861f97db059e2672b7102601ff66e55430a1fe24a530d1a6f4630f5af642a7  PlFeNr.dat
e7a7e080b2cb3bdadd6bb59ba7faf58c5798d2f34a5b0e426b7b61f30a2daf36  PlFeRe.dat
3068ac9502f0767be1b83c040d63d42249a877402f75dbdca52e6e746396c401  PlFeBu.dat
216d540811aa49f293826b669b90385d75d8cbce09d7296c5c6194b99a85525e  PlFeGr.dat
520f1bf9c759eb459fe840287d9fd7b4451cec0e88c2f4861ddcb6059d831b9a  PlFeYe.dat
6707f90d06afc1b3107efdb66d8d90a797d3081fbfb5385aab095d8f66cb12d2  EfFeData.dat
0c2a406286870c414cc66cf60b1501c08d41dd22e240e589cdc64cbd042300f1  emblem.ssm
```

## Source-free match preparation follow-up

The second complete visible replay retains exact core state, CPU decisions, HUD,
magnifier and match result through all 6,965 ticks. Non-selective development
rendering now resolves first-use pipelines before encoding the corresponding
draw, using Aurora's existing blocking path. Its match preparation drains renderer
work and waits for submitted GPU work without invoking game camera callbacks.
Menu priming and the selective public policy retain their current behavior.

This removes all nine unmatched preparation draws and restores the original
entry clip planes. The first remaining subject difference is Roy bone Z at tick
64, and camera interest X first differs at 427. The original's six two-tick
batches still differ from the browser's draw-after-each-tick schedule.

This instrumented run has no timing resumes, audio underruns, queued pipelines
or callback overruns: native maximum 15.700 ms, browser gap 24.135 ms. It still
constructs 27 live pipelines and grows Wasm capacity by 66,846,720 bytes. This
single state capture is not cold/warm performance admission and does not close
the retained GPU stall. See the [separate immutable evidence](evidence/roy-dr-mario-render-preparation-replay-v1.json).

## Original joint-transform rounding

Four `HSD_MtxSRT` endpoints used separately rounded C multiply/add while retail
uses `fmsubs`/`fmadds` at 8037A388, 8037A38C, 8037A3B4 and 8037A3B8. Explicit
`fmaf` restores those boundaries; inner products and outer scale operations
remain separately rounded. At source tick 64, local parent m02 feeds world m22
through the rotated grandparent, then Roy's subject Z. The isolated test compares
24 original parent/child matrix words and retains an unfused failing control.
The [producer evidence](evidence/roy-dr-mario-srt-rounding-cause-v1.json) also
records the unsuccessful trig probes and corrected world/local interpretation.

The third complete replay matches **all subject/bone fields across all 6,965
ticks**, in addition to core, CPU decisions, HUD, magnifier and result. Camera
interest X still first differs at source tick 427, and the six original PAD
batches still have fewer draws. A GPU staging wait recurred (101.160 ms native,
113.650 ms browser, 22 audio underrun frames), so performance remains open.
See [full replay evidence](evidence/roy-dr-mario-srt-replay-v1.json).

## Full declared-state match after vector arithmetic repair

The camera's `lbVector_Rotate` uses a quintic sine/cosine approximation whose two
polynomial endpoints and six axis-rotation endpoints are fused in retail. The
port now preserves those exact instruction boundaries. The original normalization
sums are unfused and remain unchanged. A cheap source-kernel test retains the
unfused failing control; the 700-tick native diagnostic then matches camera
position, interest and FOV while retaining exact core state.

The fourth complete visible replay matches **every declared state domain across
all 6,965 ticks and all 6,959 comparable original draws**: core input/fighter/RNG/
PAD state, CPU decisions, camera, subject/bone transforms, HUD, magnifier and match
result. Roy wins with three stocks. It has no extra preparation draws, pauses or
audio queue failures; measured maxima are 16.020 ms native and 24.840 ms browser.
There are still 27 first-use pipeline constructions and 66,846,720 bytes of Wasm
capacity growth. The preceding GPU-stall failure remains preserved, so these
figures do not establish cold/warm performance admission.

The strict comparator remains red on the six extra source draws. Original
`gm_801A4D34` processes every queued PAD sample before drawing once. The periodic
grouping is consistent with nominal 60 Hz PAD timing versus NTSC 59.94 Hz VI
timing, and the independent original replay repeats the same grouping exactly.
The current recipe does not carry a shared emulated VI/PAD alarm epoch: MWRI
timestamps SI status requests; raw observer timestamps use host steady_clock,
and captured `source_vi_count` is zero. Reconstructing the exact phase requires
that missing timing input. No source-index skip list or guessed phase was added.
Drawing only once per browser callback is also insufficient: browser callbacks
are not original VI boundaries and previously exposed magnifier gameplay errors.

The bounded debugger probes did not establish additional valid replay prefixes:
their strict input poll timing was rejected. The saved instruction reads and
original matrix operands are diagnostic evidence only; the independent complete
Dolphin replay and the new full browser comparisons establish the timeline claims.
Raw diagnostics and all failed attempts stay outside tracked source/build output.
See [the final state-comparison evidence](evidence/roy-dr-mario-vector-replay-v1.json).

For subsequent character work, this pass establishes a short iteration order:
verify authored table extents and shared ownership, reproduce the first mismatch
in a small scalar or native prefix, then run one complete visible replay after
the narrow checks pass. Original save-profile context is part of initialization,
and original fused arithmetic must be checked at shared math boundaries. Exact
VI/PAD phase capture and the existing GPU staging stall remain separate open
work; human holdouts remain unopened.

Final combined verification: Release browser/native builds pass, as does
`python3 -m unittest discover -s tests -v` (756 tests, 36 skips, no failures).

## Captured original clock phase and strict draw match

A passive isolated Dolphin probe now preserves complete strict input replay and
all 37,822 typed semantic boundaries while observing the shared emulated PAD/VI
clock. The opening four source ticks establish the supported NTSC two-XFB context.
An initial-only periodic queue model predicts all six later two-update batches;
a wrong-phase control fails and ±11-cycle quantization controls retain the result.
MWRC v5 binds these compact initialization values to immutable input history.
There is no source-index skip list or expected-state payload in the recipe.

Replay batching can span browser callbacks, preserving original update order and
one traversal per original batch. The complete visible browser replay now passes
the strict comparator: 6,965 state updates and 6,959 draws, all declared domains
matching, Roy winning with three stocks. Maxima are 16.050 ms native and 26.240 ms
browser, with zero preparation pauses, overruns or audio underruns. The historical
GPU stall, live first-use pipelines and heap growth remain open; this one
instrumented state run does not establish cold/warm performance admission.

Release runtime/native builds and the 757-test suite (36 skips) pass. Subsequent
focused binding, decoder and 19 browser-report tests also pass. Raw clock rows
stay outside Git/build output. The passive probe patch is optional diagnostic
source, not installed into the capture app. Live controller phase, other startup
histories/VI modes, pixels, PCM and both unopened holdouts require separate evidence.
See [clock format](RETAIL_DRAW_CLOCK.md) and [immutable evidence](evidence/roy-dr-mario-clock-replay-v1.json).

## Independent Yoshi’s clock rejection

The fresh physical Doc/CPU Roy Yoshi’s four-stock recording repeats naturally in
the isolated passive Dolphin probe: strict MWRI is complete/valid and all 58,668
typed semantic events match. It contains 11,077 updates and 11,067 draws.
The opening-only model matches seven early batches but misplaces the next:
original 8527/8528 versus predicted 8534/8535. Subsequent batches and final counts
agree. All ±11-cycle controls preserve this failure.

At source 8527 the original queue-count return observes a poll offset of 69,564
CPU block-clock ticks from the opening-anchored periodic schedule; adjacent normal
checks have offset 154. It sees two queued samples. The exact producer of the
late check is not yet established. Initial phase alone is therefore insufficient
for this route. No recorded batch list or fitted timing was put into runtime.
The full browser run was deferred to avoid spending CPU on a known cadence failure.
This supersedes any implication that the first route established general clock
correctness; its exact comparison remains valid. See [rejection evidence](evidence/doc-roy-yoshis-clock-validation-v1.json).
