# Original Dolphin input recording

The optional `0002-si-input-stream.patch` follows the passive observer patch on
the same pinned Dolphin source revision. `build_reference_dolphin.py` applies
both patches in an isolated source copy, includes the adjacent GPL-licensed
`ReferenceInputStream` source, and archives the exact corresponding source,
patches, build receipt and notices. The build receipt declares
`input_recording_version: 1`. The pinned upstream checkout remains untouched.

The SI GameCube controller and native-adapter implementations record the full
host `GCPadStatus` returned at each original request, before origin/calibration
and SI packing. Native adapter connection queries are separate ordered events.
Only requests actually made by the original emulation are recorded. All four
guest PAD ports remain covered by the separate semantic observer, including
unused/disconnected ports. The stream never contains CPU-generated decisions,
guest memory, source addresses or save states.

Activation requires the existing verified observer environment and exactly one
of `MWRC_INPUT_RECORD` or `MWRC_INPUT_REPLAY`, together with
`MWRC_INPUT_STATUS`. The supervisor strips inherited observer, SDL and Dolphin
profile overrides before supplying its explicit session configuration. The
actual boot DOL is verified before input-stream initialization.

Record mode uses a 4,096-slot bounded ring and a separate writer thread. No file
I/O occurs at each SI polling boundary. Replay mode validates and loads a
bounded stream before boot and bypasses live host input. Each request must
match the next record's channel and exact emulated CoreTiming tick. The
original calibration and SI processing then consume that device sample.

The version-1 wire format is little endian:

| Region | Fields |
| --- | --- |
| 16-byte header | `MWRI`, u16 version 1, u16 header bytes 16, u32 record bytes 40, u32 reserved 0 |
| 40-byte record | u64 sequence, u64 emulated tick, u32 channel, u16 buttons; u8 main X/Y, C X/Y, L/R triggers, analog A/B, switches, connected; u32 kind; u32 CRC32 of the preceding 36 bytes |
| Channel | 0–3: full pad status; 256–259: native-adapter connection query |
| Kind | 1: sample; 2: completed footer; 3: interrupted footer |
| Footer | Next sequence, last sample tick, channel `0xffffffff`; never consumed as input |

The file bound is 64 MiB. Unsupported headers, CRC errors, sequence gaps,
backwards emulated clocks, invalid channels, invalid connection flags,
truncation and missing completed footers reject replay. Runtime timing/channel
mismatch, exhaustion before teardown, unread events at teardown, ring overflow
and writer/status failures invalidate completion. The observer waits for the
input writer to flush and publish completion before it publishes its own
successful ending. Crashes and cancellation retain incomplete partial bundles.

`input-status.json` independently reports mode, count, completion and errors.
The Python validator checks it against the stream count before finalization.
`configuration-snapshot.json` contains a bounded private hex encoding of exact
configuration files, checked against their captured SHA-256 inventory before
restoration. Neither this snapshot nor raw `.mwri`/observer payloads belong in
Git. Replay results bind the original manifest and input hash without copying
the original stream into a second raw bundle.

The semantic comparison is independently regenerated from each raw observer
stream. It compares source observations, including addresses, as outputs.
It excludes declared independent host/session provenance only. No comparison
result is fed back into either emulation or its supplied inputs.

This boundary does not add audio/PCM comparison, arbitrary memory dumps,
allocation-history breakpoints, menu equivalence in the web port, netplay,
native Dolphin movie interoperability, or save-state replay. The application
supports one ordinary human-P1/CPU-P2 match per fresh boot.
