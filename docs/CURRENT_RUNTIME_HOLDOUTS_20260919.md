# Current runtime holdout evidence — 2026-09-20 replacement continuation

The original frozen campaign remains retained as v1: its first timing slot
failed on two live pipeline creations, and its other three slots were
permanently unstarted. A fresh replacement campaign was then authorized under
the same hard gates and completed all four slots with acceptance evidence. The
replacement establishes current-runtime holdout timing acceptance for its fresh
inputs. It does not reclassify the historical September 12 GPU operation or
settle the remaining causal acceptance scope by itself.

The v1 failure and its limits remain in the [v1 receipt](evidence/current-runtime-holdouts-20260919-v1.json)
(`300040b7719baf63d8aef1f6028bd53b8cf75c38d9a60a1dd72d08fe4b4f1f12`). The
failed report was
`work/issue33/holdouts/timing-attempts/01-marth-cold/browser-report.json`
(`31ce0684443af5aed5fd43c7fe87f079bd8a5ec8d155cf34fd5ffa6286f7c478`):
`livePipelinesCreated: 2`, `livePipelinesQueued: 0`, and
`acceptance_evidence: false`. That evidence is retained and is not relabeled.

The replacement continuation used owner-approved scope
`work/issue33/replacement-holdouts/acceptance-scope.json`
(`b252a4b7407369d18b1c183d627d49b7651aea40663f19e1fa2948f8693eb8bd`) and the
frozen runtime receipt
`work/issue33/replacement-holdouts/approval-and-runtime-freeze.json`
(`3ccf5362b72ebe48c6543226afe591faac00383fac86b3549329465b53d228d7`). The
candidate code verification commit is `713e45a8b6f67e00ef5fb88c045419a48c82c230`
with seed
`4bdb7c4a3e906d907d066f0c65041d7eb3472dc25c9d8c98be6e4946fdce560f`.
The browser profile metadata records base commit
`6fe77fafc653b44ed168a8f091ecef4dbfbbd84a`.

Fresh identities were selected and reserved before runtime under
`work/issue33/replacement-holdouts/selection-manifest.json`
(`6f190b2dc71279f29d5f0dab8e6d10e173bedb092eefa53f2ba8cdae6470351b`), then
bound into the canonical reservation manifest
(`be52f16661da2696268fc50ed6b43c4d6b5f8a1974859c98d2cfa6e2b8d852ab`) and
execution plan
(`50fd6b945741f31136e702070249df9c318da74acfc47cf38ece77d4267532da`). The
freeze clarification records that the stale nested pre-freeze approval field is
historical and that the frozen plan and runtime were not modified
(`e7488b4dc370b66f78af1bce5b1baf3e536a9e192e36770241cc3857180111fe`).

The two fresh replacement holdouts are:

- `ucf-off-replacement-marth-bf-e6903468`: Marth/Marth on Battlefield, source
  SHA `e6903468493f93fec6401d7496f840a914ed939f800d0513e6d42b630bd296dd`,
  6,432 input frames, and recipe SHA
  `c443284216a19a08cf0d40d4f09987ce676d9ee5f3902911d861a8c8b65ffc55`.
- `ucf-off-replacement-falco-fd-90cb8348`: Falco/Falco on Final Destination,
  source SHA `90cb8348fdac8a1f4cdc7fef31469c79dfd40d515d25ce8d41e47582ee7f5730`,
  8,561 input frames, and recipe SHA
  `9c1ed600ff1e553b714a7c57b95b9ca915591c84cdb1396e8e6da9289b5a699b`.

Both original reference preparations exhausted their bounded input caps without
observing an original match ending. Marth’s retained reference evaluation is
`work/issue33/replacement-holdouts/ucf-off-replacement-marth-bf-e6903468-prepared/evaluation/finish.json`
(`0e42657347a8167add764161ccd598667faf6013b3b2fcfd8fb624a2f6b84290`);
Falco’s is
`work/issue33/replacement-holdouts/ucf-off-replacement-falco-fd-90cb8348-prepared/evaluation/finish.json`
(`2b09ec6c7502255dc8dfb65f08a88bc2cb7f1338b036bf07e4d93e9fdb56d92c`). Both
are `bounded_cap_exhausted`, incomplete original matches, and not gold-admitted.
The exact paired references and discovery receipts are bound in the v2 receipt.

The visible state comparisons pass their declared scopes at 6,432 Marth frames
and 8,561 Falco frames. The state acceptance receipt is
`work/issue33/replacement-holdouts/state-acceptance.json`
(`e01d670cb1bd16d9b397a429667b8b2577c8400a155ad164c914c28073c411cf`); its
state gates and timer audits report no first divergence, exact reference
binding, and `per_tick` input scheduling. This is declared
entry/input/fighter/RNG/match-clock/PAD/history-bank and timer evidence. It does
not establish original draw-cadence or scheduling equivalence, pixels, PCM,
hardware output, full-game equivalence, live-input behavior, consecutive-match
admission, or gold admission.

The replacement timing matrix used one unprofiled cold/warm pair per holdout,
one repetition, a 300-second slot timeout, and unchanged thresholds of
33.333 ms for browser gaps and native hard callbacks plus the 16.667 ms native
target. The four consumed slots were `01-marth-cold`, `02-marth-warm`,
`03-falco-cold`, and `04-falco-warm`; each finish is `completed` with
`acceptance_evidence: true`, no report failures, and valid role/recipe binding.
Across 29,986 source updates, draws, and callbacks, the aggregate browser
maximum was 29.000 ms and the aggregate native maximum was 13.695 ms. Browser
hard gaps, browser long tasks, native callbacks over 33.333 ms, native
callbacks over 16.667 ms, live pipeline creations, live pipeline queueing,
audio underrun/overflow counters, browser errors, and focus loss were all zero.
The timing status receipt is
`work/issue33/replacement-holdouts/timing-status.json`
(`d8d4eb7365e9461f01ff52dc207fdf5efe2e573051ab065798888b81e27744e4`). All
four timing reports classify the causal owner as `NOT_CLASSIFIED`.

The reports retain preparation and resource details separately from acceptance
counters. Preparation took 149.600–166.200 ms per slot, with 122.540–150.205 ms
construction, 15.650–26.805 ms render wait, 13.840–16.725 ms GPU completion
wait, two preparation callbacks, zero preparation source draws, zero preparation
texture uploads, and zero preparation pipeline creation or queueing. Cold
attempts started with the optional render cache cleared; warm attempts retained
3,464,168 bytes for Marth and 3,373,528 bytes for Falco. Reported live texture
upload totals were 5,969,920 bytes for Marth and 4,169,728 bytes for Falco;
reported live staging usage was 10,796,538,896 and 9,893,205,012 bytes
respectively. Peak staging usage was 3,673,936 bytes for Marth and at most
2,990,012 bytes for Falco (cold; the warm Falco slot reported 2,710,456 bytes).
The Marth slots grew the Wasm heap by 66,846,720 bytes; the Falco slots reported
zero Wasm heap growth. The v2 receipt records the per-slot memory snapshots,
preparation values, staging/upload counters, diagnostics, and hashes.

The browser profile was headed Chrome 153.0.8010.50 on Mac16,12 / Apple M4 with
32 GiB on macOS 26.6.2, AC power with low power mode disabled, 640×480 at DPR 2,
and focus emulation disabled. `GraphiteDawnMetal` is the Chrome browser’s Skia
backend in this profile; it is not the game’s WebGPU backend. The run did not
throttle CPU, hide source ticks, add synthetic warmup, or control the driver
cache. Preparation, memory growth/teardown, cache state, source progress, and
uploads remain diagnostic measurements; the acceptance counters cover the
declared unprofiled timing interval. Pixels were not compared and PCM was not
admitted by this campaign.

The first timing plan normalization preflight rejected the approval receipt
shape before any slot started. The retained derived-scope receipt then bound the
original approval and new freeze; the original spec and failed log remain
preserved. The preflight, derived scope, quiet-start, quiet-extension, and
quiet-release receipts are recorded in v2. The quiet-release receipt’s
`record_created_at_utc` records receipt creation; its own note states that this
is not the exact audio-release message time.

The final evidence backup is bound by
`work/issue33/replacement-evidence-backup-complete-v1/manifest.json`
(`487bacc3fda66aa63c83623265ff60241ea4bb5f2a6a1142dd599b0dce652b5d`): 860
regular files and 803,034,750 bytes were hash-verified, with 32 nonregular IPC
entries explicitly skipped. The separately verified hosted release is recorded
in the [production release receipt](evidence/public-marth-pipeline-release-v1.json)
(`61942ae356d1134ab5df144ce0b668f5ebcdb9ec3942f1c50c550ba30f1ad32a`).

## Historical v1 failure and seed recovery

The original v1 campaign used Marth/Marth Battlefield at 7,347 frames and
Falco/Falco Final Destination at 5,953 frames. Their retained state/timer
reports had no first divergence; original draw audits observed 7,341/7,341
unchanged traversals across Marth’s 7,347 source ticks and 5,948/5,948 across
Falco’s 5,953 ticks, with 6 and 5 source ticks not drawn. Those observations
were declared state/source-draw evidence only: performance, pixels, audio, and
gold admission were not evaluated.

Two earlier 180-frame development state preflights remain separate evidence.
Both completed with `performance: not_evaluated`, `pixels: not_compared`, and an
incomplete source match. They do not convert either holdout into a development
input or supply timing acceptance.

A retained supplemental resource event records one creation at runtime frame
3889 / source frame 3754, with `created_delta: 1` and `queued_delta: 0`. It is
aggregate-only: no descriptor hash or source draw owner is retained, and no
earlier creation event is retained. It cannot assign ownership or retroactively
classify the historical September 12 operation.

The first Marth preparation failed before launch because it referenced the
missing `tools/extract_disc_file.py` path and emitted no runnable recipe. The
earlier Falco preparation wrapper was explicitly unlaunched and superseded.
The reviewed preparation wrapper was also unlaunched at preparation time;
`run_reference_pipeline` later launched that reviewed bundle for the retained
complete reference evaluation. The old prepared Falco bundle was not used for
that evaluation. These preparation records are not timing or gameplay passes.

After the stopped v1 timing campaign, a read-only IndexedDB recovery recovered
the exact pipeline cache DB and WAL from the copied failed-run profile. The Dawn
driver cache files were retained separately and excluded. The review was
eligible with exactly two new descriptors (`dfe90cff` and `f285d8c5`), zero
payload conflicts, and zero missing base rows. The merged candidate seed is
`work/issue33/marth-missing-pipelines/two-descriptor-seed.db`
(`4bdb7c4a3e906d907d066f0c65041d7eb3472dc25c9d8c98be6e4946fdce560f`); its
preservation receipt records all 626 prior rows preserved field-for-field and 2
appended rows. Focused seed/review validation passed 11 tests.

The corrected retained development Marth state capture completed all 7,347
updates/draws with an exact timer match, zero live pipeline creation/queue
counts, native/browser worst metrics of 14.350/24.800 ms, and no audio or focus
faults. A helper had hardcoded the holdout role; the corrected receipt binds the
actual capture as `role: development`, while the original mislabeled state-gate
remains retained. Because this state capture overlapped public build and
full-suite work, its performance field is `not_evaluated` and it is not timing
acceptance; the stopped v1 campaign was not rerun. Both Release builds, the
production package audit, the full suite, and the headed public-browser checks
passed, with their exact receipts retained in v1.

The complete replacement receipt is the [v2 receipt](evidence/current-runtime-holdouts-20260919-v2.json).
It binds the selection, canonical reservation, freeze clarification, reference
results, state/timer gates, all four starts/reports/finishes, runtime identity,
preparation and memory details, coordination records, backup, and evidence
limits with repository-relative paths and hashes. The historical September 12
GPU cause remains unresolved and unclassified under the approved scope.
