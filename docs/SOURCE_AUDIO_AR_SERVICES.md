# Original AR and ARQ service boundary

This fixture executes original `ARInit`, `ARQInit`, `ARQPostRequest`, the AR
interrupt handler and the ARQ completion handler through a checked service.
The [scoped receipt](evidence/source-audio-ar-services-v1.json) records the tested
revision and limits. No AX/Synth integration, PCM, timing or full-session claim
follows from this component boundary.

The service borrows one platform owner for registers, interrupts, contexts and
cache spans. It owns the declared 16 MiB ARAM backing, decoded DMA request and
explicit completion phase. Original source retains the DMA callback, ARQ queues,
chunking and request callbacks. Transfer direction is explicit at the cache
resolver boundary. A platform diagnostic callback that returns is followed by
an abort; failed MMIO cannot silently produce a valid register value.

Initialization uses the same physical profile as the [ARInit oracle](SOURCE_AR_INIT_PROFILE.md):
mode 4 aliases writes below 4 MiB to the next 4 MiB during probing, then the
original probe selects mode 3. Absent-expansion reads leave destination bytes
unchanged. Transfers crossing a declared physical or mirror boundary fail.
The provider supports this exact profile, not arbitrary ARAM geometries.

Synchronous source probes complete at the original `__ARWaitForDMA` poll.
After source ARQ initialization, deferred requests retain their busy flag and
bytes until the explicit completion pump. The pump requires an enabled caller,
then masks delivery, copies the transfer, raises AR completion status and calls
the original interrupt handler. Source code owns acknowledgement and callback
selection. This is a declared functional schedule, not hardware timing evidence.

The fixture uses the original SDK `OSContext` layout and scoped cache shadows.
The three original stack-buffer registrations expire when ARInit returns;
subsequent requests require a separate borrowed span. Flush publishes CPU bytes,
DMA accesses the shadow and invalidation reloads bytes into the source buffer.
No arbitrary host-pointer fallback is available. Submission binds the resolved
shadow pointer and a nonzero lifetime generation; completion rejects replacement
or recycling. Deferred requests must fit physical ARAM, and reinitializing a
live service fails before its pending ownership can be discarded.

The compatibility header separates the no-argument `ARDMACallback` from the
request-pointer `ARQCallback`, while preserving the original request layout.
Only three exact C pointer conversions are adapted for the original AR source's
C++ register proxies, as documented by the existing ARInit compatibility patch.
Original source hashes remain pinned and are checked after execution. The
original OS interrupt-mask routines update checked C4/C8 cells and DSP AR enable
bits. Completion status is write-one-to-clear and source code acknowledges it.

```sh
python3 -m unittest discover -s tests -p test_source_audio_ar_services.py -v
```

The runner retains generated sources, commands, binaries, stdout, stderr and
failures in unique ignored `work/source-audio-ar-services-*` directories. A
shared AX integration must supply the same register-5 bank and interrupt/context
owner to both services; a standalone component pass does not establish that join.
