# UCF input-history lifetime and rollback ownership

**Source identified / Reference compiled / State observation unrun**

[Milestone #339](https://github.com/ericvanlare/melee-web/issues/339) binds the
shared UCF Pad Buffer/Cardinals history before its dependent behaviors are
implemented. The source describes four 12-byte rows with 40 meaningful bytes,
indexed by the processed PAD slot. The installed table address, match reset,
fixed snapshot membership and restore behavior still need direct evidence.
The [structured receipt](../evidence/ucf-history-ownership-v1.json) keeps exact
source, failure, review and build identities.

## Reference source and build

The pinned Slippi Dolphin source and all 30 recursive submodules now have
independent, exact-commit local object stores. Six ordered downstream patches
match their expected source trees. Independent source review passes; all 31
seed indices are unchanged. The first copy/repack failure on incomplete Git
ancestry remains retained. The reduced file-only shallow transfer preserves the
actual pinned commit without inventing historical objects.

The earlier resource mismatch is withdrawn: the actual pinned tree-hash helper
reports identical source, staged and locked Sys resources across 750 files.
That does not resolve the historical executable mismatch or supply its missing
build receipt. A fresh source-bound arm64 NoGUI build now passes using installed
pinned tools, the locked build options, offline Cargo and process-local network
denial. Its executable and Rust library have their own hashes; neither is
relabeled as the old locked artifact. All 16 build commands passed, pre/post
source verification agrees, and all 16 owned process groups are absent.

## Next boundary

No client was launched or staged. The next experiment must review the actual
offline initialization entrypoint, selected patch bundle and observed memory
addresses under a client-specific default-deny policy. The previous synthetic
isolation fixture and this build are prerequisites, not runtime admission.
No official Slippi service traffic is permitted. Stop at the first source,
isolation or observation mismatch; preserve it and reduce before another long
attempt. No history-dependent behavior or complete-profile claim follows from
this checkpoint.
