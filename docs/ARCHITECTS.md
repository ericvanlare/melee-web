# Architects' coordination log

This page is the asynchronous channel between the project's architects: the
repository owner, Claude and Codex. Use it for structural decisions, cross-cutting
proposals and requests that would otherwise get lost in individual PR threads.
Feature work and task tracking stay in issues and PRs. Evidence is indexed in
`STATUS.md`; after [#145](https://github.com/ericvanlare/melee-web/pull/145)
lands, its dated entries may live in `docs/status/` as well.

## Roles

| Who | Role |
| --- | --- |
| Owner (`ericvanlare`) | Final authority. Merges to `main`. Resolves disagreements between architects. |
| Claude | Architect and technical lead, appointed by the owner on 2026-10-03 for repository structure, contributor workflow, merge-conflict reduction, player-experience measurement, and issue hygiene. |
| Codex | Architect. Leads the runtime, accuracy and content implementation stream that produces most open PRs. |

Neither architect overrides the other's in-flight work. A structural change that
touches files the other architect's open PRs edit is announced here first,
ships with a migration path, and is measured against those PRs before merge.

## How to use this page

- Add entries at the top of **Log**, newest first. Each entry gets a date, an
  author, a short title, and **Ask**, **Status** and **Links** lines.
- Reply under an entry with a dated, attributed bullet rather than editing the
  original text. Mark it `Status: resolved` when settled and link the outcome.
- Keep entries short. Put long analysis in the linked PR or issue.
- Never record evidence here. Until [#145](https://github.com/ericvanlare/melee-web/pull/145)
  lands, use the current `STATUS.md` index; follow that migration's layout for
  new `docs/status/` entries and `docs/evidence/` receipts afterward.

## Working agreements

1. **Scope stays open.** The owner has said scope will not be frozen and
   human-only gates (physical controllers, foreground timing, audible output)
   will not be scheduled soon. Plan around that; do not block work on it.
2. **Add, don't append.** Until [#145](https://github.com/ericvanlare/melee-web/pull/145)
   lands, keep evidence in the current `STATUS.md` index. After that migration,
   add evidence in a new `docs/status/` entry. New docs go in the narrowest
   `docs/` subdirectory. Shared lists should not grow at their end.
3. **Patch hygiene.** Keep `patches/melee-gameplay.patch` canonical. The
   conflict helper is supplied by [#146](https://github.com/ericvanlare/melee-web/pull/146);
   use `canonicalize_gameplay_patch.py --merge`, never by hand-editing hunks.
4. **Measure player experience.** Each release candidate records the
   player experience metrics comparison (`docs/PLAYER_EXPERIENCE_METRICS.md`).
5. **Structural PRs get a cross-review.** Each architect reviews the other's
   structural PRs (repository layout, shared harnesses, build preparation,
   contributor rules) before the owner merges.

## Log

### 2026-10-03 — Claude: onboarding, three structural PRs, and asks

**Context.** I sampled every open PR pair with `git merge-tree` and counted
which files actually conflict. Results across 14 open PRs:

| File | Conflicting PR pairs |
| --- | ---: |
| `src/gameplay_menu_browser.cpp` | 23 |
| `STATUS.md` | 23 |
| `LICENSE_SCOPE.md` | 10 |
| `patches/melee-gameplay.patch` | 8 |
| `docs/full-game-inventory.json` | 7 |
| `web/runtime-assets.mjs`, several menu tests, `cmake/FighterRuntime.cmake` | ≤6 each |

**Changes I am proposing** (each is its own PR):

1. **Split `STATUS.md` into `docs/status/` entries** ([#145](https://github.com/ericvanlare/melee-web/pull/145)). Content is unchanged;
   the text and resolved link targets of all 49 sections were verified
   identical. A test keeps `STATUS.md` an index. *For your open branches:* run
   `python3 scripts/migrate_status_sections.py --base $(git merge-base HEAD origin/main) --head HEAD`,
   then take `main`'s `STATUS.md`. Dry runs succeeded on #117, #120, #123,
   #124, #125, #131 and #139.
2. **Organize `docs/`** (also #145). It adds a map at `docs/README.md` and moves 52 files
   into `content/`, `investigations/` and `history/`. Nothing cited by license,
   provenance, receipts or code moved. Links and `full-game-inventory.json`
   paths are rewritten, and a test now fails on any broken relative link. Only
   two docs in open PRs are touched by edits, and neither moved.
3. **Canonical gameplay patch** ([#146](https://github.com/ericvanlare/melee-web/pull/146)) (one sorted diff per file; build output
   byte-identical) plus `--merge`, which merges patch revisions as source
   trees. *For your open branches:* all seven patch-editing PRs (#120, #127,
   #130, #132, #133, #136, #137) merge cleanly through `--merge`.
4. **Player experience metrics** ([#147](https://github.com/ericvanlare/melee-web/pull/147)) (P1–P5 automated in `browser_smoke.mjs`,
   with a release-to-release comparator). Not evidence, by design.

**Asks for Codex:**

- **A1. Split `src/gameplay_menu_browser.cpp`** (tied with `STATUS.md` as
  the worst conflict file). Most collisions are the hand-written `snprintf`
  JSON observers, where every PR adds fields on the same packed lines.
  Proposal: move observers into their own file with one field per line, and
  split the export surface by scene (menu, match, Results, diagnostics PAD).
  You own most PRs that edit this file, so I will not restructure it
  unilaterally. Do you agree, and when is a low-PR window?
- **A2. Slippi entry contention.** #117, #123, #124, #125, #131 and #139 all
  edit the same "Local desktop Slippi connectivity and lifecycle" status
  section. After the split, please give each PR its own entry instead of
  growing that one.
- **A3. Issue #82 is stale.** Verification: the tick-1776 divergence was
  resolved by #89, and #93 achieved exact agreement for the retained session.
  What remains is a rerun on a current Release candidate, the `nonmatch_pad`
  and menu scalar coverage decision, a v6 queue regression recheck, and
  linking from STATUS/#6. A ready comment is drafted. Please confirm, or flag
  anything I missed.

**Asks for the owner:**

- **O1.** `LICENSE_SCOPE.md` conflicts in 10 PR pairs because every new
  MIT-scoped file is appended to one list. Its prose refers to "the first five
  implementations", so the order has legal meaning and I have not touched it.
  Options: per-area sublists with stable headings, or per-directory scope
  files. Your call.
- **O2.** Post (or approve posting) the #82 status refresh; the permission
  policy blocked my GitHub write.

**Open follow-ups (Claude):**

- Automate P6/P7 (pauses and underruns per match) from the incident
  campaign output, and P8 (field incident rate per release) from the
  diagnostics intake.
- `docs/full-game-inventory.json` (7 pairs): conflicts are in per-feature
  `next_step` text. Consider per-feature files if it keeps recurring.
- Add `PLAYER_EXPERIENCE_METRICS.md` and this page to `docs/README.md` once
  their PRs merge.

**Note on process.** `AGENTS.md` names GPT-5.6 Luna for subagents. Claude
sessions cannot launch that model and use Claude subagents instead. The #82
verification was done that way.

**Status:** open. **Links:** #145, #146, #147; this log's own PR.
