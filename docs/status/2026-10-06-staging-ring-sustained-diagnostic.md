# Sustained staging load: induced guard and inconclusive ring comparison

Evidence labels: **Source identified**, **Browser exercised**.
The [portable receipt](../evidence/staging-ring-sustained-diagnostic-v1.json)
binds reports in owned run `h1-sustained-load-reducer-20261006-154556-073b048c`.
The [earlier staging experiment](2026-10-06-staging-ring-experiment.md) preserves
its older byte, timing and natural-route evidence. This is a synthetic, headless mechanism diagnostic. It does not establish an H1
fix, explain the earlier natural pause, or change the default two slots.

## Frozen boundary and retained failures

The owner Mac mini (macOS arm64), installed Chrome 154.0.8037.98, ran with silent
host speakers while retaining Web Audio and PCM processing. Both rings used the
same disc and synthetic conditioned MWRCv4 fixture of 1,800 neutral four-player
inputs. Hash capture was off; staging diagnostics were on. Fixed 300/600-work
calibrations requested 500 ms each before the game clock started. One preparation
wait was bounded at 180 seconds. A ready gameplay control requested two seconds
at 300 work, followed by one treatment requesting two seconds at 600 work, with
at most 100 ms of declared observer slack. The first pause, preparation or fault
stopped the attempt. There was no host stall, automatic resume or adaptive tuning.

The initial identity preflight refused the old timing-build binding without
starting a browser/server: the current WASM and development-module identities
differed. New prospective manifests bound the current local 31-file inventory,
then required fresh HTTP pre/post equality. The old timing report binds disc and
fixture identity only. Its historical artifact map remains unchanged. The earlier
natural report's 24 actual responses plus local WASM identity do not become a
retrospective 31-file pre/post map.

Ring-two attempt 1 then failed after calibration, before either measured window,
with `page.evaluate: ReferenceError: readNative is not defined`. The status reader
closed over a Node variable that Playwright did not serialize. Its unit mock had
called the original callback directly and concealed this boundary. The repair
passes the option explicitly, skips every native call after unload, and tests
callbacks in an isolated realm. The unchanged failed helper reproduces the same
exception there. Its report, trace, recorder and launcher receipt are preserved.
The first launcher used only a run-local marker; successors exclusively acquired
the shared timing marker and removed only their own file identity.

## Ring two: the guard and the sampled fraction are different quantities

Attempt 2 qualified its 2,000 ms control: 120 callbacks, source frames 4–123 and
completed-input cursors 128–247, with no active preparation. Actual GPU load was
2,006.6 ms. The worst native callback was 10.685 ms; staging wait peaked at 5.665 ms.

The 600 treatment stopped at the first simulation-debt guard after 848 ms of
actual load, rather than completing the requested two seconds. Seventeen active
callbacks progressed from source frame 125 to 165 and cursor 249 to 289, with no
active preparation. The genuine IndexedDB recorder retained the incident:
`simulation_debt`, value nine, threshold eight, source frame 165. Its staging
snapshot observed two active/occupied slots and 462 queue-completion registrations
and deliveries. The 75.1 ms peak registration-to-delivery interval is wall time,
not pure GPU execution.

The preceding active callback and actual guard callback must remain separate:

| Retained row | Completed source steps | Published pending fraction | Native callback / staging wait |
| --- | ---: | ---: | ---: |
| 373 | 5 | 0.9836 | 78.09 / 49.99 ms |
| 374 | 5 | 0.9426 | 113.09 / 84.74 ms |
| 375 | 8 | 0.0007 | 158.985 / 111.28 ms |
| 376, actual guard | 0 | 0 after reset | 2.205 / 0 ms |

The default `FixedTickClock::Pause` policy accrues elapsed time, checks the floored
pre-consumption demand against eight, then consumes every admissible whole tick.
`menuDiagnosticSample` publishes `pending_ticks()` after that subtraction. A
successful sample is a fractional remainder below one tick. The guard observer
retains its triggering whole demand before the clock resets. The callback start
marker also precedes the later clock sample; it is not the exact tick timestamp.
The preceding callback's work contributes to the next tick's elapsed demand.

The frozen classifier checked whether the last three published fractions rose.
They did not, so its raw result remains `classified_pause` / `park_ring_four`.
That is a poorly targeted demand metric: a falling remainder cannot rule out
increasing pre-consumption demand. Because the actual guard row's timing hook is
after the incident cutoff, the legacy `pause_callback_*` fields select row 375.
They describe the preceding active callback, not row 376.

A separate source-informed interpretation reconstructs accepted pre-consumption
demand as completed steps plus the remainder, conditional on no step-loop break
or later clock reset. All 120 control and 17 treatment rows meet the retained
active-match, no-preparation and cursor/source-step agreement checks. The last
three accepted demands are 5.9836, 5.9426 and 8.0007 ticks; the next incident
independently records whole demand nine. This supports increasing demand near
an induced staging-wait-dominated callback. It is not a strictly monotonic tail
or a persistent whole-tick backlog under the pause policy. Exact floating demand
at the guard was not retained.

## Ring four: observed nonreproduction, frozen comparison inconclusive

A separately approved one-shot ring-four comparison used the same unchanged
31-file build, inputs and fixed workloads. Four slots were explicitly selected
and verified before capture and unload. Both logical windows completed 2,000 ms
and 120 callbacks. Actual 300/600 load intervals were 2,022.3/2,018.2 ms; source
ranges were 2–121 and 124–243. Neither interval had staging wait or a timing pause.
The 600 window's worst native callback was 7.79 ms and maximum reconstructed
accepted demand was 1.9662 ticks. The actual recorder export had no qualifying
incident; this means none was generated, not a failed persistence claim.

The separate comparison limits were frozen before this run: corresponding
calibration/window batch means, p95 and batches per enabled second within
±20%; initial source/cursor offsets at most three ticks; and common 600-work
source frames 133–165 / cursors 257–289 covered. These are experiment comparability
limits, not runtime accuracy tolerances.

Exactly one of twelve metric checks failed: 300-control batch p95 was 28.6 ms
versus ring two's 21.9 ms, a ratio of 1.305936. All 600-work metrics, source offsets
and common coverage passed. The raw legacy result remains
`clean_nonreproduction` / `park_ring_four`; the separate source-informed comparison
is **inconclusive**. The control miss is not relaxed retrospectively, and there
was no tuning or rerun. The observed absence of staging wait is useful diagnostic
evidence, but it does not establish a comparable-load causal fix.

## Provenance, validation and remaining scope

Capture reports bind the original harness commits and trees, not the rebased
review branch. Prospective inventory equality proves served-byte identity; it
is not a native binary producer attestation. Those bytes also differ from the
older timing build. The recorder's producer source/runtime identity fields are
unknown; report manifests provide a separate external binding. Its recent-event
history is bounded and records eviction, while the pause-hook table has no
capture errors, dropped rows or incident overflow. No claim expands either
recorder's coverage.

Both completed attempts verified all 31 HTTP identities before and after against
their frozen manifests. Load was off before native snapshots, owner unload and
large exports. Unload completed, owned Chrome/server closed, and the owned shared
marker was released. Ring four's pre-unload snapshots had no error. The unchanged
preexisting desktop Chrome was separately recorded. Declared GPU buffer
reservation is 126 MiB for two slots and 252 MiB for four; the additional 126 MiB
is not measured physical allocation or residency.

Only the new harness commits were rebased onto the main revision containing
[#167](https://github.com/ericvanlare/melee-web/pull/167). Current main also has
independent network-session seams absent from the captured source. This PR changes
no native/runtime code or canonical Aurora patch; no new native build is required,
and no capture is relabeled as evidence for the rebased runtime. The full integration suite
passed: 1,924 tests in 348.912 seconds, with 141 skips. The ten focused staging
checks, harness syntax check and offline reproduction also passed. The receipt
binds validation logs separately from the historical browser runs. Its tracked
offline tool reproduces the unchanged original source-informed result and metrics.

The packet audit showed the exact-recipe follow-up was never launched and that the
earlier staging observations were correlations, not a guard/completion trace or
an established natural cause. A fresh producer-bound Release diagnosis then used
that exact MWRC v8 recipe and disc with the default two-slot ring and no injected
load or stall. It reached its cursor limit and crossed the previously reported
pause identity without a timing/runtime incident. The
[H1 clean-prefix entry](2026-10-06-h1-clean-prefix-visual-followup.md) and
[receipt](../evidence/h1-248fe763-clean-prefix-v1.json) retain the scoped result.
The run is a clean prefix only: it does not reproduce or explain the earlier
pause, and its independent local/HTTP file maps do not verify bytes consumed by
Chrome or the Wasm filesystem.

Review found the retained stopped screenshot black; its old
`stopped_scene_visual` field meant artifact/viewport/GPU presence only. The next
changed hypothesis is a short paired screenshot reducer: stop after a positive
match source-frame callback near cursor 1600, verify the source remains stopped,
then capture immediately before trace finalization and after the existing
trace/GPU-query delay. It must not resume, advance or redraw the source. If both
images are black, the presentation cause remains unknown and this boundary stops.

A separate original CSS → SSS → Mario-versus-Mario four-stock Final Destination
→ Results → CSS natural run remains necessary for the acceptance gap, with
separately declared callback/gap/audio criteria. Foreground, physical-input and
retail gates remain open; no timing-pause fix or default ring change is claimed.

[H1's natural original-menu match](../ROADMAP.md#current-priorities),
[the accuracy contract](../ACCURACY_CONTRACT.md), and the
[performance playbook](../PERFORMANCE_AND_ACCURACY.md) retain their separate gates:
zero natural timing pauses, callback/gap and audio limits, original comparison,
foreground timing, physical input and the full route remain unaccepted here.

To reproduce the **offline assessment only**, set `H1_RUN_ROOT` to the retained
run directory and choose an absent output filename:

```sh
python3 scripts/agent_workspace.py run -- \
  python3 scripts/assess_staging_ring_sustained_comparison.py \
  --baseline "$H1_RUN_ROOT/ring2-sustained-attempt-2" \
  --candidate "$H1_RUN_ROOT/ring4-sustained-attempt-1" \
  --proposal "$H1_RUN_ROOT/ring-four-comparison-proposal-v1.json" \
  --out "$H1_RUN_ROOT/new-offline-assessment.json"
```

This command reads existing reports and traces; it does not start a browser.
