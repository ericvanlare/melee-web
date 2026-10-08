# Current status

This is the evidence index. Each observed result lives in its own entry under
[`docs/status/`](docs/status/), named `YYYY-MM-DD-<slug>.md` by the date the
evidence was first recorded. Entries link their scoped receipts under
[`docs/evidence/`](docs/evidence/) and keep the playbook's evidence labels.

List the entries newest first, with their evidence labels:

```sh
python3 scripts/status_index.py
```

## Adding or updating evidence

- **Add a new entry file** in `docs/status/` for new evidence. Do not add
  sections to this page. While all evidence lived here, every pull request
  that recorded a result conflicted with every other one that did.
- **Update an existing entry in place** when the same boundary gains a newer
  receipt. Keep superseded results as explicitly historical text, or mark the
  entry as superseded and link its replacement.
- Start the entry with a `# Title` line and, where applicable, a bold line of
  evidence labels (`**Compiled / Source identified / ...**`). Name the scenario,
  fields, source boundary, machine, browser, build and run inventory, and keep
  exclusions explicit.
- Edit this page only when the acceptance boundaries below change.
- A branch started before this split can move its STATUS edits into entries
  with `python3 scripts/migrate_status_sections.py --base $(git merge-base HEAD origin/main) --head HEAD`,
  then take `main`'s `STATUS.md`.

## Current acceptance boundaries

The supported CSS → SSS → Mario/Final Destination → Results → CSS route is
covered by the scoped receipts in `docs/status/`. Remaining acceptance is
independent across original comparison, live and physical input, visual output,
audio fidelity and sustained performance. The [roadmap](docs/ROADMAP.md),
[accuracy contract](docs/ACCURACY_CONTRACT.md) and
[performance/accuracy playbook](docs/PERFORMANCE_AND_ACCURACY.md) define those
boundaries; this section adds no new runtime or deployment evidence.

The [runtime-owned CSS-to-active-match prerequisite](docs/status/2026-10-08-a3-runtime-css-active-match.md)
is bounded local browser evidence; full A3 and whole-session acceptance remain open.
