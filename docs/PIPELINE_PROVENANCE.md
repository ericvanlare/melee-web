# Private pipeline-use provenance

This private recorder's native fixtures, synthetic generator join and Wasm
build pass. One frozen ordinary-key Mario/Mario Final Destination route now
retains 3,555,646 records through original CSS, SSS, four stock losses, ending
and the return to original CSS, with zero recorder errors, drops or transport
failures. Its generator certifies a conservative 137-descriptor union against
the 508-row reviewed seed. Earlier recorder and transport overflows remain
failed evidence. The instrumented capture fails timing admission. Coverage is
finite and route-specific; the runtime still imports the complete seed, and
broader content and shipping performance admission remain open.

The private option records demand at Aurora's normal pipeline finder before
last-reference, ready and pending returns, at GX draw-cache reuse, before merged
draws return, and before ordinary draw packets are emitted. Background imports
have their own event kind and cannot contribute demand. Descriptors are hashed
from the exact configuration bytes; exports contain typed references, versions,
lengths and digests, never configuration or game bytes. A private memo reuses
digests only for the same type/reference/version and exact descriptor bytes.
Its entry count and aggregate 16 MiB byte storage are bounded; capacity failure
invalidates the capture. Capture begin clears it.

Source context is copied from original CSS/SSS match data and the owned match
session. It includes numeric character/fighter, costume/subcolor, stage/ground,
HUD selection, registered fighter effect banks, action and stock observations,
source tick, route epoch, and world generation. Original HSD GObj render dispatch
adds classifier, GX link and pass. A classifier identifies a dispatch family;
it does not prove which fighter, material or effect bank owns a particular draw.
Those owner identities remain explicitly unknown. Initial groups must therefore
be conservative composites of the observed route and phase.

A side table copies context alongside FIFO stream offsets without adding command
bytes or drains. Ordinary display-list consumption inherits its call-site
context. Unsupported raw nested GX display-list opcodes invalidate private
coverage instead of allowing omitted draws to certify a group. GX
draw packets and frame operations also carry copied context through deferred
execution. The recorder validates capture/device/renderer generations and source
scope pairing. Bounded overflow and missing, stale or unpaired metadata invalidate
the capture; partial evidence and counters remain available for inspection.

## Private build and collection

The build and capture commands below require a quiet validation window when
another task is collecting original-game references.

```sh
python3 scripts/bootstrap.py
python3 scripts/build.py --target runtime --configuration Release --pipeline-provenance
```

This uses `build/browser-provenance-release`, separately from normal development
and public output. The CLI rejects provenance for any target other than the
private runtime. CMake also rejects combining the private recorder with
`MELEE_WEB_PUBLIC_RUNTIME`. The source declarations and hooks are excluded when
the private option is off; the public export allowlist is unchanged.

Before loading the private native module, the local collector must set
`window.meleePipelineProvenanceBinding` to a numeric `caseId` and the 64-digit
lowercase `inputSha256` of the frozen input case. It must install
`window.meleePipelineProvenanceChunk` to preserve each JSON chunk in order,
including invalid chunks. The binding is applied before Aurora startup.
A missing binding invalidates collection. The private command
`_melee_web_provenance_set_case` can switch bindings only between source
commands with no active source or execution scope.

The collector must retain the complete stream from startup through ending,
teardown and return. Drain transport chunks do not independently certify a
capture. After the original work is quiescent, call the private finish command
and preserve its status and remaining records. Any native error, collector
failure, incomplete drain, sequence gap or invalid status remains a failed
attempt. Do not retry into the same evidence file or suppress prior failures.

The deterministic generator and its strict input contract are documented in
[PIPELINE_REQUIREMENTS_SCHEMA.md](PIPELINE_REQUIREMENTS_SCHEMA.md). Bind the seed,
source and complete dirty overlay, pinned dependencies, renderer configuration
layout, source registry, input recipe and coverage inventory. A valid record of
a phase does not establish unobserved actions, costumes or lifecycle coverage.
A source world that has not been constructed has generation zero; do not invent
an identity to certify that preparation scope.

## Remaining validation

The C++ recorder fixture covers deferred consumption, thread separation,
import isolation, overflow preservation and stale-token rejection. Its first
execution exposed a missing execution-token validity assignment; the failure
was retained and the corrected fixture passes. The affected Wasm target still
needs a complete original CSS/SSS/match/lifecycle capture and generator join.
The optional integration test accepts
`MELEE_WEB_PIPELINE_PROVENANCE_FIXTURE` pointing to the compiled C++ fixture;
it joins actual recorder JSON to an independent synthetic SQLite seed. Neither
static Python checks nor a synthetic fixture establishes that the Aurora hooks are reached by source draws.

Start with ordinary non-holdout Mario/Final Destination through original CSS,
SSS, Entry/Ready, actions, HUD/effects, death/respawn, ending, teardown and return,
with repetitions and costume coverage declared before capture. Extend the same
coverage process across the four supported characters and four stages. Keep
incomplete groups explicit. Do not add selective runtime loading until a
materially smaller, coverage-complete conservative set has been demonstrated.
The historical GPU stall and both untouched human holdouts remain open.
