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
is silently dropped.

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
