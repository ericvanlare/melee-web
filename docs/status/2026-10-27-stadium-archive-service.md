# Stadium C5: archive transfer and source teardown

**Source identified / Focused source controls passed**

Current boundary: PR #346, #348, and #350 are merged with exact-head CI passed; historical local regression failures and their repairs remain scoped below. On runtime producer `38ecb6b1`, the reviewed three-process guard, ordinary/diagnostic builds, integrated source control, and conditional first-Fire A+B Results/CSS route pass; independent raw review gives scoped GO. The final test-only head `57714af2` passes the full regression (2,990 tests, 211 skips). Per-owner RNG, general timing, other forms, exact-head CI for the current test head, C5 acceptance, and whole-stage acceptance remain open; ordinary/public transformations stay disabled.

[Milestone #345](https://github.com/ericvanlare/melee-web/issues/345) composes the exact-disc transfer, source-file service, and Stadium map-2 writable-region owner. The [evidence record](../evidence/issue345-stadium-archive-service-v1.json) keeps service, compatibility, build, and regression identities separate.

PR [#346](https://github.com/ericvanlare/melee-web/pull/346) merged as `3fd2ed878bbb9a149b79e830701762887410a0a2`; its reviewed tree `88ac98b1c482b7062c33bc490eeba11cf8e95481` matches the merged tree. Exact-head CI run `40540770417` completed all 14 checks: 12 passed and two expected skips. The merge gate is recorded in `runs/stadium-owner-parity-20261026-150215-d5a0bac0/c5-merge-gate-v1/`.

The C5 service implementation is at 9d9d53b6; its five-file compatibility repair was committed as 3639903a. The composed service controls, ordinary and Stadium-diagnostic Release builds, 23 focused compatibility controls, and public/audio-preview Release builds passed at their recorded source revisions. The repaired 3639903a full regression passed 2,973 tests with 211 skips; output is retained at `runs/stadium-owner-parity-20261026-150215-d5a0bac0/c5-archive-service-full-regression-v2/`. Those results are separate from the later #347 source-slot work.

The #347 source-slot and private-entry metadata focused runs were first executed on staged composition `35b50ab`, before rebasing to `e4879796`. Their nine tracked source/test hashes match the corresponding e487 files; the runs were not executed at the e487 commit. The first metadata fixture failure is retained and the corrected v2 control passes. Ordinary and diagnostic Release builds passed at e487. The e487 full regression was deliberately stopped after 232.29 seconds on the shared-map0 defect, with the owned group absent; it is not a completed suite result. These records remain at `c5-slots-focused-v1/`, `c5-slots-focused-v2/`, `c5-slots-build-v2/`, and `c5-slots-regression-v2/`.

Independent review found that each form map contains shared `map0`, but original first-resident lookup supplies `map0` from base slot 0. The old release check attributed a live base Ground0 to each form-local map0 and refused unrelated form release. The exact repair now uses checked first-provider lookup and preserves the lease, header, registry, file-scope, and stale-row guards. A retained source-only reducer reproduces the refusal on e487; the repaired actual-source control passes on `3e0a533c`, keeping base Ground0 live while requiring a real form Ground9 consumer to remain protected. The former HOLD is closed by `c5-slot-final-review-v1/closure-v1.json`; the original HOLD receipt remains preserved. The focused result is in `c5-slots-focused-v3/`.

Ordinary and diagnostic Release builds also passed at `3e0a533c` (`c5-slots-build-v3/`). The full regression on that source completed 2,975 tests with 211 skips and one failure: `test_gameplay_patch_canonical` rejected patch byte ordering. The canonicalizer then rewrote only `patches/melee-gameplay.patch`; the applied source tree remained `13ef3f889b0e3a6ffd5c52feffefe47bfd5c92ad`. The 15-test canonicalization module passed. At that historical point, exact-head CI was pending and the full suite had not been rerun after the representation-only normalization. Later PR #348 and PR #350 exact-head CI passes are recorded on their own merge heads, not as a rerun of this local suite. See `c5-slots-regression-v3/` and `c5-slots-canonical-v1/`.

The source-slot code binds the exact ordinary source row, source header, backing-buffer extent, allocation lease, and selected form identity, retaining its matching native archive-section handle as a separate adjunct. The source `map_head` graph remains borrowed. The private `lbFile_8001634C` entry bridge resolves logical length through the active RuntimeFiles registry; invalid or inactive private IDs restore interrupt state and fail instead of falling through to `DVDFastOpen`. Those source-slot controls alone did not exercise a live disc request or World transformation; the later conditional first-Fire route is recorded separately below.

The original four-form asset probe used `c56161ea` plus the exact five-file overlay pinned at `c5-form-assets-reduced-v1/freeze-v1.json`, not a clean c561-only checkout. A later composed probe/build at `3e0a533c` plus canonical patch bytes `db35d73d…` again found four hydrated `map_head` records and 123 public names, refused wrong-ordinal lookup and close with a live handle, and reported no source transform. Both probe receipts are linked from the evidence record. Neither probe validates lighting, pipelines, textures, active World wiring, or transformation behavior. The phase-6 raw map2 clear remains distinct from an ordinary source archive row and does not free an archive header or native adjunct; the native ABI fixture does not establish the original Wasm clear extent.

`DVDFastOpen` remains an explicit STOP for ordinary DVD-backed entries. The conditional first-Fire route now exercises the selected `World` path and checked graph retirement; a general completion rule and other-form retirement remain unproved. These results do not enable ordinary/public transformations or close checkpoint 5. The earlier four regression failures, initial metadata fixture failure, e487 shared-map0 stop, and canonical representation failure remain retained with their repairs or disposition.

The first PR #348 CI run (`40552013069`, head `e71533c6`) failed only the new metadata fixture in unit-1: strict C11 on Linux did not expose the POSIX `strnlen` declaration. The other unit shard and six runtime partitions passed; the aggregate correctly failed. The reviewed fixture-only repair adds `_POSIX_C_SOURCE=200809L`, preserving C11, `-Werror`, all assertions and production bytes. Its local focused check passes; the failed CI log is retained. The repair subsequently passed the complete exact-head CI recorded below. A passing source control, build, or asset probe does not demonstrate a live archive transformation or C5 acceptance.

PR #348 merged as `b10ef03b` after the reviewed Linux fixture repair and all
14 checks on exact head `bd4a025c` (12 successes, two expected skips;
[run 40554121745](https://github.com/ericvanlare/melee-web/actions/runs/40554121745)).
Its tree `816d55ad…` exactly matches the reviewed tree. This closes #347's
archive/slot component. #349 still owns the first live transformed graph; this
merge does not activate transformations or resolve the general completion model.

PR #350 merged as `3ddc98b7` after all 14 exact-head CI checks passed (12
successes and two expected skips; run `40559928170`). The merge tree matches
reviewed head `a1aebaa2`. This adds passive original request, callback and
loaded-pending-poll observation; it does not supply a general DVD completion
rule.

The first default-off #349 Fire graph browser attempt used the reviewed original
request-clock recording (`c389eded`) with runtime source `cffe173a`. Exact
source/runtime/input pins and owned-process cleanup passed, but B stopped at
world tick 4349 on the first-fire phase/slot/source-graph guard. The retained
strict prefix comparison covered 22,446 rows and 9,078 actions with no prefix
mismatch, including 4,264 B state samples; that prefix is not a completed route.
B did not close its window, retire, enter Results, or return to CSS. The whole
route failed and `native_retired` remained false. The first-failure receipt,
raw report, rows and input/runtime pins are retained under
`runs/stadium-owner-parity-20261026-150215-d5a0bac0/c5-first-fire-browser-capture-v1/`.
The runner and first-Fire source composition received scoped source-review GO
receipts, not runtime acceptance. C5 and transformation acceptance remain open.

Source reduction for the first-fire guard found that the original Ground
constructor installs two Ground processes and Fire adds a third process; the
old guard incorrectly required a single node. The exact-chain predicate repair
was independently reviewed and applied at `38ecb6b1` (tree
`2077bb4c`), rebased only onto merged PR #350. The review covers the authored
three-node chain and keeps callback, priority, owner, and tail checks.
The ordinary and diagnostic Release builds plus integrated actual-source
three-process chain control pass at `38ecb6b1` (one test, zero skips). The
second bounded capture completed the declared first-Fire route on the same
source; independent raw review returned scoped GO for this conditional component. It compares
the declared 14 source fields plus seed, four-port PAD and action order. It does
not compare per-owner RNG or establish general timing. The first graph-guard
failure remains preserved. Independent raw review verified all 106 manifest
pins and returned `GO_SCOPED_CONDITIONAL_FIRST_FIRE_COMPONENT`; it keeps per-owner
RNG, general timing, other forms, and C5 acceptance out of scope. Receipts:
`runs/stadium-owner-parity-20261026-150215-d5a0bac0/c5-first-fire-proc-chain-repair-v1/`,
`runs/stadium-owner-parity-20261026-150215-d5a0bac0/c5-first-fire-proc-chain-build-v1/`,
`runs/stadium-owner-parity-20261026-150215-d5a0bac0/c5-first-fire-browser-capture-v2/`,
and `runs/stadium-owner-parity-20261026-150215-d5a0bac0/c5-first-fire-browser-capture-v2/independent-review.json`.

A later full-suite attempt stopped after 106.60 seconds on a stale textual fixture
assertion after the reviewed helper moved the callback check into the exact
three-process predicate; no runtime failure was inferred. The reviewed test-only
repair at `58d705a1` passed its two focused cases. Its full regression then
finished with eight additional stale fixture failures; those outputs remain
retained. The seven-file compatibility batch restores the modeled fields and
scheduler declarations required by the actual extracted source and updates exact
Node test counts. Assertions and production bytes are unchanged. Its first
focused batch passed seven cases and failed one on a fixture newline; the repaired
single case passed separately.

The integrated final regression on test-only head `57714af2` passed all 2,990
tests with 211 skips in 623.445 seconds (624.114 seconds including ownership).
The checkout was unchanged and the owned process group was absent with no cleanup
signals. Runtime/build/browser producer `38ecb6b1` remains distinct. Exact-head CI
for the eventual PR head is pending. See the evidence record for hashes and
`runs/stadium-owner-parity-20261026-150215-d5a0bac0/c5-first-fire-full-regression-v1/`,
`c5-first-fire-full-regression-v2/`, `c5-first-fire-regression-fixture-batch-v1/`,
and `c5-first-fire-full-regression-v3/`.

The next bounded graph is [Water #351](https://github.com/ericvanlare/melee-web/issues/351):
first observe its actual original callback timing, then admit its source-created
parent and children with checked deferred joint binding and nested teardown.
Fire's passing route will not be repeated without a new hypothesis. Ordinary
transformations remain disabled and C5 remains open.
