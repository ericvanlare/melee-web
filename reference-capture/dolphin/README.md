# Passive retail Dolphin observer

This directory contains the downstream source overlay and patch series for the
read only JITARM64 observer used by the retail reference capture app. The
upstream checkout at `.deps/reference-dolphin` is pinned to
`c77bbaa0f372c3f72281602a8b087206706542cb`; the build helper copies it to an
ignored source tree, applies the patch, and overlays this directory before
configuring CMake.

The observer is dormant unless all of these activation variables are present:

* `MWRC_ENABLE=1`
* `MWRC_OUTPUT=/path/to/a/new/capture.mwro`
* `MWRC_DOL_SHA256=dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646`
* `MWRC_CPU=JITARM64`
* `MWRC_SOURCE_REV=GALE01r2`

`MWRC_STATUS` optionally names the sidecar status JSON; otherwise it is
`<MWRC_OUTPUT>.status.json`. The output is an immutable little endian MWRO
stream parsed by `reference_observer_stream.py`. It contains a handshake, a
start record, typed boundary records, and an end record. A natural result and
scene teardown publish `{"status":"completed","natural":true}` and a
sidecar with `completed: true, invalid: false`. Operator termination is
reported as interrupted. Ring overflow, a bounded read or serialization
failure, stream failure, or status failure latches `invalid: true`; no record
is silently dropped. An overflow error includes the attempted event's PC,
source tick and steady-clock time, producer/consumer indices, ring occupancy,
maximum observed occupancy, and writer dequeue/write durations. These fields
diagnose the observer itself and do not affect the guest or convert an
overflow into a valid capture.

For a bounded Link Arrow callback investigation, the optional compact companion
trace uses `MWRC_ITEM_PROBE_OUTPUT` and `MWRC_ITEM_PROBE_MATCH`. Fixed windows
use `MWRC_ITEM_PROBE_FIRST_TICK` and `MWRC_ITEM_PROBE_LAST_TICK`; a dynamically
triggered window instead uses `MWRC_ITEM_PROBE_TRIGGER` set to
`young_link_arrow_creation` or `young_link_arrow_launch`, plus
`MWRC_ITEM_PROBE_CAPTURE_TICKS` from 1 through 16. The match index is zero
based. Its instruction words are pinned in
`tools/cpu-item-boundary-gale01r2.json`; each observed instruction is checked
against the resident Rev 2 DOL. The trace records paired item/Arrow/CPU callbacks
and source tick/draw returns, plus stable item identity, owner, scheduler
priority, registered process priorities, position/velocity bits, hitbox-0
endpoints, and CPU xF4/xF8 fields. It fails closed if the observed item pointer,
GObj, kind, or owner changes, and is bounded to 2,048 records and 4 MiB. Its
output path must be separate from both MWRO and any CPU register-probe output.
Schema version 5 also records the paired-single angle argument, collision
position, shield HitResult bytes, and shield-bone matrix at the Link Arrow
`ftColl_80077688` assignment entry. These fields support a narrow source/browser
input comparison and do not broaden the probe's equivalence claim.
This diagnostic does not change guest memory or constitute gameplay
equivalence evidence.

For a companion-only bounded probe, `MWRC_ITEM_PROBE_SUMMARY_STREAM=1` keeps
only the observer handshake/start and error/end records in the MWRO path while
the status sidecar continues to report progress. This is not a complete MWRO
session stream and must not be passed to the whole-session comparator.

At each `Fighter_Create` return, the observer retains the GObj context and the
returned Fighter head. Source player slots can own paired entities (for
example Zelda/Sheik or Popo/Nana), so every fighter slice carries identity in
its flags: the low byte is the exact source slot and the high byte is the
entity index in source creation order. Entity zero keeps the legacy slot-only
flag value. Per-player stock, HUD, and magnifier slices remain keyed by source
slot alone. The observer accepts at most two distinct fighter kinds per slot;
out-of-range slots, duplicate identities, and excess entities remain errors.

The JIT callback flushes cached guest registers and only copies bounded raw
memory slices. It never writes guest memory, single steps, or uses the debugger.
The writer is a separate thread. The stream is opened with exclusive creation,
and completion flushes through the operating system before the completed
sidecar is published.

The boot patch independently reads and hashes the selected disc's actual
`main.dol` (SHA-256 and SHA-1), and checks `GALE01` revision 2. The activation
hash is not trusted as a substitute for this check. When
`Core.SaveDataWritable=False` is used for a capture, directory-card shutdown
and SRAM shutdown both return before any host save write; guest-side card/SRAM
mutations remain in the emulated session and are discarded with the process.
