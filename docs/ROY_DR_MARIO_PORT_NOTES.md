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
three authored parameter chains, no spheres, and five native mode pointers.
The five mode rows are `{2,2,2}`, `{0,0,2}`, `{2,0,0}`, `{1,1,1}`, and
`{2,1,1}`. A following referenced DAT target is adjacent metadata rather than
a sixth mode. Each character's effect DAT has a two-entry table with the
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
This points to initialization RNG consumption as the first boundary to trace;
the responsible source call is not yet identified. Do not force a seed or add
an unexplained RNG call to compensate for it.

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
