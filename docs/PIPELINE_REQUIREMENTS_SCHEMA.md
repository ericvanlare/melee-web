# Private pipeline requirement sidecar

`generate_pipeline_requirements.py` turns a private Aurora use capture into
reviewable requirement metadata. It is an evidence join, not a seed inventory:
the generated sidecar can certify only the source route, phase, actions,
costumes, lifecycle and frozen input cases declared by its coverage manifest.
Finite route captures do not establish requirements for unobserved actions.

The generator is intentionally fail closed. A malformed or contradictory
capture is a hard rejection. A valid capture with a declared phase/action/
costume/lifecycle that was not observed is an `incomplete` draft and has
`certified: false`. Neither result is a usable selective preparation policy.

## Command

```sh
python3 scripts/generate_pipeline_requirements.py \\
  --capture private/pipeline-use.json \\
  --seed /private/initial_pipeline_cache.db \\
  --metadata private/pipeline-binding.json \\
  --coverage private/coverage-manifest.json \\
  --output private/pipeline-requirements.json
```

`--capture` may be one JSON object or JSONL. JSONL has one header object (all
capture fields except `records`) followed by one record object per line. The
event stream is ordered evidence: `sequence_begin`, `sequence_end`, and every
record sequence must be contiguous and in file order. The generator never sorts
the event stream to conceal a gap. Coverage case and generated group output is
sorted by stable identifiers.

Copied identifiers are bounded to 256 characters and must not contain a slash,
backslash or NUL. This applies to case, route, capture, scope and worker
thread IDs, custom phase/scene labels, dependency keys, renderer version/layout
labels, and declared action/lifecycle labels. A path-shaped value is a hard
rejection; the generator does not sanitize it into a certificate.

For a long native run, JSONL may instead contain one complete recorder drain
chunk per line. Each chunk is bounded by the per-object input limit; chunks are
joined in sequence order and the aggregate record cap still applies. Missing
chunks, invalid intermediate status, or a non-final last chunk is rejected.
The native recorder may compact repeated source contexts within each chunk:

```json
{
  "contexts": [
    {"scene": 4, "phase": 4, "world_generation": 2, "route_epoch": 3}
  ],
  "records": [
    {"sequence": 12, "kind": "packet_use", "scope_id": 9,
     "context_index": 0, "type": 1, "pipeline_ref": "0x0000000000000007"}
  ]
}
```

`context_index` is a zero-based integer into that chunk's `contexts` array.
The generator expands and validates every indexed chunk before joining its
records, then consumes the same direct `context` shape as a verbose capture.
Indexes never carry across chunks. A missing table, non-integer or boolean
index, out-of-range index, non-object referenced context, or record carrying
both `context_index` and a direct `context`/`scope`/`source_context` is a hard
rejection. Existing verbose records remain accepted, and unused context-table
entries do not contribute evidence.

The seed is opened read-only. A nonempty `seed.db-wal` is rejected: its visible
rows would not be covered by the base database SHA-256, so certification
requires a materialized SQLite snapshot. An empty WAL or `seed.db-shm` may be
copied to a temporary directory for the read-only open; the original is never
checkpointed or changed. The seed may also be the checked-in gzip/base64
encoding, in which case its decoded SQLite bytes are hashed. The
`seed_decoded_sha256` binding always refers to decoded database bytes, not the
encoded file.

On hard rejection the command writes a small result with
`status.kind: "hard_reject"`, `valid: false`, and machine-readable `errors`,
then exits 2. It never writes a partially usable requirement catalog.

## Capture input

The capture root is:

```json
{
  "schema": "melee-web-pipeline-use-v1",
  "version": 1,
  "capture_id": "css-sss-mario-fd-20260913-a",
  "capture_generation": 4,
  "sequence_begin": 1,
  "sequence_end": 6,
  "provenance": {
    "seed_decoded_sha256": "…",
    "source_head": "40-or-64-hex-revision",
    "dirty_overlay_sha256": "…",
    "dependencies": {"aurora": "40-or-64-hex-revision"},
    "renderer": {
      "version": "…",
      "config_layout": "…",
      "config_layout_sha256": "…"
    },
    "registry_sha256": "…",
    "coverage_manifest_sha256": "…",
    "input_manifest_sha256": "…"
  },
  "descriptors": [
    {"type": 1, "ref_hex": "0000000000000007",
     "config_version": 65549, "size": 2772, "sha256": "…"}
  ],
  "records": [],
  "status": {
    "valid": true,
    "errors": [],
    "dropped": 0,
    "open_scopes": 0,
    "overflow": false,
    "total_records": 6,
    "drained_records": 6
  }
}
```

`capture_id` and `provenance` may be supplied by the private capture wrapper;
when the compact recorder chunk omits them, the generator joins the capture to
the single capture ID in the coverage manifest and to the external binding
metadata. If present, `provenance` must match the metadata binding exactly. A
capture generation is
the renderer/device lifetime token; records carrying `capture_generation` or
`renderer_generation` must match the header. A stale generation, invalid
status, dropped/overflowed record, recorder error, open scope, missing record,
or sequence gap is a hard rejection.

When the native status includes `final`, it must be `true` with
`capture_active: false` for a certificate attempt. Intermediate active drain
chunks may be joined in JSONL, but a non-final snapshot cannot certify a
requirement catalog.

The recorder's `descriptors` catalog and every descriptor in a record contain a
typed row reference and the complete descriptor identity:

```json
{
  "type": 1,
  "ref_hex": "0000000000000007",
  "config_version": 65549,
  "size": 2772,
  "sha256": "descriptor-config-digest"
}
```

`ref_hex` is the lower-case, 16-digit unsigned representation of Aurora's
SQLite integer key. The C recorder's compact `descriptors` dictionary may omit
`ref_hex`; in that form the event's `pipeline_ref` supplies the row reference
and the descriptor dictionary supplies `bytes`, `type`, `config_version` and
`sha256`. The generator checks type, row reference, config version, size and
SHA-256 against the actual SQLite row. A missing row, unknown row,
descriptor hash/config conflict, or duplicate typed row with different metadata
is a hard rejection. Descriptor bytes never appear in the capture contract or
generated sidecar.

The native source phase is numeric (`PREPARATION=1`, `ENTRY=2`, `READY=3`,
`INTERACTIVE=4`, `DEATH=5`, `RESPAWN=6`, `ENDING=7`, `TEARDOWN=8`, and
`RETURN=9`). A manifest may use these readable names in `expected_phases`; the
generator resolves them to the numeric enum before joining `context.phase`.
Native numeric phases must be joined with the numeric `context.scene`; a
phase-only native record is rejected because the same phase number can occur in
CSS, SSS and MATCH. Custom phase names remain string identities. A numeric recorder
`context.coverage_case_id` must be bound explicitly by a case's
`coverage_case_id` field or the manifest's top-level `case_ids` map, for
example `"case_ids": {"17": "mario-fd"}`. An unbound numeric case ID is a
hard rejection. The generated coverage entry preserves the numeric binding.

Record `kind` is one of:

* `scope_begin` and `scope_end`, which pair an immutable source scope;
* `use`, `draw_use`, `merge_use`, or `demand`, which are source demand edges;
  the native recorder names these outcomes `last_ref`, `ready`, `pending`,
  `create`, `draw_cache_reuse`, `merge`, and `packet_use`;
* `import`, which records background/catalog import and is excluded from group
  membership.

Demand records carry a `scope` object with `scope_id`, `case_id`, `route_id`,
`phase`, and, when available, `source_tick`, `render_packet_id`, `action`,
`costumes`/`costume_ids`, `lifecycle`, and `input_manifest_sha256`. The compact
C recorder writes these as direct event fields (`type`, `pipeline_ref`,
`config_version`, `descriptor_sha256`, `frame_id`, `packet_id`) plus a chunk-local
`context_index` into the full numeric context object (`coverage_case_id`,
`phase`, `source_tick`, `players`, and `input_binding_sha256`). Scope begin and
end records carry the same context; after expansion the generator treats it
exactly like a verbose `context` object. Their live scope token is represented
by the stable numeric scope ID and worker
thread ID, never a pointer. A demand scope without a paired begin/end is
always rejected. Deferred demand may follow the end
record with the same scope ID and immutable context, even when its worker
thread ID differs. Absent source context is always rejected for a demand edge.
Warm finder hits, clean draw-cache hits and merged draws are demand edges and
must be recorded before their fast return.

## Binding metadata

The metadata root has schema `melee-web-pipeline-requirements-input-v1` and
version `1`, then supplies:

```json
{
  "schema": "melee-web-pipeline-requirements-input-v1",
  "version": 1,
  "seed": {"decoded_sha256": "…"},
  "source": {"head": "…", "dirty_overlay_sha256": "…"},
  "dependencies": {"aurora": "…", "melee": "…"},
  "renderer": {"version": "…", "config_layout": "…"},
  "registry": {"fighters": [], "stages": []},
  "coverage_manifest_sha256": "…"
}
```

`registry` is source-owned numeric identity data. Its canonical SHA-256 is
bound into the capture and output; names, paths and source addresses are not
copied. Passing `--source-root` additionally derives `git HEAD` and an
isolated-index binary diff digest. That diff includes tracked edits and
untracked source files without changing the caller's real index, then compares
the result with the declared source binding. Dependency revisions, renderer
version/layout, seed bytes, registry identities and coverage-manifest bytes all
participate in the binding.

## Coverage manifest

The coverage root has schema `melee-web-pipeline-coverage-v1` and version `1`.
Every case declares a route, expected phases, actions, costumes, lifecycle and
a frozen input/capture binding:

```json
{
  "schema": "melee-web-pipeline-coverage-v1",
  "version": 1,
  "cases": [{
    "case_id": "mario-fd",
    "coverage_case_id": 17,
    "route_id": "mario-fd",
    "route": {"route_id": "mario-fd", "fighter_numeric_ids": [1, 1], "stage_numeric_id": 3,
              "menu_fighter_numeric_ids": [1, 1], "menu_stage_numeric_id": 3},
    "expected_phases": [
      {"scene": "css", "phase": "preparation"},
      {"scene": "sss", "phase": "preparation"},
      {"scene": "match", "phase": "entry"},
      {"scene": "match", "phase": "ready"},
      {"scene": "match", "phase": "interactive"},
      {"scene": "teardown", "phase": "teardown"},
      {"scene": "return", "phase": "return"}
    ],
    "expected_actions": ["Wait1", "Jab1"],
    "expected_source_ticks": [120, 121],
    "expected_costumes": [0],
    "lifecycle": ["entry", "interactive", "ending", "return_css"],
    "input": {"capture_id": "css-sss-mario-fd-20260913-a", "sha256": "…"}
  }]
}
```

The per-case numeric ID is optional for string-only private captures. It is
required whenever native recorder records use numeric `coverage_case_id`.
The recorder's player context supplies numeric fighter/costume identities and
`motion_id` values; manifests can list numeric motion IDs in
`expected_actions`, which are checked against active players. Optional
`expected_source_ticks` binds action/phase evidence to exact source ticks.
Native imports
must carry the BOOT/PREPARATION context and zero frame, packet, renderer and
device generations; they remain excluded from requirement membership. For CSS
and SSS contexts whose transient selection differs from the final match route,
the route may bind `menu_fighter_numeric_ids`, `menu_stage_numeric_id` and
`menu_ground_numeric_id` (and corresponding menu costume IDs); those identities are checked in the menu scope while
the ordinary `fighter_numeric_ids` and stage/ground fields remain the match
route identity. Final `costume_numeric_ids`/`costume_ids` are checked in MATCH
scopes when supplied. A menu scope without those optional fields preserves observed
context without substituting the final match selection, and remains explicitly
incomplete for certification when the route declares final content identities.
When a route traverses more than one transient menu selection, it may instead
declare a finite tuple list:

```json
"menu_identities": [
  {"fighter_numeric_ids": [1, 0], "costume_numeric_ids": [0, 1],
   "stage_numeric_id": 3, "ground_numeric_id": 37},
  {"fighter_numeric_ids": [8, 8], "costume_numeric_ids": [1, 0],
   "stage_numeric_id": 32, "ground_numeric_id": 36}
]
```

Each tuple has exactly these four numeric fields; fighter and costume lists
are non-empty and have equal lengths. Every observed CSS/SSS selection tuple
must equal one complete listed tuple, so fighter, costume, stage and ground
values are never matched as independent Cartesian-product sets. The tuple
form cannot be combined with fixed `menu_*` fields, and duplicate or malformed
tuples are rejected; fixed menu fields also may not provide multiple aliases
for one identity. A route with final content identities but no fixed menu
binding or `menu_identities` remains explicitly incomplete.
BOOT/PREPARATION scope boundaries may legitimately have
`world_generation: 0`; a paired empty setup scope records only global
initialization and does not count as unbound source content. Empty CSS, SSS,
or MATCH PREPARATION construction scopes receive the same treatment. Any
non-preparation scope or demand in CSS, SSS, MATCH, teardown, or return with
`world_generation: 0` lacks a bound source world and forces an incomplete draft,
even when its case and frozen input are otherwise bound.

The generator waits until the complete ordered stream has been joined before
classifying an empty scope. A scope that has no demand before or after its
paired `scope_end` is setup-only when it is BOOT or PREPARATION: its default
selection fields are ignored, it contributes no route identity or descriptor
membership, and it cannot satisfy source action or costume coverage. If a
deferred demand later uses that scope, both boundary contexts are checked
strictly against the route and the demand's own content checks remain strict.
Empty non-PREPARATION boundaries retain the strict route checks and remain
incomplete when their source world is absent.

The manifest digest is the SHA-256 of its supplied bytes (or canonical JSON
when passed in-process). The generator verifies every demand edge's case,
route, phase and frozen input against this manifest. It does not use an
optional `complete` flag as evidence. Missing expected content produces an
explicit `status.incomplete_scopes` entry.

## Generated sidecar

The generated root has schema `melee-web-pipeline-requirements-v1` and contains
the binding, capture identity, descriptor identities, sorted composite groups,
and the declared coverage inventory. Each coverage case also carries a
`route_identity_sha256` digest, binding its source player/stage registry
selection without copying archive names or paths. A group is keyed by the conservative
`route_id:scene:phase` composite when the native scene is bound (the
string-only private form remains `route_id:phase`). This preserves the native
scene because a numeric phase can occur in CSS, SSS and MATCH. Members are the
union of descriptor identities from `use`, `draw_use`, `merge_use`, and
`demand` records only. The same descriptor may be a member of many groups. Each
member carries sorted provenance links with capture ID, sequence, case ID,
scope ID, source tick, render packet ID and worker thread ID when supplied.
Deferred demand is accepted after a paired source scope closes only when its
scope ID and immutable source context match.

The output contains no raw trace, game bytes, descriptor bytes, personal paths,
CPU/GPU addresses, or diagnostic fields. Hashes, numeric registry identities
through their registry digest, and case/provenance identifiers are safe binding
data. `status.kind: "certificate"` and `certified: true` are emitted only when
all declared cases satisfy every expected phase/action/costume/lifecycle and
all input and capture checks pass. The resulting certificate is still scoped to
the declared finite coverage inventory.
