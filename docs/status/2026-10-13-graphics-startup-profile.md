# Public staging graphics startup profiling

**Browser observed / Source identified — startup attribution in progress**

Issue [#326](https://github.com/ericvanlare/melee-web/issues/326) follows the
owner’s reported 10–15 second “Preparing graphics…” wait on the public Pages
staging player, before disc selection. This is separate from #134’s post-disc
native menu preparation and from multiplayer transport.

The retained foreground Chrome reload used the existing browser profile,
without cache clearing or CPU/network throttling, with DevTools recording.
The deployed audio-player was source `07bcfdfa6f4531ac464eab44ebecb206daa1f0da`,
runtime graph `9c81b55e5bc2366d`, Pages deployment
`19a6ec7e-b035-4361-ad6e-43f275e4d3f5`. It is historical relative to the current
instrumentation candidate. The immutable deployment and public alias served
identical HTML during the inspection.

In sampled trace screenshots, the label was observed from 813.9 through
6,939.4 ms after navigation; the next sampled screenshot without it was at
7,017 ms. This brackets a warm reload observation, not the owner’s exact
reported attempt. Of 28 same-origin responses, 27 were marked cached,
including Wasm/data. Wasm cache-consumption markers finished by about 550 ms.
The renderer main thread was busy for about 8.7% of the sampled label interval.
The trace contains no direct WebGPU pipeline/shader events and cannot identify
which renderer readiness gate consumed the wait. Its 14.573-second recording
length is not startup duration.

The [scoped receipt](../evidence/public-startup-profile-v1.json) retains the
public build identity and derived trace observations.

Retained private run: `public-startup-profile-20261013-211719-304e5e58`.
The original gzip trace is unchanged. Its scoped derived summary is
`foreground-existing-profile-startup-1-summary.json`, SHA-256
`98728c0895b0728f5bedc7d28e0ac18bf688affe19446a277f84ee1946f06972`.
No source ticks or disc import were requested. The Chrome profile, docked
DevTools and instrumented capture are not a quiet-machine performance gate.

The bounded startup report is deployed on the ordinary audio-player at immutable
[e3dc446a](https://e3dc446a.webmelee-staging.pages.dev/) and the public staging
alias. Source `dcf6b06e794597b01bcc5d07c4dfb46f0e843018`, runtime
`2f3efb6678651310`, and producer/package identities remain in the scoped receipt.
The UI works before native startup and after an error, exports locally, and
records User Timing marks. The Release build, package audit, immutable HTTP
verification and two headless report/export checks pass. The first 2,400-test
regression retained one reduced-fixture failure (166 skips); its reviewed repair
passes focused controls. A corrected full regression is still pending.

Two native Chrome UI reloads on that deployed candidate took 6,597.915 ms and
4,627.905 ms from mount. Their native startup intervals were 5,697.145 ms and
4,539.575 ms; renderer initialization was 79.755 ms and 29.350 ms. Each retired
all 890 declared pipeline entries with queue zero and the original cache-idle
state `1`. The second reload recorded no long tasks. Exact exported reports,
cache conditions and observation limits are in the receipt. These observations
identify the pipeline queue as the gate holding readiness; they do not measure
backend GPU compilation completion or reproduce the owner's exact 10–15 s run.

The next bounded experiment decouples the same five-entry startup batches from
the sixth-render-callback cadence with one yielding 8 ms timer. Its ownership,
queue ordering and stop controls must pass independent review before a matched
capture. No speedup is claimed yet. The report and candidate preserve game
arithmetic, input, source order, readiness guards and timing deadlines.
