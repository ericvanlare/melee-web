# Compact pipeline preparation union

## Current publication boundary

The tracked preparation header is **stale development metadata**: it binds
seed `cdf157ee0f1850884f07a71165fd2192177acb23c2c777313b719e70ea67546f`,
while the current materializer requires
`8df6a998cef19b88a3eff0ee61f666e42791f5f73841818961aa3b224cad2c4b`.
`MELEE_WEB_SELECTIVE_PIPELINES` remains off by default. Do not use the old
header as a certificate for the current release or enable that path without
regenerating and validating it from matching certified inputs. This is a
separate performance-preparation task; the publication pass preserves the
metadata and its known limitation. See the
[provenance assessment](PUBLICATION_PROVENANCE_ASSESSMENT.md#generated-aurora-renderer-metadata).

`scripts/generate_pipeline_preparation.py` joins an explicit list of already
certified requirement/coverage sidecar pairs into one conservative preparation
union. The certificate and coverage inputs remain private evidence. The output
does not contain input paths, capture IDs, route or case inventories, raw
provenance, traces, descriptor payloads, or game data.

The result is finite and deliberately non-exhaustive. It is suitable for the
public alpha's warmup set: every descriptor used by every nonempty certified
demand group is included once, including the shared Clear descriptor. Empty
groups and unused descriptor dictionary entries do not add anything. The
generator validates each certificate's status, descriptor dictionary and
nonempty provenance membership, joins certificate cases to the paired coverage
manifest, verifies route/input identities, and requires all inputs to share the
same source, dependency, renderer, registry, and seed bindings. A rejected
certificate must not be listed as an input.

## Input list

The command accepts either a bare JSON array or a versioned container:

```json
[
  {"requirements": "private/case-a-requirements.json",
   "coverage": "private/case-a-coverage.json"},
  {"requirements": "private/case-b-requirements.json",
   "coverage": "private/case-b-coverage.json"}
]
```

```json
{
  "schema": "melee-web-pipeline-preparation-v1",
  "version": 1,
  "inputs": [
    {"requirements": "private/case-a-requirements.json",
     "coverage": "private/case-a-coverage.json"}
  ]
}
```

Example:

```sh
python3 scripts/generate_pipeline_preparation.py \
  --inputs private/preparation-inputs.json \
  --metadata-output private/pipeline-preparation.json \
  --header-output src/pipeline_preparation.generated.hpp
```

The CLI creates output parent directories. The metadata output is intended for
ignored build/evidence directories. Only the generated header is checked in.

## Compact metadata

The metadata root has schema `melee-web-pipeline-preparation-v1`, version `1`,
`finite: true`, `exhaustive: false`, and `kind:
conservative_certified_union`. It contains only:

* common source/dependency/renderer/seed/registry binding hashes;
* certificate and coverage file SHA-256 digests;
* the descriptor count and total configuration bytes; and
* `descriptor_union`, whose entries are exactly
  `(type, ref_hex, config_version, size, sha256)`.

All descriptor references, versions and sizes are bounded for Aurora's ABI. The
union is limited to 1,024 descriptors and 16 MiB of configuration bytes, and
typed descriptors may not alias a global pipeline reference. These checks are
performed before either output is written.

## Generated header interface

The checked-in header defines one payload-free descriptor type:

```cpp
typedef struct MeleeWebPipelinePreparationDescriptor {
    uint32_t type;
    uint64_t pipeline_ref;
    uint32_t config_version;
    uint32_t size;
    const char* sha256;
} MeleeWebPipelinePreparationDescriptor;
```

The sole generated array and its two count spellings are:

```cpp
static const MeleeWebPipelinePreparationDescriptor
    melee_web_pipeline_preparation_union[] = { /* compact identities */ };
static const size_t melee_web_pipeline_preparation_unionCount = ...;
#define melee_web_pipeline_preparation_union_count ...
```

The runtime imports this array once at bootstrap, before interactive play. It
does not construct all catalog pipelines eagerly and retains successfully
prepared pipelines across scene transitions for the current device generation.
Unexpected interactive descriptors are handled by the runtime policy: strict
validation builds fail closed, while the public alpha pauses the source clock,
prepares the bounded descriptor safely, resumes, and increments a local
diagnostic counter. No network telemetry is part of this artifact.

Exhaustive route certification and capture-size optimization remain follow-up
work. The compact union makes no claim that unobserved routes, actions,
costumes, lifecycle phases, or renderer paths are covered.
