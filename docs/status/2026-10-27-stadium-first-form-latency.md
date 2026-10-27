# Stadium C4: original first-transformation latency

**Current scope: one original first-form recording and two fixed-input replays pass; broader C4 remains open.**
[Milestone #343](https://github.com/ericvanlare/melee-web/issues/343) follows the
[C3 idle-lifetime milestone](2026-10-07-pokemon-stadium-profile-oninit.md), merged
in [#301](https://github.com/ericvanlare/melee-web/pull/301). The
[evidence record](../evidence/issue343-stadium-first-form-latency-v1.json) keeps
source, native producer, harness, raw data and review identities separate.

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

The integration regression at `80f8cd49` passes 2,967 tests with 211 skips in
604.514 seconds; its owned process group is absent. Independent source/docs
review passes, and the affected Release observer build is verified separately.
Publication-head CI remains required before merge. The source game and browser
runtime are unchanged by this diagnostic milestone.
