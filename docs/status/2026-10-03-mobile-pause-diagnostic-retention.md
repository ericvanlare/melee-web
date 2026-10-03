# Mobile pause diagnostics across suspension and reload

**Compiled / Source identified / Browser exercised (scoped diagnostics checks)**

The [scoped receipt](../evidence/mobile-pause-followup-v1.json) separates the
reported iPhone 15 Pro / Chrome iOS pauses and first-load refresh from local
reproduction. The first authenticated staging query found no report from the
reported release; that absence does not identify a client or server cause.

Controlled tests demonstrate two reporting failures: a deferred local write can
be lost on suspension or teardown, and quick Resume actions can repeatedly
cancel collection of an already completed incident. The owner now requests a
coalesced local checkpoint on suspension, bounds orderly teardown persistence
to 250 ms, and collects completed incidents on the next eligible inactive
transition. It preserves the newest incident's recovery window and prefers a
fresh current snapshot over its older retained copy. These changes do not alter
source timing, guard thresholds, input, audio clocks or manual Resume intent.

The Release candidate passed packaging/audit and both real-HTTP Chrome
retention/reload cases. The full suite passed 1,849 tests before the final
retained/current snapshot correction; all 34 owner tests passed afterward.
The broader player check passed 14 checks, then encountered an audio queue
overflow during the second menu loop. A smaller two-loop route passed without
overflow after removing the save/Eject/layout prelude. The exact deployed
baseline then passed all 19 checks, and the candidate passed all 19 on a
follow-up run with earlier error detection. The extra audio
observer lost its data on final navigation, so no queue-trace claim is made.
The initial intermittent overflow remains retained and unassigned. Final-code
CI passed both unit shards and all required checks. These functional results
do not establish sustained performance or uninterrupted audio.

Final review additionally reproduced a saved-incident loss when a first read
found older records before a young current incident reached storage. Retained
reads now remain eligible while any current incident is young. The negative
control fails and all five affected owner checks pass after the correction.
The earlier package/browser and CI results above precede this final JavaScript
correction; its source hash and focused results are bound in the receipt.

Three bounded probes of the retained PR144 package reproduced a first-CSS
simulation-debt pause in desktop WebKit; the corresponding Chrome controls
reached their declared CSS observation windows without a forced pause. A slow
GPU completion notification coincided with the callback gap, but the slow
submission introduced no new pipeline identity. Aggregate process stacks show
background Wasm optimization; they do not locate its work within the gap or
establish CPU, GPU or compiler causation. No local probe reproduced the refresh.

The physical phone pause/refresh causes remain open. Desktop engine probes,
induced stalls and lifecycle storage checks do not establish iOS memory limits,
foreground performance, sustained play, original-state agreement, physical
input or uninterrupted audio. No new deployment is claimed by this entry.
