# Recorded-session state comparison

The default whole-session comparison covers the three-match, four-Mario CPU9
Final Destination MWRC v8 regression and the three-match, twelve-distinct-
character CPU9 MWRC v9 milestone. Both run through original character select,
stage select, gameplay, Results and the intended final character-select
return. Explicit MWRC v10 first-setup/tick-0 and first-positive-match-frame
prefix scopes are documented below; neither admits v10 to whole-session
comparison.
[STATUS](../STATUS.md) indexes the observed results and their receipts.

The MWRC v8 input contains source-consumed controller samples and their scene
spans. The browser uses its existing per-tick traversal policy. This is separate
from MWRC v6's recorded queue batches and does not establish original draw
cadence. Match setup and every match tick retain the exact comparison fields:
RNG, match frame, semantic PAD bytes and each complete declared Fighter record.
Float values represented as hexadecimal bits receive no tolerance.

## MWRC v9 setup and entity coverage

MWRC v8 remains the four-Mario regression. MWRC v9 adds a three-entry table of
the original `StartMeleeData`, one independently captured and checked setup for
each match. The browser receives the constructed setup at each match entry; a
recipe with fewer or more entries, a setup inconsistent with its source setup
boundary, or a setup outside the predeclared milestone is rejected before match
construction. The v8 one-setup wire format remains unchanged.

The predeclared v9 roster is Mario/Fox/Falco/Marth, Dr. Mario/Roy/Link/Young
Link, then Captain Falcon/Ganondorf/Luigi/Pikachu. Each row uses costume indices
0–3, four CPU-kind-4 players at level 9, four stocks, Final Destination, rumble
off, and the exact stock-match rule bytes copied from the accepted v8 Mario
setup. This records twelve selected character kinds; forms, transformations,
and secondary entities do not increase that count. The native decoder enforces
this profile as well as the producer, and the original menu's typed
`StartMeleeData` is compared field by field with the corresponding setup before
match construction and again at match entry.

For this roster, each slot must retain one primary StaticPlayer.player_entity
GObj whose user_data points to that slot's Fighter head, whose Fighter gobj
backlink points to the same GObj, and whose Fighter player_id equals the slot.
The source and browser records carry the ordered slot/entity identity pairs,
checked backlink status, primary user-data link, and Fighter slot identity.
Generation starts at zero and increments if the primary GObj is
replaced; the comparator checks every recorded generation against the browser.
A secondary entity, detached link, slot mismatch, missing or extra row,
reordered slot, or malformed identity is an explicit coverage failure. This
milestone does not
claim support for comparing a roster that creates secondary or transformed
entities; such a roster needs those entities' source identities, lifetimes and
state fields captured and compared before it can be admitted.

The v9 envelope and its rejection controls alone are implementation checks.
The [12-character receipt](evidence/recorded-session-12-distinct-v1.json)
binds the original v9 capture, its independently identified match setups, the
fresh browser run, and the strict comparison. The claim covers exact consumed
input and scene order, all three setup boundaries, declared Fighter fields,
primary fighter-entity identities and lifetimes, and the final CSS endpoint.
The v8 recording remains a separate four-Mario regression and is never
relabelled as the v9 source run. The v9 MWRO is one original Dolphin
execution; repeatability across independent source captures was not evaluated.

Keep second-match CPU observations off for comparator input. They are focused
diagnostics and make the browser trace an unsupported observation mode. The
capture command enables them only with `--cpu-observations`; that option is
restricted to v9 second-match investigations.

## Cape reaction and packed motion metadata

The merged runtime's first new state disagreement occurred during the second
match, before the eventual early Results transition. Mario's Cape hit a fighter
in Catch. The hit damage, RNG, PAD, match frame and position agreed, but the port
kept the victim grounded in Catch while the original entered DamageN3.

`ft_800895E0` copies `MotionState::x4_flags` as a numeric word into `Struct2070`.
The union exposes that word through named bytes and bitfields. Its original
declaration assumed the PowerPC byte and bit order. On little endian, the Catch
word's category was decoded as zero instead of ten. `ftCo_800C3538` therefore
took the Cape-only response. The owned executable loads byte `fp + 0x2071`,
extracts its high nibble and excludes categories nine through eleven from that
response.

The downstream patch preserves the numeric word and exposes its original
field masks on little endian. It applies the same representation to
`UnkPlBonusBits`, which receives that word through the original collision and
bonus-stat code. Both types remain four bytes. Original metadata producers,
category consumers, allocation identities, Fighter leases, register tracking
and lifecycle ordering remain in use. No captured Fighter state or expected
reaction becomes a runtime input.

The focused regression executes the production flag producer and Cape consumer
in Wasm. It covers each bit, named-field writes, the bonus alias, every category,
the explicit Cape override and the producer's Luigi/item attack-ID remaps. The
old declaration remains a failing-category control. An owned-DOL check verifies
the Catch table word and the category-decoding instructions.

```sh
python3 -m unittest discover -s tests -p test_gameplay_motion_flags.py -v
```

Set `MELEE_CPU_DOL` to the owned revision-2 executable to run these focused
instruction checks; without it they report a skip. The input is validated by
the existing pinned-DOL reader. For the full suite, also supply
`MELEE_CPU_DISC`, `MELEE_CPU_SYMBOLS` and `MELEE_CPU_SOURCE_ROOT`: the existing
CPU provenance tests require all four together.

## Camera arithmetic during a screen KO

The screen-KO draw callback writes Fighter position through the inverse HSD
camera matrix. Its SDK math therefore affects the compared simulation state.
The original `C_MTXLookAt` calls paired-single vector normalization and cross
product routines. Aurora's portable version selects different scalar routines;
its inverse fallback also uses a different determinant and reciprocal sequence.

The camera bridge keeps HSD's original up-vector, view-cache and inverse-cache
ownership. Within `cobj.c`, normalization and LookAt use the owned executable's
operation order. Normalization retains the reciprocal-square-root estimate's
extra precision and rounds the multiply's FC operand to 25 significant bits
before refinement, as the original PowerPC instruction does. A source-only
negative control distinguishes this from rounding only the product.
The shared PS inverse preserves cofactor rounding, determinant
order, reciprocal estimation and refinement, translation, aliasing, and the
zero-determinant return. Explicit scalar inverse callers retain their C routine.
The existing source-ordered matrix-vector multiplication remains the final
position writer. No tick, character, scene or captured-value condition selects
these operations.

Focused checks compare synthetic vectors and matrices against independent
instruction-order transcriptions and exercise the original HSD camera consumer.
The owned-DOL checks bind the transcriptions to the original SDK bodies:

```sh
python3 -m unittest discover -s tests -p test_gameplay_ps_inverse.py -v
python3 -m unittest discover -s tests -p test_gameplay_camera_math.py -v
```

## Camera-shake depth scaling

`Camera_8002A0C0` scales the source quake input for viewport and camera depth,
then publishes the translation consumed by `Camera_8002AF68` for both HSD camera
objects. The owned executable uses two `fmadds` instructions to interpolate the
depth factors, followed by separate multiplications. The patch preserves those
rounding boundaries with `fmaf`, retaining the original input resets and update
order. Screen-KO positioning uses the secondary camera; the reference's main
camera snapshot cannot substitute for that owner's transform inputs.

The focused regression executes the production camera function for 512
synthetic combinations of depth, FOV, translation and training scaling,
including the near-zero depth-range branch. It checks published translation,
input reset and retained scale fields, with a split-arithmetic negative control
and an owned-DOL body hash:

```sh
python3 -m unittest discover -s tests -p test_gameplay_camera_shake.py -v
```

## Combo-counter position adjustment

`ftColl_80076528` decrements the original combo counter and, when its existing
ground/victim gates permit it, pushes the fighter along the floor normal. The
owned executable multiplies facing by the selected common-data magnitude,
then uses `fnmsubs` for each position component. Splitting the latter product
and subtraction introduces a second rounding point. The patch uses `-fmaf`
to preserve both fused rounding and the final negation. It retains the source
counter, threshold, gate and field-write order.

The production-function regression covers synthetic near-unit floor normals,
both facing directions, both authored magnitude branches, counter decrement,
airborne/victim exclusions and the old split-arithmetic negative control:

```sh
python3 -m unittest discover -s tests -p test_gameplay_combo_push.py -v
```

## Reproduce the comparison

Use the owned disc, `candidate.mwrc` and `capture.mwro` identified by the
receipt. Keep them outside Git. Build the runtime and gameplay checks from the
recorded source revision, then serve the development build over real HTTP:

```sh
python3 scripts/build.py --target gameplay
python3 scripts/build.py --target runtime
python3 scripts/serve.py --directory build/browser --port 8813
```

In another terminal, with `SESSION_DISC`, `SESSION_REFERENCE`, `SESSION_RECIPE`
and `SESSION_MANIFEST` set to those local inputs, run the full recording. The
output directory must not exist. Configure `MELEE_PLAYWRIGHT_DIR` for the
installed Playwright package; the shared browser tools select installed Chrome.

```sh
node scripts/capture_whole_session_browser.mjs \
  --url http://127.0.0.1:8813/runtime.html \
  --disc "$SESSION_DISC" --recipe "$SESSION_RECIPE" \
  --manifest "$SESSION_MANIFEST" \
  --runtime-data build/browser/gameplay_menu_browser.data \
  --out work/recorded-session-reproduction \
  --phase-timeout 180000 --replay-timeout 1500000 --poll-ms 500
python3 scripts/compare_whole_session_state.py \
  --reference "$SESSION_REFERENCE" --recipe "$SESSION_RECIPE" \
  --port-trace work/recorded-session-reproduction/retail-port.jsonl \
  --browser-report work/recorded-session-reproduction/retail-browser-report.json \
  --out work/recorded-session-reproduction/comparison.json
```

The browser report establishes replay completion and the final CSS endpoint.
The separate comparator binds every consumed input to the original stream and
recipe, checks scene order, and compares each match setup and tick without
searching for a later matching record. Keep both reports and their hashes.
Run the comparator's negative controls with
`python3 -m unittest discover -s tests -p test_whole_session_state_compare.py -v`.

## Bounded MWRC v10 first-setup/tick-0 comparison

The comparator keeps `whole-session` as its default scope and admits only MWRC
v8/v9 there. Use `--scope v10-first-setup-tick0` only with a frozen expectations
packet and all bound source and browser sidecars. `--browser-report` selects the
retail browser report; `--browser-capture-report` selects the separate capture
wrapper report. Both are required for this scope, as are the source manifest,
source capture report, first-setup identity audit, and browser producer
manifest. The command rejects missing or mismatched identities.

```sh
python3 scripts/compare_whole_session_state.py \
  --reference "$SESSION_REFERENCE" --recipe "$SESSION_RECIPE" \
  --port-trace "$SESSION_BROWSER_TRACE" \
  --browser-report "$SESSION_BROWSER_REPORT" \
  --scope v10-first-setup-tick0 \
  --expectations "$SESSION_EXPECTATIONS" \
  --source-manifest "$SESSION_SOURCE_MANIFEST" \
  --source-report "$SESSION_SOURCE_REPORT" \
  --source-audit "$SESSION_SOURCE_AUDIT" \
  --browser-capture-report "$SESSION_BROWSER_CAPTURE_REPORT" \
  --browser-producer-manifest "$SESSION_BROWSER_PRODUCER_MANIFEST" \
  --out "$SESSION_COMPARISON_OUT"
```

The bounded reader caps source input at 32 MiB and 4,200 records, stopping at
the first invalid or divergent boundary, or immediately after the first
match's completed source tick 0 join. CSS/SSS frames establish consumed-input
ordering only: they do not compare menu scalar state, and
`nonmatch_fields_compared` stays empty. The one setup and one match-tick state
use the declared fields and strict primary-entity identity checks. The whole
v10 recipe is still validated before prefix consumption, but later match state
is not compared. The packet's full-MWRO SHA-256 is recorded provenance; the
comparison freshly hashes only bytes consumed through the join and checks
pre/post file stat identity. It does not hash the remaining MWRO bytes.

When this bounded boundary matches, the report records
`boundary_result: equivalent` while keeping `result: incomplete`,
`complete: false`, and `whole_session_equivalent: false`. The CLI exits 1 for
that incomplete prefix; exit 0 remains reserved for complete whole-session
equivalence, and exit 2 reports invalid evidence. The scope does not support a
later-gameplay or whole-session accuracy claim. Run the comparator and CLI
negative controls with:

```sh
python3 -m unittest discover -s tests -p test_whole_session_state_compare.py -v
python3 -m unittest discover -s tests -p test_compare_whole_session_state_cli.py -v
```

## Bounded MWRC v10 first-positive-match-frame comparison

Use `--scope v10-first-positive-match-frame` only with a fresh frozen
expectations packet, the source first-setup identity audit, the separate
source-only first-positive audit, and provenance sidecars for the browser
capture made for this boundary. The earlier tick-0 browser capture does not
bind or authorize this browser trace. Pass the first-positive audit with
`--positive-boundary-audit`; the packet also pins its path and hash, the source
and browser inputs, and the first-positive target (match, source tick and
sequence, preceding PAD-consume sequence, recipe timeline index, browser
cursor, and match-frame value).

The explicit scope validates the entire MWRC v10 recipe, then streams the
source and browser in order from CSS through that target. It requires
contiguous match-0 source ticks, match frame zero before the externally pinned
first positive clock value, and an exact PAD-to-tick, timeline, state, and
source-sequence join at the target. CSS/SSS frames still establish consumed
input order only; the setup and every match tick through the target use the
declared state fields and strict primary-entity checks. The source reader is
capped at 32 MiB and 8,192 records. It stops at the first invalid or divergent
join, the target join, or either cap; it never searches for a later match with
clock zero.

Like the tick-0 scope, a matched first-positive boundary reports
`boundary_result: equivalent` with `result: incomplete`, `complete: false`,
and `whole_session_equivalent: false`. The CLI exits 1 for that bounded result.
It does not establish later gameplay or whole-session accuracy. The default
scope and the existing tick-0 behavior remain unchanged.
The recorded result and exact boundary are documented in the
[B4 first-positive entry](status/2026-10-07-b4-v10-first-positive-match-frame.md).

## Capture command failure gates

The capture command reports success only after the replay completes, browser
callbacks finish, and the required `retail-port.jsonl` and
`retail-browser-report.json` files are exported exactly once with nonempty,
hashed bytes. The state-mode protocol does not require the optional timer
trace. Browser, runtime, export and diagnostic failures retain a failing report
and return nonzero. A deliberate prefix stop reports `incomplete` and also
returns nonzero; it cannot become complete-session evidence.

The explicit `--runtime-data` path binds the Emscripten preload package. Chromium
can report `net::ERR_ABORTED` for this fetch after its complete bytes have been
read into the virtual filesystem. The capture retains that event and clears it
only when exactly one GET/fetch received HTTP 200, the response length, loaded
byte count, virtual-file size and local artifact size agree, and the virtual
file SHA-256 matches the supplied artifact. Missing identity, partial bytes,
duplicates or any other request failure remain fatal.

PR #93's independent review found that the earlier harness could print `pass`
despite recorded browser errors or missing exports. The correction changes
capture failure classification, not runtime behavior, recorded inputs or the
exact comparator. The accepted session already has clean diagnostics and both
required artifacts; its original receipt and reports remain unchanged. The
[follow-up receipt](evidence/recorded-session-harness-finalization-v1.json)
binds the correction, negative controls and preserved evidence separately.

Run the fast gates and the real HTTP/headless Chrome CLI contract:

```sh
python3 -m unittest discover -s tests -p test_whole_session_capture.py -v
node tests/whole_session_capture_browser_test.mjs \
  --out work/session-capture-contract
```

The browser contract uses synthetic transport fixtures and requires the same
Playwright/installed Chrome configuration as the capture command. It retains
each case's report, process exit, page dump and screenshot. Its success is
harness evidence, not a gameplay comparison.

## Evidence scope

Successful comparison is limited to this recording and the declared fields.
Scene and input-span checks do not compare every internal menu field. The
reference lacks full per-tick menu state, and the browser diagnostic does not
record comparable draw state. General game equivalence, pixels, PCM, live-input
accuracy and foreground performance require their separate validation gates.
Local inputs, captures, generated binaries and diagnostic probes stay outside
Git. A receipt must bind the source, build, input, harness, comparator and report
identities before a result is accepted.
