# Selective pipeline alpha candidate: no-go

The bounded release decision is **NO-GO**. The final artifact completes cold and warm Mario/FD with no unexpected pipeline construction. Falco/Battlefield completes both lifecycles but discovers **18 undeclared descriptors in each**, expanding the device-local selection from 280 to 298. Four fallback pauses add 852.945 ms cold and 194.295 ms warm. This violates the zero-unexpected-pipeline gate. No staging preview or hosted comparison was run, and production is unchanged.

The implementation imports all 508 catalog descriptors without eagerly constructing their GPU pipelines. A compact union from six already certified captures requests 280 descriptors before interactive play. Requests are deduplicated and prepared objects survive source-scene transitions within the current device generation. The cache directory is ephemeral MEMFS, without IDBFS. Validation builds reject undeclared descriptors; the public alpha pauses source time, prepares missing descriptors and resumes. Its local diagnostic counter has no network telemetry. The observed fallback resumed successfully, but it can interrupt gameplay and leave an incomplete first-use frame.

## Final local artifact

| Route | Cache | Native max (ms) | Browser gap max (ms) | Unexpected descriptors | Fallback total (ms) |
| --- | --- | ---: | ---: | ---: | ---: |
| mario-fd | cold | 7.215 | 22.065 | 0 | 0.000 |
| mario-fd | warm | 7.290 | 25.245 | 0 | 0.000 |
| falco-bf | cold | 11.075 | 27.110 | 18 | 852.945 |
| falco-bf | warm | 8.885 | 25.610 | 18 | 194.295 |

All four runs complete original CSS → original SSS → four-stock gameplay → original CSS, followed by Eject and document reload. The timing columns cover callbacks that entered with source time running. They do not hide the separately recorded fallback work: the cold Falco preparation callback maximum reaches 169.770 ms. Its four pauses are 357.400, 207.565, 143.960 and 144.020 ms; warm pauses are 59.005, 45.290, 45.875 and 44.125 ms. Source time is stopped before deferred construction. These are scoped browser/performance results, not retail-equivalence or content admission.

| Route/cache | Renderer init (ms) | Union submission (ms) | Union ready (ms) | Complete bootstrap (ms) | Disc ready (ms) |
| --- | ---: | ---: | ---: | ---: | ---: |
| mario-fd/cold | 54.860 | 6.375 | 979.775 | 1034.635 | 2344.403 |
| mario-fd/warm | 19.900 | 1.420 | 890.110 | 910.010 | 2227.858 |
| falco-bf/cold | 55.905 | 6.245 | 1045.660 | 1101.565 | 2407.666 |
| falco-bf/warm | 18.425 | 1.420 | 894.575 | 913.000 | 2227.643 |

These wall timers overlap and must not be summed. The [release evidence](evidence/selective-pipeline-alpha-release-v1.json) records every scene-preparation profile, construction event, GPU completion wait, fallback pause and source artifact binding. Preparation profiles include GPU completion waits. Fresh startup improvement against the 508-pipeline PR15 baseline is **not established** because the local gate failed before hosted comparison.

The 754-test local suite passed with 31 optional skips. The 23 focused public release-guard tests, 13 manifest tests and two native registry/hash tests passed. The pending-transition regression, public runtime build, strict-cache translation unit, public package audit and real HTTP checks passed. The final headed public-player test passed all ten loader, controls, invalid-disc recovery, CSS/SSS, unload/reload, second-launch, legal and storage/network checks.

## Exact bindings and retained failures

- Artifact source commit: `e9f8cdbdf4b34b00e9c5b1d1cabd6443f6271bc9`.
- Runtime group: `a82fc986ebecd0e8`.
- Wasm SHA-256: `d59c82c0acf84137f025007546a20bcccfc8e7df2d16d8a5545a7a7987076b4c`.
- JavaScript SHA-256: `a827d5b6ef76e4d6f019c636cc5044dabd223154605df4d7a9b122683de56f14`.
- Descriptor catalog SHA-256: `cdf157ee0f1850884f07a71165fd2192177acb23c2c777313b719e70ea67546f`.
- Package manifest SHA-256: `75905f69eb8488b8fd9a91f97de27e792c58420505becb7b182338475f4a8550`.
- Compact union: 280 descriptors, 773,452 configuration bytes; binding SHA-256 `0ad391b8c9c929779abc228360dfbe47616b2c202ab75b17c47e5ccd838b31d4`.
- Generated header SHA-256: `088007a5b8ae54913758c88a12fa2d4086a05550a40347daf3f8c25c3387f069`.

Raw per-draw provenance and full certificates remain untracked and outside the public package. Mario/Battlefield's certificate rejection is preserved; no repair or recapture was needed to construct this finite union. Earlier successful Mario/FD and Fox/Battlefield runs remain recorded against their own artifact hashes. The final Fox selection reruns include retained wrong-route failures caused by ordinary cursor-input timing; Falco/Battlefield became the final representative using ordinary B-button token pickup. Its missing descriptors are an implementation coverage failure, not a passing replacement result.

Review also found a pending-transition deadlock after an unexpected final-frame pipeline. The gate now finishes frozen preparation before starting that transition, with a native regression covering drain, GPU wait and transition request. The initial diagnostic-object compile failure, selective-root packaging rejection and Linux test-initializer warning remain preserved with their corrections. Historical GPU stall and CPU divergences remain open. Both human holdouts remain unopened. PR15, protected audio work and public deployments remain untouched.

## Deferred work and promotion

Exhaustive 4×4 manifest certification and capture-size optimization remain follow-up work. This finite union does not certify unobserved fighters, costumes or actions. The candidate is not promotable while the documented local gate fails; no production-promotion command was executed.

The retained package is `work/pipeline-provenance-20260913-01/alpha-release/public-candidate-4`, with its adjacent manifest. Local evidence is under `work/pipeline-provenance-20260913-01/alpha-release/`. The prepared hosted matrix was not started.
