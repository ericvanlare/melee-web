# Original ARQ completion boundary

This document records the **Compiled / Source identified / Native traced**
bounded source-level ARQ handoff used by the
checked-Wasm32 oracle. The focused test includes the pinned original
`extern/dolphin/src/dolphin/ar/arq.c` directly and supplies a separate
`src/source_arq_context.c` backend for owned host spans and an ARAM byte span.
The [receipt](evidence/source-arq-completion-v1.json) binds the source and
test hashes.

The queue remains the original source queue. Its high and low priority order,
source chunking with 32-byte-aligned transfers, queued-request removal,
active-request completion,
callback re-entry and request callback order are not reimplemented by the
provider. The provider accepts only aligned nonempty transfers whose 32-bit
source address resolves inside a configured span and whose ARAM offset stays
inside the configured ARAM extent. Logical source addresses and host pointers
are separate values; configured host spans must be disjoint from one another
and from the ARAM backing bytes.

`MeleeWebSourceArqContext` must be zero-initialized before its first bind.
Descriptors and backing bytes remain caller-owned. A pending transfer retains
its resolved source and destination until `melee_web_source_arq_pump`; rebind,
callback removal and shutdown are rejected while a transfer or callback pump
is active. The original ARQ queue must be reset or torn down separately before
rebinding because the provider does not own the queue's static source globals.

The fixture models the source interrupt contract in one controlled thread.
`OSDisableInterrupts` returns the previous enabled state and
`OSRestoreInterrupts` restores it, including nested masks. A masked external
completion pump defers delivery. An accepted pump masks interrupts while it
copies the pending bytes and invokes the source DMA ISR, then restores the
previous state. This gives the oracle a checkable critical-section boundary;
it makes no hardware interrupt cadence, audio, allocation-order, browser,
session-equivalence or performance claim.

Run the focused boundary check after provisioning the pinned local SDK:

```sh
python3 -m unittest tests/test_source_arq_completion.py -v
```

The test compiles the provider and oracle as checked Wasm32, resolves the
project-local Node runtime from the pinned Emscripten configuration, and
checks that the original `arq.c` hash is unchanged. The repository-wide suite
and its retained log are recorded in the receipt but are independent of ARQ
hardware behavior.
