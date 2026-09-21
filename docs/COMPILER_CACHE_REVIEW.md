# Compiler-cache publication review

Public repositories can use GitHub Actions caches. GitHub documents that fork
pull requests can restore base-branch caches, so their contents must be treated
as readable by contributors. That warrants a content and provenance review;
it is not a requirement to disable compiler caching permanently. See
[GitHub's cache access rules](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching).

The initial publication change disabled persistent caches and compiler-seed
transfers in public CI. Its private rehearsal completed successfully in
[run 35556728757](https://github.com/ericvanlare/melee-web/actions/runs/35556728757),
but took 12m 32s from creation to completion, above the 600-second CI target.
The cached run at the same source head,
[35556715568](https://github.com/ericvanlare/melee-web/actions/runs/35556715568),
took 6m 18s. These are individual workflow observations, including queue time,
not a gameplay performance measurement or a controlled speedup estimate.

## Inspect an existing cache

Use the exact key from a retained cache API inventory. The optional
`compiler-cache-audit` job in [Verify](../.github/workflows/verify.yml) restores
that identity, uses ccache's inspection/extraction commands in bounded
subprocesses, and uploads only its report. Ordinary CI does not run this job.
Cached compiler outputs are never executed by the auditor. This explicit
manual mode runs only the content guard and inspection; it does not produce
the required `browser-build` result or substitute for normal PR verification.

```sh
gh api --paginate --slurp repos/ericvanlare/melee-web/actions/caches \
  > work/public-readiness/cache-inventory-for-review.json
gh workflow run verify.yml --ref codex/public-repository-readiness \
  -f audit_compiler_cache=true -f audit_cache_key=EXACT_CACHE_KEY
```

The [auditor](../scripts/audit_compiler_cache.py) can also inspect a local cache:

```sh
python3 scripts/audit_compiler_cache.py --cache-dir .cache/ccache \
  --output work/public-readiness/compiler-cache-audit.json
```

The output path must be new. Exit status 0 means the bounded inspection found
no review findings, 1 means findings need review, and 2 means inspection was
incomplete. Preserve unsuccessful reports. A passing pattern scan does not
prove absence of every secret or game asset and does not grant rights in
compiled inputs. Review the compile graph and dependency notices alongside
the result. A selected-cache receipt covers only its recorded identity, not
all historical cache keys or future uploads.

## Selected policy and source scope

Retain ordinary compiler caching for both public and private CI. A fresh `v4`
namespace avoids restoring prior cache epochs during normal verification.
Unreviewed private-era caches still receive one-time cutover cleanup under
the [publication procedure](PUBLICATION_CUTOVER.md). Starting a namespace alone
does not make old cache keys inaccessible or replace that cleanup.

The review of [seed selection](../scripts/ci_seed.py), the generated Ninja
compile graph and package depfiles found compiler inputs from pinned
dependencies, recovered Melee/SDK sources and current project sources,
including current audio. No local disc, ROM, extracted-asset, credential or
reference-capture binary input was found in the selected compile commands.
The pipeline SQLite seed enters at link/preload time, outside the selected
object compilation. Cache seeding selects `.o` outputs; it does not export
linked players or prepared asset directories. This is source-input evidence,
not a claim about every existing cached byte.

The cache is mixed-project CI state. Dependency notices remain under
[third-party provenance](../THIRD_PARTY.md) and
[the full notice aggregate](licenses/runtime-third-party.txt). Patched Aurora
objects retain their downstream provenance. Recovered source, current audio
and generated material retain the uncertainties accepted in the
[publication assessment](PUBLICATION_PROVENANCE_ASSESSMENT.md); compiler objects
receive no new blanket MIT grant. This decision does not authorize a new
playable or native release.

## Observed inspection

[Run 35560432544](https://github.com/ericvanlare/melee-web/actions/runs/35560432544)
passed the real-ccache regression and inspected the selected `main` cache for
`e8c756a72fe49ea4ec5b6eae7dbd88907142070c` using ccache 4.9.1. The
[receipt](evidence/compiler-cache-review-v1.json) binds the cache key, API ID,
scanner hash, full-report hash, compile-graph evidence and limits.

All 3,092 entries were inspected: 1,532 manifests and 1,560 results. Decoding
produced 1,560 Wasm relocatable objects, 1,559 dependency files and 245 diagnostic
members, totaling 267,756,885 decoded bytes. The bounded scan found no credential,
non-runner personal-path, local-asset-input, unexpected-output or incomplete
inspection findings. This supports the selected cache boundary; it does not
clear every historical key or establish rights in the objects.

The first runner attempt caught a filename-format bug in the new auditor before
it inspected the selected cache. The corrected implementation uses ccache 4.9's
mixed hex/base32hex identity and passes a real cached-compile regression. The
failed attempt and its log remain retained. The audit is an explicit manual job;
normal PR verification supplies the separate CI timing evidence.

The subsequently merged Mewtwo seed received its own
[bounded metadata review](evidence/compiler-review-pipeline-seed-v2.json) before
updating the content guard's exact hash exception. No cache deletion or
visibility change was performed.
