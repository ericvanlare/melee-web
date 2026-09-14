# Renderer startup fix: local regressions pass; hosted signoff incomplete

The shared player owner now creates `/melee-render-cache` before native startup,
independently of optional persistence. This is a browser-memory filesystem path,
not an HTTP endpoint. Development may subsequently mount IDBFS; the public alpha
continues without persistence. The tests cover disabled and unavailable storage,
entry callback ordering, and a directory failure that prevents initialization.

The 280-descriptor selection had missed 18 descriptors in Falco/Battlefield. The
runtime now requests the entire verified 508-descriptor catalog through the same
checked, deduplicated preparation queue. Import alone creates no pipelines;
preparation completes before source time starts. Prepared objects remain alive
across source-scene transitions in the same device generation. Public fallback
and its local counter remain available for genuinely unseen descriptors; the
validation configuration rejects undeclared requests. No telemetry was added.

This restores known coverage at a startup cost. It does not establish that 508
descriptors cover every enabled action, or that a smaller certified selection is
safe. The [previous 280-member failure](SELECTIVE_PIPELINE_ALPHA.md) and its raw
local evidence are preserved. No captures were repaired or regenerated, and no
raw per-draw provenance entered the repository or public artifact.

## Scoped local evidence

All four original CSS → original SSS → four-stock gameplay → original CSS
lifecycles completed, including Eject and document reload. Each has zero
unexpected descriptors, zero deferred preparation, zero interactive pipeline
construction, and no observed error. The timing columns cover callbacks that
entered with source time running; preparation is reported separately.

| Route | Profile | Native maximum (ms) | Browser gap maximum (ms) | Complete renderer bootstrap (ms) | Disc ready (ms) |
| --- | --- | ---: | ---: | ---: | ---: |
| Falco/Battlefield | cold | 9.805 | 27.350 | 8859.410 | 10264.364 |
| Falco/Battlefield | warm | 7.940 | 21.990 | 1675.555 | 3010.616 |
| Mario/FD | cold | 9.590 | 27.135 | 2865.125 | 4171.174 |
| Mario/FD | warm | 7.340 | 20.685 | 1674.800 | 2992.772 |

Every local native callback is within 16.67 ms; browser and animation-frame gaps
are within 33.33 ms. Cold means a fresh headed Chrome profile; warm means an
ordinary reload in that profile. Browser/driver caches are not globally cleared.
The cold preparation variation must not be omitted from a startup claim. Wall
timers overlap and must not be added. The [compact evidence](evidence/renderer-startup-fix-v1.json)
contains renderer initialization, catalog submission/readiness, every scene
preparation profile, pipeline counters, and the hashes of the retained attempts.
This is browser/performance evidence, not retail-equivalence or content admission.

The eight focused startup/cache/registry tests passed in 2.49 seconds. The
incremental public build passed in 25.98 seconds. Packaging, package audit and
real local/hosted HTTP checks passed. The headed public-player test passed all
ten loader, controls, invalid-disc recovery, unload/reload, second-import and
storage/network checks. The final full suite passed 758 tests with 31 optional
skips in 227.08 seconds. It ran once, after the focused loop and local browser
regressions; no broad corpus or holdout run was added.

## Hosted evidence and limitations

The immutable noindex preview is
[62f8a526.webmelee-staging.pages.dev](https://62f8a526.webmelee-staging.pages.dev).
Its hosted files match the locally tested manifest. The stable PR15 staging
deployment and production remain unchanged.

The first hosted Mario cold attempt completed renderer preparation in 1818.275
ms and disc import/preparation in 3145.214 ms, but timed out in stage select.
It has no gameplay evidence. Mario warm completed the full lifecycle, with
native/browser maxima of 8.265/25.225 ms and zero unexpected pipelines.

The hosted Falco cold lifecycle passed, with native/browser maxima of
8.535/25.395 ms, zero unexpected pipelines, 1781.825 ms renderer bootstrap and
3075.557 ms disc-ready time. Its warm attempt selected Mario/Battlefield instead
and was rejected immediately. It cannot count as a Falco pass. All 508
descriptors were ready with no errors in both rejected attempts. These input
selection failures remain failures; they have not been attributed to a renderer
regression or erased by another run.

One fresh PR15 Falco/Battlefield pair at its unchanged immutable URL completed
both lifecycles, with zero interactive construction and native/browser maxima
of 7.720/24.690 ms cold and 8.625/25.355 ms warm. PR15 disc-ready time was
3078.824 ms cold and 2992.648 ms warm. The candidate's 3075.557 ms cold result
shows no material speedup in this pair. This is a small comparison, not an
estimate of the cold-start tail or a claim to have resolved the historical stall.

The requested startup-speedup release remains **NO-GO**. Hosted signoff is also
incomplete because of the two selection failures above. The quick iteration
stopped after this comparison: the larger four-cold/four-warm-per-artifact matrix
and a fresh production gameplay run were not performed. Production's prior
missing-directory evidence remains in the performance investigation. This draft
fixes the shared setup and known descriptor misses, but is not an all-green
production candidate.

## Exact artifact and promotion boundary

- Artifact source commit: `05d629adbcfbefe33a0c1880088e3e3d22dc02bc`.
- Runtime group: `4e6fa7f58a84a6ae`.
- Wasm SHA-256: `7e15e154fbec89f2a24de55a02a5d72c725eaff69c97f4f813aed299074624a6`.
- Loader SHA-256: `df53825947a9b94c133f4d915912c3f16afa382c3f301644f1e9889076d25479`.
- Catalog SHA-256: `cdf157ee0f1850884f07a71165fd2192177acb23c2c777313b719e70ea67546f`.
- Package manifest SHA-256: `a8d09936ea7c759acc4d3898baf06acb1d55257ffcd8a82f200cd4bae03efbaa`.

The retained package is `work/renderer-startup-fix-20260914/public-candidate-1`,
with its adjacent manifest. It was built at the source commit above and was not
rebuilt after documentation-only changes. Draft PR #18 remains open. No merge,
production deployment, protected worktree edit or history rewrite occurred.

After the remaining release decision is satisfied and production promotion is
explicitly authorized, the exact command to promote these same noindex bytes is:

```sh
WRANGLER_SEND_METRICS=false \
  work/pipeline-provenance-20260913-01/deploy-tools/node_modules/.bin/wrangler \
  pages deploy work/renderer-startup-fix-20260914/public-candidate-1 \
  --project-name webmelee --branch main \
  --commit-hash 05d629adbcfbefe33a0c1880088e3e3d22dc02bc --commit-dirty=false
```

This command was not executed. Historical GPU stalls and CPU divergences remain
open; both human holdouts remain unopened. Exhaustive 4×4 manifest certification
and capture-size optimization remain follow-up work. Async pipeline creation
was not added: the existing experiment did not demonstrate a startup speedup.

## Minimum iteration loop

For this startup/descriptor boundary, first run:

```sh
python3 -m unittest tests.test_melee_runtime_owner \
  tests.test_public_player_startup tests.test_runtime_cache \
  tests.test_aurora_pipeline_preparation -v
python3 scripts/build.py --target runtime-public --configuration Release \
  --selective-pipelines --jobs 2
```

Then run the retained Falco cold/warm driver against one package, because it
directly exercises the previous miss. Run Mario and the public UI checks once
the targeted route passes. Keep full discovery at the final checkpoint, not on
every edit. Do not run builds or CPU suites alongside performance measurements.
Reuse the verified artifact for staging and preserve rejected attempts. Unchanged
CPU and human-replay corpora are not part of this renderer iteration loop.
