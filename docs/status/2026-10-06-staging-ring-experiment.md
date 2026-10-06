# Opt-in staging ring experiment: byte equality and inconclusive timing

Evidence labels: **Source identified**, **Compiled**, **Browser exercised**.
The [machine-readable receipt](../evidence/staging-ring-experiment-v1.json)
binds the retained reports in owned run
`claude-h1-staging-ring-20261006-102458-eb61e3c0` on external storage.
This experiment does not establish an H1 timing-pause fix. The earlier natural
pause remains unexplained.

The renderer defaults to two frame/staging slots, including mobile and ordinary
requests. Four slots require the explicit loopback desktop development query
`?melee-web-staging-slots=4`. Functional byte hashing and local staging diagnostics
are separately explicit. Source tick/draw order, arithmetic, RNG, timing guards
and resume behavior are preserved. Existing aligned write tails were already
zeroed by the baseline; this change does not introduce that behavior.

## Functional submitted-byte component

Two captures consumed exactly 600 completed inputs, with one captured source draw
per input. Independent capture ordinals are 1–600 and input cursors are 0–599.
The original match counter is retained rather than relabeled: it starts at zero
while authored entry is active and ends at 476. The retained entry snapshots
show active gameplay/HUD by input cursor 239, original counter 116. The fixture
is a synthetic conditioned MWRCv4 component from the tracked four-player setup
and PAD snapshot, with explicit seed/profiles and neutral inputs; it is not an
original-menu or retail identity comparison.

All 2,463 observed Queue.WriteBuffer fingerprints agree in actual role, offset,
length, order and SHA-256 between two and four slots. This proves equality of the
submitted ranges observed by the capture, not pixels or timing. Both native
reports completed 600 inputs/steps/draws with no resumes or structural, focus,
runtime or teardown failures. Their original performance failures remain
retained. Only `browserCallbackGaps`, `livePipelinesCreated` and
`preparationPauses` are explicitly performance-only for this functional
assessment; all other failures still reject completion.

The reusable comparator retains raw report hashes and both harness source
identities, proves their difference is confined to unserved harness files, and
checks every available local/served artifact map and the same Aurora patch.
**Artifact binding is conditional:** the failed original ring-two report lacks
its post-capture map. The comparator rejects that omission by default. The
explicit recovery assessment uses matching later ring-four before/after
31-file maps as subsequent unchanged-build witnesses. It does not assert that
both captures had complete pre/post manifests. The original quick comparison is
unchanged; `byte-comparison-v2.json` is the new reproducible assessment.

## Conditioned timing and natural regression

Four fresh instances requested two/four slots and 35/100 ms host stalls, with
local staging diagnostics enabled uniformly and functional hashes disabled.
The stall boundary is the `menuRuntimeTiming` observer after the original
observer and source submission, before the native callback returns. It is not
an injection between tasks. The exact target is completed-input cursor 600;
the observed original counter was 476. GPU workload windows and actual source
progress are retained.

The two 35 ms treatments and ring-two 100 ms treatment were delivered and did
not produce a timing pause in the bounded post-stall window. Each had post-entry
preparation, including events whose measured duration quantized to zero, so all
are inconclusive. Ring-four 100 ms skipped the exact target to cursor 601 and
correctly refused the stimulus; that cell is non-delivered and inconclusive.
There was no retry to obtain a favorable outcome. These results demonstrate
neither a 35 ms benefit nor the proposed 100 ms pause behavior.

No qualifying timing/fault incident was generated. Existing recorder policy
excludes preparation lifecycle events from persisted incidents; empty incident
records therefore mean no qualifying incident, not a retention failure. Actual
staging incident persistence remains unproven. Producer/retention unit checks
have their separate scope. Completion latency means registration-to-callback
delivery, not pure GPU duration; occupancy is sampled staging occupancy.

A separate headless, silent natural run explicitly observed four slots and
completed original CSS → original SSS → four Mario CPU9, four-stock Final
Destination → original Results → original CSS. The terminal source counter was
12,541, outcome 2, with P3 winning. It had no timing pause, automatic resume,
page/runtime/crash/native-command errors. Results and returned-CSS screenshots
are retained. The earlier interrupted natural prefix is preserved separately.

The natural report retains `source_unchanged=false`: comparison harness edits
occurred during that run. Its retained tracked end diff matches the subsequently
committed unserved harness-only subset; the natural harness and served runtime
were unchanged. A separate assessment retains its 24 actual response identities
and original local WASM identity, with subsequent local verification. This is
not a retroactive 31-file pre/post manifest claim. The natural run used the
updated CSS helper build, while byte/timing evidence retains its earlier frozen
build identities.

## Integration and validation

Preparation exposed a pre-existing scoped import lifecycle defect: ordinary
owner unload cleared the imported-disc mode before v4 replay could request its
bounded Replay scope. The fix separates persistent import mode from active asset
owners. Ordinary unload releases owners while retaining mode; actual disc/import
clearing resets it. Eager imports and whole-session ownership remain covered.

The development CSS observer helpers also allocated only 16 bytes for the native
14-int32 output. They now allocate the authored 56 bytes, preserve the returned
four IDs/eight geometry values and cleanup, and explicitly handle allocation
failure. Focused tests execute the actual helper assignments with full native
write shapes. The natural regression uses these served helpers, without a
replacement observer.

The affected Release runtime built successfully. Focused byte sequence,
completion, comparator, stall-boundary, recorder, CSS storage, import lifecycle
and SHA/compiler checks passed. The first full suite is retained as failed:
1,886 tests, three failures and one error. One historical observer check needed
an exact projection of the reviewed telemetry additions; three native trace
checks required the absent ordinary build graph. The historical fixture remains
unchanged, unrelated callback deltas still reject, and generation/fatal/release
ordering is checked. This is not binary equivalence: disabled diagnostics still
perform an extra atomic source-frame read/callback capture. The corrected full integration suite passed: 1,906 tests in 311.911 seconds,
with 141 skips. Its source revision and log hash are bound in the receipt.
Owned browsers/server and the timing marker are closed; the heavy lane is
released. Incremental builds are retained for review/reuse.

Renderer GPU buffer reservation rises from 132,120,576 to 264,241,152 bytes
(126 MiB additional reservation). This is not measured physical GPU allocation
or residency. Native allocator and JS heap snapshots are retained as separate
lifecycle observations without attributing their differences to the ring size.
Headless component results do not satisfy retail comparison, foreground timing,
physical-controller, uninterrupted audio or tournament acceptance gates.
