# Selective pipeline alpha candidate

This candidate imports the complete bundled descriptor catalog without eagerly constructing all 508 GPU pipelines. A compact union from six already certified captures requests 280 descriptors before interactive play. The renderer retains them across scene transitions in the current device generation. The public cache directory is ephemeral MEMFS; no IDBFS is mounted.

Validation builds reject undeclared descriptors. The public alpha defers their descriptor bytes, pauses the source clock before construction, settles the scene and resumes. `Module.pipelinePreparation.unexpected_count` is a local diagnostic; no telemetry was added. Unobserved content may trigger a visible preparation pause and an incomplete first-use frame. The bounded routes below did not use that fallback.

## Local release evidence

| Route | Cache | Native max (ms) | Browser gap max (ms) | Unexpected pipelines |
| --- | --- | ---: | ---: | ---: |
| mario-fd | cold | 9.795 | 28.010 | 0 |
| mario-fd | warm | 9.345 | 27.530 | 0 |
| fox-bf | cold | 11.775 | 30.375 | 0 |
| fox-bf | warm | 11.290 | 29.820 | 0 |

Both routes completed original CSS → original SSS → four-stock gameplay → original CSS, followed by Eject and document reload. No gameplay pipeline construction, native deadline failure or browser-gap failure was observed. These are scoped browser/performance results, not retail-equivalence or content admission.

All preparation remains visible in the local evidence. Renderer initialization, union submission, union readiness and complete bootstrap timing are exposed separately; disc import and every scene-preparation profile are retained. Hosted startup comparison against production and PR15 remains the release decision checkpoint.

The 754-test local suite passed with 31 optional skips. The 23 focused release-guard tests (including selective-root acceptance, mixed-root rejection and private-root rejection) passed. The public runtime and strict-cache translation unit compiled. Public package audit and real HTTP validation passed. The headed public-player test passed all ten loader, controls, invalid-disc recovery, CSS/SSS, unload/reload, second-launch, legal and storage/network checks.

## Bindings and retained failures

- Compact union: 280 descriptors, 773,452 configuration bytes.
- Union binding SHA-256: `0ad391b8c9c929779abc228360dfbe47616b2c202ab75b17c47e5ccd838b31d4`.
- Generated header SHA-256: `088007a5b8ae54913758c88a12fa2d4086a05550a40347daf3f8c25c3387f069`.
- Descriptor catalog SHA-256: `cdf157ee0f1850884f07a71165fd2192177acb23c2c777313b719e70ea67546f`.
- Raw per-draw provenance and full certificates remain in ignored local evidence; neither is packaged or committed.
- Mario/Battlefield certificate rejection remains preserved. No repair or recapture was needed for this conservative union.
- The initial diagnostic-object compile error and initial selective-output packaging rejection remain preserved alongside successful corrections.
- Historical GPU stall, CPU divergences and all earlier failures remain open. Both human holdouts remain unopened.

## Follow-up scope

Exhaustive 4×4 manifest certification and capture-size optimization are deferred. Unobserved fighters, costumes, actions and routes are not certified by this finite union. Production is unchanged; PR15 remains the immutable comparison baseline.

Local evidence: `work/pipeline-provenance-20260913-01/alpha-release/`. The deployment and hosted comparison result will be appended before this candidate is promoted. No production deployment is authorized by this change.
