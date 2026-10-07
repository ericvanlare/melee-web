# H1 stopped-image diagnostic ownership during export

**Source identified / Browser exercised** (retained historical runs only).
The [portable lifecycle receipt](../evidence/h1-stopped-owner-lifecycle-v1.json)
reduces the retained callback table of the [older black stopped-canvas run](2026-10-06-h1-clean-prefix-visual-followup.md).
It adds lifecycle evidence without changing that historical receipt.

After native reason-5 pause, cursor 3001 and source frame 1419 initially remained
stopped. About 736 ms later the callback table reports cursor zero and frame −1.
This reset occurred before callback export finished and before the capture
helper's explicit native cleanup. The production replay poll finishes stopped
performance replays after 700 ms without progress; its finish path unloads the
source owner and resets the replay cursor. A read-only execution of that actual
poll handler reproduced its finish decision. The lifecycle evidence strongly
supports teardown during export; it does not prove the exact pixel blanking cause.

The newer short paired-image run acknowledged native pause at cursor 1608. Its
immediate image retained that cursor, and the latest subsequent callback had
zero source steps and draws. Trace export completed, but the delayed screenshot
correctly refused cursor zero. The pair remains incomplete. The immediate image
was independently observed to show the four Mario fighters, Final Destination
and HUD with existing magenta geometry; there is no pixel equivalence claim.

A development-only ownership lease now bounds stopped-pair evidence collection
to 30 seconds. It is acquired synchronously with a fresh checked native pause,
bound to the replay owner, token and cursor, and suppresses only the stopped
replay watchdog. Faults, overflow, wall-time, expiry, resumed source, cursor
changes and preparation re-entry still finish explicitly. Success and export
failure join the existing replay owner's teardown. Actual-handler tests cover
these boundaries and unchanged ordinary stopped-replay behavior. No browser or
build has exercised the lease yet.

H1 natural-pause explanation, performance admission, the complete supported
route, original pixels, foreground timing, physical input and PCM remain open.
