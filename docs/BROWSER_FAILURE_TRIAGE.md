# Browser failure triage

Summarize an existing browser replay output directory without running the
browser or recomputing replay comparisons:

```sh
python3 scripts/summarize_browser_failure.py \
  work/your-browser-run --out work/your-browser-run-summary
```

The command writes `summary.json` and `summary.md` into a new output directory.
It reads the browser report, progress snapshot, page log, failure text, and
session JSONL trace when present. Large JSONL traces are read one record at a
time; an incomplete final record is reported, while malformed interior records
are rejected. Source artifacts are read-only. The output must not exist and
must not overlap the input directory.

The report distinguishes the browser's recorded first error from harness or
cleanup failures, and leaves their relative order unknown unless the artifacts
establish it. It keeps the source cursor, session-frame index, match
index, and match frame separate; unknown values remain unknown. Fighter kinds
and motions remain numeric, and item/owner records appear only when present in
the retained diagnostics. Artifact links point to files in the selected run.

Exit status `0` means a report was generated, including for a failed or
incomplete replay. Exit status `2` means the input could not be summarized or
the output could not be safely created. The report is diagnostic only; it does
not establish gameplay correctness, completion, equivalence, or crash cause.
