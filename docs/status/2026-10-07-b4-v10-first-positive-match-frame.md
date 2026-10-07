# MWRC v10 first positive match-frame comparison

**Compiled / Source identified / Retail compared / Browser exercised**

One bounded comparison joined the retained GALE01r2 MWRC v10 source capture to
the separately frozen ordinary browser capture. It compared the match setup
and 125 contiguous match-0 ticks, from tick 0 through the first positive
`match_frame` at tick 124. The exact join was source sequence 4735, after PAD
consume sequence 4734, at recipe timeline index 1308 and browser cursor 1309.
No field differed through that boundary.

The source prefix began at entry sequence 4088 and included the setup at
sequence 4113 and first match tick at 4115. It consumed 4,736 records and
10,389,712 raw bytes through sequence 4735; the fresh prefix SHA-256 is
`24d520abccf98ee109b3cee2ae67767d4363c7e941d688c3534c0cbfdd7cde4a`. The
full 2,137,257,881-byte MWRO kept its previously recorded SHA-256; it was not
rehashed, and its before/after stat identity was unchanged. The browser wrapper
observed cursor 1311 and exported cursor 1314 (1,316 trace rows); the frozen
target join remained cursor 1309. Exported rows outside the semantic target
received provenance, ordering and shape validation, not original-state
comparison.

CSS frames 0–936 and SSS frames 937–1183 established consumed-input order only.
Their scalar state was not compared (`nonmatch_fields_compared` is empty). The
setup compared RNG, match frame, semantic PAD bytes, Fighter state, strict
primary fighter-entity identities and declared setup. Each of the 125 match
ticks compared RNG, match frame, semantic PAD bytes, Fighter state and strict
primary fighter-entity identities. Match timeline frames 1184–1308 are the
compared match prefix. This is a first-positive-frame boundary result, not a
claim about later gameplay.

The comparison reports `boundary_result: equivalent`, while its overall result
is `incomplete`, `complete: false`, and `whole_session_equivalent: false`.
CLI exit 1 is the expected status for this bounded result. The capture used
producer `940411fa13a17de796217b1dba5b4c338dd02b8f`; the accepted comparison
used producer `2e9dd41de2cf5644aec1367f9eec970a4bcee449`. After rebasing the same
five comparison commits onto main `52f735dbd9198d0aa570f7f52ed625f47ba90332`,
the integrated source is `039e0a328d2249ef7069fae3938675e6c080c9f8`. The portable
[receipt](../evidence/b4-v10-first-positive-match-frame-v1.json) records these
distinct identities and validation results.

Three preflight failures were retained. The first rejected the valid
requested/observed/exported cursor relationship because it required report
metrics to equal the wrapper cursor; exported-tail validation now checks those
counters separately. The second rejected a recorded runtime-data abort before
checking the complete-package diagnostic; the source-backed validation now
requires matching complete package evidence and does not infer the abort's
causal origin. The third rejected finite observation times across initial
navigation; the source-backed chronology check admits only the structurally
identified pre-cursor navigation prefix. None of these repairs suppresses
browser errors or upgrades an incomplete capture.

The integrated source passed the full Python suite (1,977 tests, 145 skipped)
and the official Release runtime build. The later integration build does not
change the runtime identity of the earlier capture. No new capture or
comparison was run during integration, and no whole-file MWRO or runtime-data
hash was computed. This result does not establish whole-session equivalence,
later gameplay, pixels, PCM, timing, performance or competitive readiness.
