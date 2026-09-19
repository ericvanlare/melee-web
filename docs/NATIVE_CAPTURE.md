# Native System Trace capture

`scripts/native_capture.py` is a small macOS diagnostic tool for System Trace
captures. It makes a successful owned-process readiness, export, and short
observed-range check a precondition of a longer capture; it does not prove
that the full requested window contains target events and does not turn a
trace into gameplay or performance evidence.

The tool records the installed `xctrace` version and option output in each
receipt. On the reference machine, `xcrun xctrace record` has no `--help` flag;
its usage output is printed by running `xcrun xctrace record`. The relevant options are
`--template`, `--attach`, `--time-limit`, `--window`, `--notify-tracing-started`,
`--no-prompt`, and `--output`. Export uses `xcrun xctrace export --input ...
--toc` and an XPath query for the `thread-state` table.

## Preflight

Choose a new ignored evidence directory for each run. The directory must not
already exist. The command starts a short-lived Python workload owned by the
command, attaches System Trace to that PID, stops the recorder after the probe
interval, exports the TOC and target thread-state table, and validates all of
the following:

- xctrace starts and emits its readiness notification within the bound;
- the recorder stops within the bounded SIGINT → SIGTERM → SIGKILL cleanup
  sequence;
- both exports produce nonempty XML;
- the TOC names the owned probe PID and reports the exact requested window; and
- exported target rows retain at least the requested minimum time range.

For example:

```sh
python3 scripts/native_capture.py preflight \
  --output work/native-preflight-2026-09-19 \
  --retention-window 90 \
  --probe-seconds 2 \
  --min-retained-seconds 0.5
```

On success, `preflight.json` is the hash-bound receipt. It binds the
template, exact retention window, scope, xctrace version, export mode, probe
identity, retained target range, and SHA-256 identities for every evidence
file. It also binds the observed macOS release and machine identity, and the
current xctrace helper identity. `configuration.json`, the xctrace command
transcripts, `native.trace`,
the exported XML, and the session log are retained beside it. A failed run
writes `failure.json` when possible and consumes that output directory; do not
rerun into it.

The scope is deliberately narrow: one owned non-game process proves recorder
readiness, bounded finalization, and retention/export behavior. It does not
prove a browser or GPU process is covered, that a later interval is inside the
trace, or that any gameplay callback is explained.

## Receipt validation and capture

Validate the exact configuration before attaching to a longer-lived target:

```sh
python3 scripts/native_capture.py validate \
  --receipt work/native-preflight-2026-09-19/preflight.json \
  --retention-window 90
```

The receipt is rejected when it is expired, modified, missing evidence, or has
an incompatible template, scope, or retention window. The preflight window is
the rolling retention setting; a later capture may use a longer `--time-limit`
while retaining that same validated window. The short probe's observed rows do
not establish that a later 300-second capture retains events for 90 seconds;
each capture must pass its own export and coverage check.

An explicit target capture then requires both a live PID and the matching
receipt. The recorder and its readiness observer are the only processes this
command cleans up. It never sends a signal to the explicit target PID.

```sh
python3 scripts/native_capture.py record \
  --pid 12345 \
  --output work/native-capture-2026-09-19 \
  --preflight-receipt work/native-preflight-2026-09-19/preflight.json \
  --time-limit 300 \
  --retention-window 90 \
  --stop-after 30 \
  --coverage-range 20:24
```

`--coverage-range START:END` is optional, but should be supplied for an actual
diagnostic interval. It uses trace-relative timestamps from the exported
System Trace run start; it is not a page-clock or source-frame anchor. When supplied, the
exported rows must cover the complete interval. Every record also checks its
own target PID, exact TOC window, nonempty target rows, and retained range, so a
successful profiler exit or trace file alone cannot pass the command.

The output is another immutable directory containing `capture.json`, the raw
trace, TOC, thread-state export, and command logs. A failed startup,
finalization, export, or coverage check leaves a failure record and returns a
nonzero status. The target process must be stopped by its owner after capture.

## Evidence boundaries

System Trace can contain kernel and other-process rows even when attached to a
single PID. Filter all attribution to the PID recorded in the TOC and capture
receipt. The tool's `thread-state` export is native scheduler evidence; it is
not GPU execution time, browser callback timing, source simulation state, or a
coverage claim for an unrelated process.

Keep raw failed records. A trace with a requested 90-second window can still
have only a short observed target range when recording stops early, and a
short owned preflight cannot retroactively provide coverage for a later game
failure. Correlate the target PID and exported range with independent page or
host clock anchors before interpreting a native interval.
