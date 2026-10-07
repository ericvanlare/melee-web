# Ordinary MWRC v10 CSS prefix with repaired observer storage

**Source identified / Browser exercised**

One headless ordinary state-mode run of the exact full MWRC v10 recipe reached
native cursor 17 in original CSS, exceeding the requested cursor-10 stop.
The retained trace contains 19 contiguous CSS session records, indexes 0–18:
source callbacks continued briefly between the stop observation and unload.
This is a menu prefix, not gameplay or a completed session.

The [portable receipt](../evidence/b4-css-observer-repair-prefix-v1.json) binds
capture source `7cb680d` over `3c261`, the unchanged Release native/web producer
`190d150`, the full recipe, disc, artifact inventories, reports and cleanup.
The source repair allocates all fourteen native `int32` CSS observer outputs
instead of four, and frees a surviving allocation when the other allocation
fails. The observer's existing four-ID result shape remains unchanged.
All 32 existing Release artifacts matched the frozen producer before and after
capture, locally and over the owner's HTTP server; no rebuild was needed.
The machine was the shared Mac mini, using headless installed Chrome
154.0.8037.98, Playwright 1.62.1 and Node 24.19.0, with speaker output muted.

## Observation and incomplete result

Snapshots before unload progressed through cursors 0, 1, 14 and 17 in CSS.
The cursor-17 stop observation reported running 1 and no runtime error. No
pre-stop browser error, observation timeout or timing resume was retained.
The final snapshot reported phase 0, running 0 and cursor 0 after unload;
that reset is not the replay's terminal pre-unload cursor. The inspected final
screenshot shows the unloaded UI and a black canvas, so it is teardown evidence
rather than CSS pixel evidence.

The runner reported `incomplete` and exited 1, as expected for the deliberate
prefix. Its first error followed unload: the browser completion report rejected
the interrupted input timeline, missing final CSS and source tick/draw count.
These full-session failures remain retained; reaching the prefix does not waive
them. The inventory wrapper preserved child exit 1. The owner likewise returned
1 because its child was incomplete, while strict OS process observation, CDP
attribution and cleanup passed independently. All owned browser identities and
capture/server groups were absent, the port was free and the owned lane marker
was released. The owner interval was 11,812 ms; this is not a performance result.

The loaded `.data` package matched its frozen byte count and hash. The run used
18-second phase and 90-second replay bounds, 250 ms polling, five-second snapshot
and HTTP-request bounds, and the retained 300-second owner envelope with a
45-second cleanup reserve. It enabled neither callback-probe mode nor timing
resume. The focused checks passed twelve actual CSS-callsite cases, replay
lifecycle checks and capture-result checks. No full suite was run for this
repair in this task.

## Original comparison and next boundary

The existing strict whole-session comparator was run read-only against the
original MWRO and this full v10 recipe and trace. It returned `invalid`/exit 2:
`whole-session comparison requires MWRC v8 or v9`. It rejected the transport
before comparing state, so no original-state agreement or mismatch is claimed.
The failure report and comparator hashes are retained in the receipt. The
reference's non-match scalar-state limitations remain separate.

This run demonstrates ordinary replay progress with repaired CSS polling on
this producer. It does not prove that the observer overflow caused the older
`add492c` timeout: producer, Node and observation machinery also differ. The
[earlier comparison](../evidence/b4-add492-to-190d-review-v1.md) and
[first-callback result](2026-10-06-b4-first-replay-callback.md) remain unchanged.

The next recommended experiment is one ordinary prefix to cursor 1200 using
the same full recipe, state mode and repaired polling path. First integrate
current shared H1 changes and bind fresh source/runtime identities. The recipe places CSS
at indexes 0–936, SSS at 937–1183 and first match entry at 1184. Retain actual
scene order, the first match ticks, overshoot and first error; stop at the target
or first failure with no automatic resume or retry. Reuse the existing owner
and inventory wrapper. A v10-capable comparator needs its own source review
before an original-state claim. No further callback instrumentation is needed
for this next progress check, and no next run was performed here.
