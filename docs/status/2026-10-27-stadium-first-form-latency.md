# Stadium C4: original first-transformation latency

**Current scope: reviewed water contexts and a new original fire-form recording with request/callback/poll clocks; broader C4 remains open.**
[Milestone #343](https://github.com/ericvanlare/melee-web/issues/343) follows the
[C3 idle-lifetime milestone](2026-10-07-pokemon-stadium-profile-oninit.md), merged
in [#301](https://github.com/ericvanlare/melee-web/pull/301). The
[evidence record](../evidence/issue343-stadium-first-form-latency-v1.json) keeps
source, native producer, harness, raw data and review identities separate.

PR #344 merged as b041dcefff1f96a73f5b4afc9972e48250d4ba91. Its tree
ebac35fcb62841cb37135aefa179f3f0d5043d85 exactly matches the reviewed PR
tree; the reviewed PR head was d4a7280b3a4ebd9e4633808ea231612628083543.
The exact-head CI run completed successfully on attempt 2: 14 checks, 12
successes and two expected skips. The initial unit-1 and menus jobs failed
before their test partitions started when the pinned FreeType archive transfer
ended with HTTP/2 receive code 56 after HTTP 200 at 2%. A separate fetch
matched the existing 4,134,916-byte SHA-256 pin, so only the failed jobs and
dependent aggregator were retried; no source change was made. The retained
diagnosis and merge gate are in c4-merge-gate-v1.

One original whole-three recording reaches form 9 and its checked replacement
root, then completes the existing Results/CSS route and native input footer.
Independent replay of the raw observations verifies the actual four-port inputs,
source order, original issue journal and selected window. The first loading phase
runs from source tick 3689 to 3700 (11 ticks); phase 6 appears at 4305 and the
replacement settles in phase 0 at 4306. The fresh neutral issue at 3689 is consumed
at 3690. These are observed source coordinates, not fabricated input or a timing
acceptance result.

The same raw stream yields phase durations 11, 1, 302, 181, 121 and 1 source
ticks for phases 1–6. This derives from observed phase transitions, with the
raw identity and decoder hash retained in the evidence record.

Runtime observer producer `64620661` and capture harness `5103583b` are distinct.
The capture completes in 139.442 seconds under the unchanged limits; its owned
processes are absent and its empty disposable launch directory is removed.
The actual prelaunch configuration explicitly sets `FastDiscSpeed=False`.
Dolphin's postexit INI serialization differs, but independently compared settings
are unchanged. The initial reviewer byte-equality failure remains retained.

The replay observer is a separate producer, `841305f3`, with host harness
`2fa481a2`. Independent review rejected the first driver for checking metadata
on the wrong status stream and reconstructing input issue timing at host-drain
time. The repair applies historical issues at exact actual source rows and
keeps input completion separate from observer metadata. Focused checks pass;
the original recording and binary remain unchanged.

The first fixed-input replay observes the same loading and first-form timeline.
Its native route and input footer complete, but the live report fails when a
generic exporter requires scene slices explicitly omitted by this diagnostic.
The failed report stays unchanged. An independently reviewed supplemental
postflight verifies literal inputs/profile, actual PAD/tick/draw order, all
Entry/Setup seeds and owners, full native terminal, the declared compact window,
historical input issues and owned-process cleanup. The repair is `cba847f0`;
no recapture was needed to recover those checks.

The second planned exact-input replay passes live and independent raw review,
including whole-three terminal, complete input footer and owned cleanup. All
three water samples measure 11 ticks and have equal first-form/owner summaries.
No further replay of this unchanged case is planned.

No constant delay is justified
for other forms: file range, queued music reads and emulated drive state affect
completion. A broader scheduling rule, other form samples/distribution, C4
completion, browser parity, public admission and whole-stage acceptance remain
open. The incomplete historical water prefix is a different input/context and
cannot establish variability under fixed input.

The pre-merge full regression passed 2,967 tests with 211 skips at the reviewed source tree, and the affected observer build passed separately. The C4 diagnostic did not change the source game or browser runtime. The merged milestone remains scoped to the recorded form and fixed-input replays; other forms, a general completion rule, browser transformation support, public admission and whole-stage acceptance remain open.

## Changed context and fire request clocks

The named menu-wait72 context completed the unchanged whole-three route and
selected water again. Its loading interval is source tick 3820 → 3832 (12 ticks),
compared with 11 in the earlier context. Independent review found no missing
draw in either interval. Native producer `841305f3`, capture harness `ee3c8907`
and the equivalent rebased harness `c56161ea` stay distinct. The first attempt
was refused for a stale native identity before launch. This is a different
recorded input context, not variable latency under fixed input; no repeat is
needed.

The next recording added passive original request, callback and loaded-pending
poll observations. It naturally selected **Fire (map 3, GrPs1.dat)**, observed
the real request at source tick 3853, callback and successful poll at 3864,
and settled the replacement at 4470. The source request is file entry 420,
327,617 logical bytes and a 327,648-byte transfer. The full three-match route,
literal four-port PAD checks, complete input footer and owned-process cleanup
pass. The fixed selected window and its separately checked natural tail remain
distinct. This is another fresh input recording, not an exact-input replay of
the water context.

The original emulated clocks show 72,668 ticks inside the request call,
84,808,264 from request entry to callback entry, and 4,739,233 from callback
entry to successful poll return. These are **CoreTiming ticks**, not source
ticks or wall-clock time. The decoder uses the pending bit actually loaded by
the original poll, preserving possible callback interleaving. These observations
do not establish a general DVD completion model or a universal 11-tick delay.

Both native producer and capture harness are `c389eded`; the later rebased
regression source is `70d218cb`. The binary is `895530a9…`, and the exact
manifest, source archive, decoder and raw hashes are in the existing evidence
record. Independent review replayed the strict clock, full primary, PAD/phase,
compact-RNG and input-footer checks from the immutable capture source. It
streamed the allocation hash; the collector's eight-endpoint allocation
semantics are retained, not relabeled as a new full allocator audit.

The affected native build and focused fixture repair pass. The initial integrated
focused failures are retained: four extraction fixtures omitted the new actual
observer member/helper/table, then passed after those fixtures were updated.
The first full suite at `70d218cb` ran 2,982 tests (211 skips) and retained one
failure and one error: two additional extracted fixtures omitted timing
declarations/helpers. The independently reviewed fixture-only repair passes
both affected test files. The final full suite at `54bb57c7` passes all 2,982
tests with 211 skips; owned validation processes are absent and successful
disposable scratch is removed. Exact-head CI and integration remain pending.

The next runtime experiment is the first original Fire graph under #349, using
this recording's authenticated completion boundary as explicitly conditional
evidence. General completion scheduling, other forms/cycles, browser parity,
public Stadium admission and whole-stage acceptance remain open.
