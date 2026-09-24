# Source DevCom startup allocation boundary

**Compiled / Source identified / Native traced / Retail compared**, scoped to
the fields in the [receipt](evidence/source-devcom-startup-v1.json).

The checked-Wasm `source_devcom_trace` starts with the complete validated
[HSD allocation prefix](SOURCE_ADDRESS_CONTEXT.md#standalone-original-hsd-startup-boundary).
It then issues the first synth-shaped type-3 request directly to the pinned
original `devcom.c`. This is a direct request fixture: it does not execute
`HSD_SynthInit`, `ARInit`, `ARAlloc`, or the full audio startup routine.

The fixture uses the original HSD audio allocator, preserving its heap handle
and the retained 16-node DevCom block. The original source owns request serials,
lane selection, relay selection, queue links, internal callbacks and recycling.
The [deferred ARQ provider](SOURCE_ARQ_COMPLETION.md) serves the untouched source
queue. A second synthetic request checks node reuse and a non-null callback;
it is not an additional retail startup event.

The host services have explicit ownership boundaries:

- Relay DMA tokens refer to checked compiled host spans. They are not original
  static-global addresses. Only the MEM1-backed heap payload is translated to
  the independently derived source address space.
- Cache store publishes the exact owned relay range into separate DMA-visible
  bytes. Unpublished, unowned, or actively transferring ranges fail explicitly.
  Cache invalidation and DVD operations are outside this type-3 target and fail.
- Completion stays deferred while interrupts are masked. The explicit pump
  copies published bytes and calls the original ISR under a software mask.
  This establishes controlled source ordering, not hardware interrupt timing.
- A fixture-only aligned redeclaration preserves the authored relay type and
  bounds while satisfying the SDK's 32-byte DMA requirement on the host.

The original comparison covers the first request serial, type, size and
entry destination, plus the audio heap handle, block payload and requested
allocation size. Cleanup, callback reuse, cache publication, full zero-byte
copies and interrupt-mask checks are source-execution evidence; they are not
claimed as captured retail completion timing or state.

Run the focused checks after building `source_devcom_trace`:

```sh
python3 -m unittest discover -s tests -p test_source_devcom_startup.py -v
```

Tests explicitly skip when the target is absent. Owned-input runs use
`tools.original_startup_fixture.run_owned_fixture`, which derives the context
internally from the owned disc/DOL and pinned source. Captured allocation rows
are comparison-only inputs after execution.

The remaining synth ARAM reservations, full audio startup, `lbMemory`,
`lbHeap`, scene allocation history, live fighter bindings and CPU register
carry remain open. No browser, pixels, PCM, performance or full-session
acceptance follows from this fixture.
