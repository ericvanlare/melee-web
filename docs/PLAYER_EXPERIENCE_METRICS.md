# Player experience metrics

The accuracy evidence answers "does it behave like the original?". These
metrics answer a different question: "what does a player wait through and
notice on webmelee.gg?" They are tracked release to release so a regression is
seen before players report it.

A metric report is **never** accuracy, source-timing, performance-admission or
foreground-timing evidence. It is a wall-clock observation on one named machine
and browser. Keep it out of the evidence labels and out of `docs/status/`
admission claims.

## The metric set

| # | Metric | What the player experiences | Source | Automated |
| --- | --- | --- | --- | --- |
| P1 | Page → import ready | Time until **Disc** is usable after opening the page | `browser_smoke.mjs` `page_to_import_ready_ms` | Yes |
| P2 | Cold disc → original CSS | First visit: from choosing the disc to a running character select | `attempts[cache=cold].disc_to_css_ms` | Yes |
| P3 | Warm disc → original CSS | Returning visit in the same browser profile | `attempts[cache=warm].disc_to_css_ms` (`--repeat 2`) | Yes |
| P4 | Longest main-thread task during import | The worst single freeze (input and UI unresponsive) | `longest_task_ms`, `longtask_total_ms` | Yes |
| P5 | Memory at CSS | Pressure that leads to tab termination, especially on mobile | `wasm_heap_bytes`, `js_heap_used_bytes` | Yes |
| P6 | Unexpected pauses per match | Timing-pause incidents that stop play | Runtime incident recorder ([diagnostics](RUNTIME_DIAGNOSTICS.md)); `runtime_incident_campaign.mjs` | Match harness |
| P7 | Audio underrun increase per match | Audible dropouts | Runtime audio counters in the same recorder | Match harness |
| P8 | Field incident rate | Pauses, crashes and failures real players hit | Opt-in incident reports through Settings and the hosted intake | Field data |

P1–P5 come from the existing smoke harness. P6–P7 come from the bounded
natural-match tooling. P8 comes from the deployed diagnostics intake and is the
only metric that reflects real players' hardware.

## Collecting P1–P5

Serve the candidate over real HTTP and run the smoke with an owned disc and two
imports in one fresh browser profile:

```sh
node scripts/browser_smoke.mjs --url http://127.0.0.1:8787/ --surface public \
  --disc /path/to/owned.ciso --repeat 2 --out work/player-metrics/<release>
```

The report's `player_metrics` block records each attempt. The first attempt is
labelled `cold` (fresh profile: no HTTP, render-cache or driver warm-up from
this harness), and later attempts are `warm`. The GPU driver's own shader cache
is outside the harness's control. Report it as uncontrolled, as the incident
campaign does.

Compare a candidate with the previous release on the **same machine and
browser**:

```sh
python3 scripts/compare_player_metrics.py \
  work/player-metrics/<previous>/report.json work/player-metrics/<candidate>/report.json
```

The comparator requires one valid cold and one valid warm disc-to-CSS
measurement in each report. Optional long-task and heap fields may be
unavailable when the browser does not expose them.

It prints a Markdown table and exits 1 when a metric grows beyond both its
absolute and relative slack. That is a prompt to investigate before release,
not an automatic block. Paste the table into the release PR.

## Interpreting the numbers

- Headless runs are functional measurements. Use them for release-to-release
  deltas on one machine, not as absolute player numbers. Foreground timing keeps
  its own protocol.
- P4 includes known preparation work that blocks the main thread. A reduction is
  a real player improvement even when source timing is unchanged.
- P5 is a snapshot at CSS, not a peak. Mobile termination work should add a
  peak measurement before relying on it.
- A failed smoke produces no comparable metrics; fix or explain the failure
  first.

## Not yet automated

P6 and P7 per-match counts need a natural-match run with the recorder enabled;
the [incident campaign](../scripts/runtime_incident_campaign.mjs) already
collects them. P8 needs a periodic summary from the diagnostics intake (counts
per release and incident kind; no personal data). Both are open follow-ups,
tracked in the architects' coordination log (`docs/ARCHITECTS.md`).
