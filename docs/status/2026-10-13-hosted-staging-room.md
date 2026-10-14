# Hosted staging room and two-computer alpha

**Final candidate validation, second deployment HTTP and short hosted check pass**

The final candidate passed its full regression and ordinary Release build. Its
second deployment passed the reviewed static HTTP and asset-byte checks, and
the short Host/Join → CSS → Leave browser check passed. Earlier same-host relay
RTT and full-route results came from the first candidate and remain separately
scoped: RTT passed, while that full route failed before completion. The short
check does not establish a full match, two-machine play or gameplay acceptance.
The [receipt](../evidence/hosted-staging-room-v1.json) binds both
producer/deployment revisions and their distinct evidence.

## Final candidate and revision 2 deployment — validation / HTTP pass

The final candidate is clean source
`4181cd25a0060520999e05d4e33720b56b5ce73b` (tree
`6e5256750ae126c5f8fa997a36cddbe690625790`). The full regression ran 2,398
tests with 166 skipped, and the ordinary Release runtime build passed. Its 43
runtime artifacts and the hosted-package manifest are hash-bound in the
receipt.

Revision 2 is serving at
`https://melee-web-room-relay-staging-288.mywebcomic.workers.dev/` on version
`5d607d92-458a-4b5c-9751-7c130e3cb062` at 100%. The HTTP check passed for the
runtime's 43 files with matching local/served bytes and the required isolation,
CSP and Wasm MIME headers. This is a static HTTP result; it does not establish
browser gameplay, a successful relay upgrade on this deployment, or a
two-computer match. The dedicated RTT result below is bound to revision 1.
The deployment remains workers.dev-only with no routes or preview URLs and
retains the existing `RoomRelay` SQLite namespace
`6644841b09974b46822753ce5cc49965`. Workers Free / $0 remains the owner's
previous plan authority; this work did not reverify billing or change the
account, plan, DNS or production.

The short hosted Host/Join → CSS → Leave check passed with exact paired
checksum records, keyboard cursor movement on both ports, normal Leave,
verified native unload to phase 0 with running 0, and exact 4:3 canvas aspect ratios. The
beta canvas rectangle was offscreen at the sampled point after Controls
scrolling, so this result establishes its dimensions/aspect ratio, not visible
placement. H1 exports recorded no timing incidents or pause in this bounded
check, with 169 and 51 history samples evicted across the two peers; this is not
complete H1 coverage. See the receipt for per-peer native network-wait
counters. Both browser contexts and their profiles were closed/removed, the
test room sockets were closed, and the staging Worker remains deployed idle.
The two-Mac manual trial has not run.

## Revision 1 same-host RTT — pass

The earlier dedicated RTT measurement used two browser profiles on one host
against the hosted relay. Median round-trip estimates were 93.755 ms in one
direction and 80.603 ms in the other. Those figures include browser dispatch
and relay work; they are not two-machine latency, network-only RTT,
input-to-photon latency or a performance acceptance result. This measurement
belongs to revision 1, not the current revision 2 deployment.

## Revision 1 full-route attempt — fail

The first candidate's separately reviewed route attempt exhausted its operation
budget while progressing through Results. It recorded four stock losses and
three respawns, then observed the Results presentation but did not finish the
Results-to-CSS route. The final raw checksum comparison was unavailable. No
Results logic defect or network-only cause was established.

The harness's Leave status differed from the legacy expected status. Its two
bounded status waits consumed the cleanup reserve before native unload and H1
export could be verified. Both `before_leave` observations still reported
`OriginalResults` with `running=1`; native cleanup is **unverified**. The H1
recorder was enabled, but final export failed at the cleanup deadline. Do not
claim zero H1 incidents or a clean native unload from this attempt. Exact-owned
outer browser/server process cleanup passed and remains separate from native
lifecycle evidence. This failure belongs to revision 1 and is not a result for
revision 2.

## Next boundary and manual use

With the short check's reviewed pass complete, the next proposed manual trial
uses two separate Macs and each player's own local disc. On the host, use the
existing Controls panel to set Player 1 to **Keyboard**, then choose **Host
game · Player 1**. On the joiner, use the existing controller
detection/remapping controls to assign an attached controller to **Port 2**,
set Player 2 to **Controller only**, and choose **Join game · Player 2** with
the host's invite. The controller model is not prescribed. Record connection,
intended inputs, any completed match, timing incidents and stop/unload evidence
from both peers. A successful short Host/Join check is not itself a two-Mac
match or broader acceptance result.

The hosted page still runs the existing interim lockstep route. It does not
change the match-only rollback target. The earlier local-package result remains
in the [historical initial-preparation entry](2026-10-13-manual-two-computer-alpha.md)
and cannot substitute for the hosted boundary.
