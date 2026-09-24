# Source allocation identity prerequisite

`src/source_address_context.hpp` and `.cpp` implement an address-only model of
original SDK heap allocation and the ordinary OS-backed HSD object pool. The
same C++ implementation is tested as native code and Wasm. This is a reusable
prerequisite, **not an installed CPU compatibility fix**: gameplay does not call
it yet, and it supplies no missing retail allocation history.

The existing [register investigation](CPU_REGISTER_COMPATIBILITY.md) establishes
why this is needed. On the skipped-conversion path, the original CPU command
writer consumes the low bytes of reaching `r5` and `r30` values. One can be a
fighter allocation identity. Replacing it with a host pointer, a captured
fighter-address table, or neutral input would not reproduce the original rule.

## Supported boundary

| API | Meaning |
| --- | --- |
| `Address` | Explicit 32-bit source identity; no host-pointer conversion or dereference. |
| `Heap::create_empty` | Explicitly declare a new empty source heap. There is no default arena. |
| `Heap::restore` | Supply an existing source heap's ordered cell lists and bounds. Validate structure; do not invent omitted history. |
| `Heap::allocate` / `release` | Original first-fit placement, splitting, allocation-list insertion and address-ordered coalescing. |
| `ObjectPool` | Ordinary OS-backed `HSD_ObjAllocAddFree`, `HSD_ObjAlloc` and `HSD_ObjFree` identity/lifetime behavior. |
| `RegisterWord` | Explicit known scalar/source-address word or unavailable reaching definition. |
| `resolve_stick_carry` | Defined signed-low-byte consumption of both known words; unavailable if either is unknown. |

The heap follows the pinned original `extern/dolphin/src/dolphin/os/OSAlloc.c`:
32-byte alignment and headers, a 64-byte minimum split remainder, first suitable
free cell, most-recent-first allocated list, sorted free insertion, and forward
then backward coalescing. Arithmetic uses wider intermediates and rejects
invalid or overflowing requests instead of inheriting pointer/integer undefined
behavior. Invalid requests are model errors, not claims about retail assertions.

For ordinary HSD pools, object size rounds to the requested alignment. A refill
allocates one OS block, links consecutive objects in order, and places that chain
before the previous free chain. Individual frees prepend objects to the pool;
they do not return the backing OS block. The model tracks live objects, use and
peak counts, and rejects conflicting direct OS frees of pool backing cells.
Pool ownership is unique and survives destruction of its model owner until the
heap context is replaced, matching retention of the original backing storage.

A heap clear or restore changes its generation and invalidates existing pool
owners. Explicit pool initialization in the new generation discards the stale
lists. Heap and pool owners cannot be copied; pools cannot be moved. Unavailable
context, exhaustion, invalid input and unknown allocations have separate results.
A known zero register is distinct from an unknown register; unknown values never
silently become neutral sticks.

## Original-source comparison

The differential oracle directly includes the untouched pinned SDK `OSAlloc.c`,
HSD `memory.c` and `objalloc.c` bodies. It compiles them as wasm32 with the original
release descriptor layout; static assertions require 32-bit pointers/long and
12-byte cell/heap descriptors. Test-only headers provide platform declarations,
while `HSD_GetHeap` selects the oracle's current heap. Original HSD assertions and
`OSCheckHeap` validation remain active. No replacement allocation algorithm is
used as the oracle.

The oracle's 64 KiB arena and event streams are synthetic. Addresses are compared
as offsets into that arena. The model runs each stream at three independently
declared synthetic source bases, in native and Wasm builds. Every event compares
returned offset/size, all live allocations, complete free/allocated cell lists,
free bytes, and every pool's size, alignment mask, use, peak and free-chain order.
The suite covers:

- First-fit fragmentation, both sides of the split threshold, exhaustion,
  coalescing, invalid events and complete cleanup.
- Three deterministic 1,064-event allocation/free histories.
- Interleaved OS allocations and multiple HSD pools, explicit refill, automatic
  refill, reuse order and retention after every object is freed.
- Invalid/restored contexts, overflow, ownership and lifecycle errors, plus all
  256 signed-low-byte values and unknown-versus-intentional-zero register words.

Native tests enable AddressSanitizer and UndefinedBehaviorSanitizer. Wasm tests
run with assertions and safe-heap checks. These are allocation/component tests,
not PowerPC instruction execution, CPU call-site validation, full-match evidence
or performance admission. They neither consume nor change retail golds.

Run after provisioning the pinned local dependencies:

```sh
python3 -m unittest discover -s tests -p test_source_address_context.py -v
```

Set `MELEE_SOURCE_ADDRESS_EVIDENCE_DIR` to a new ignored output directory to
retain passing event streams, original/model outputs and hashed comparison
receipts. Existing directories are rejected. On comparison failure the suite
retains the complete available stream/output and first-difference error under
`work/source-address-context-failures/`, even without that option.

The [original prerequisite receipt](evidence/source-address-prerequisite-v1.json)
preserves the pre-integration checkpoint: five passing component tests and 3,268
events per base/target. That full local suite ran 625 tests with 46 skips, 12
errors and one failure; all 13 failures were denied socket binds (`EPERM`) in
unchanged server/capture tests. It is not a green full-suite result. Its recorded
GitHub, socket and Git-write blockers describe that earlier task, not a property
of this component. The receipt remains unchanged alongside the prior retail and
browser evidence.

The [reconciled receipt](evidence/cpu-register-reconciled-v1.json) records the
successful normal merge of main, fresh Release builds, five passing component
tests, and a green 673-test local suite with 46 optional-target/fixture skips.
The additional browser-observer test passes separately. Fresh complete visible
comparisons retain exact 1,199-tick 2P and 4,346-tick 3P core/CPU results; the
3,838-tick 4P run still first differs in CPU input at tick 2,495. Other camera,
subject and draw differences remain open. These are development state checks,
not performance or content admission, and gameplay still does not use this
component. See the [integration results](CPU_REGISTER_COMPATIBILITY.md#reconciled-prerequisite-validation).

The native guard test requires a C++17 compiler. The four differential/Wasm tests
require `.deps/melee`, the repository's Emscripten SDK and its local Node runtime;
absence is reported as a skip, never an accuracy pass. Source hashes are checked
before/after the suite. No game assets or original source bodies are vendored in
these test files.

## Explicit limits

This is a general model of the supported allocation operations, not a complete
model of the game's memory system. In particular:

- The heap component receives explicit bounds. The separate
  [allocation-history experiment](ORIGINAL_ALLOCATION_HISTORY.md) derives original
  `OSInitAlloc` placement and replays observed heap selection and replacement.
  `OSAllocFixed` and `OSAddToHeap` remain unsupported and unobserved in its current
  traces. Synthetic component oracles declare their own initial heap bounds.
- `Heap::restore` checks structure, not provenance. Gaps may represent prior
  fixed reservations, but every gap and cell must come from an independently
  attested complete source context. Valid structure alone does not admit a
  fabricated snapshot. No accepted retail heap snapshot provider exists yet.
- A heap snapshot does not serialize HSD pool ownership or free chains. Resuming
  an existing pool from a snapshot remains unsupported. Explicit `ObjectPool::reset`
  now models in-place `HSD_ObjAllocInit`; adopting a backing cell requires its
  separately replayed OS allocation, rather than a restored-address assumption.
- Dedicated HSD bump heaps and number/heap limit flags return
  `unsupported_configuration`; the OS path cannot stand in for them. The model
  leaves the ordinary global `obj_heap.remain` bookkeeping to the diagnostic
  lifetime adapter; that bookkeeping does not enable those unsupported modes.
- Payload memory, source-to-host bindings, original SDA/global and stack
  identities, and call-site reaching definitions/clobbers are not implemented.
  `RegisterWord` is a representation, not an analysis of those call sites.
- The original HSD memory-allocation failure assertion is not replaced. The
  synthetic valid pool histories avoid that failure; model exhaustion is an
  explicit result to be handled by a future integration boundary.

## Native component initialization boundary

The native world initializes the public HSD component pools once per world
in the order authored by `HSD_ObjInit`: List, AObj, FObj, ID, Vec, Mtx,
RObj, Render, Shadow and ZList, following `HSD_IDSetup`. Scene owners reuse
that initialization instead of resetting Shadow or ZList at individual scene
entries. Ownership guards and world teardown remain required; initialization
must not erase a live owner.

This is the component-pool portion of startup only. `HSD_InitComponent` also
requires original OS arena/heap, framebuffer/FIFO and platform services that
the current browser bootstrap does not supply. The pool change does not derive
original fighter addresses or implement CPU register carry. Its scoped checks
and retained failures are in the
[native initialization receipt](evidence/native-pool-initialization-v1.json).

## Standalone original HSD startup boundary

The checked-Wasm allocation fixture starts a fresh source-owned MEM1 context
from independently decoded owned-disc boot geometry. It reserves the decoded
crash-handler span, then executes original XFB/FIFO allocation and the HSD
OS/ID/object initializers. Its partial HSD entry is compiled only into this fixture;
ordinary gameplay cannot invoke it. Aurora supplies the OS and GX services,
with a fixture-scoped aligned, zeroed MEM1 allocator used before `OSInit`.

The comparison covers declared arena boundaries, HSD heap identities and
component-pool metadata. It does not execute full retail platform startup,
audio, ARAM initialization, game heaps or scene ownership. In particular,
the original audio startup retains allocations before `lbMemory` and
`lbHeap`; direct ARAM initialization would skip those effects. See the
[scoped startup receipt](evidence/original-startup-allocation-v1.json) for
source/build identity, original comparison, negative controls and retained
failures. The browser still needs its own integrated source-context provider.

## Remaining dependency before CPU integration

The existing match-only bootstrap allocates a fresh host arena and does not
reproduce original pre-match allocation order. The pinned source shows the
missing chain: `HSD_OSInit` initializes OS heaps;
`lbHeap_80015F3C` and main-heap creation establish scene lifetimes;
`Player_80036DA4` / `Fighter_FirstInitialize_80067A84` initialize pools; and
`Fighter_Create` interleaves GObj, fighter, attributes, parts, joint/display-object,
archive, process and shadow allocations. Replaying just fighter allocation sizes
cannot recover the required identity.

The next source-context work must supply a declared original application profile
and complete relevant allocation/free history, or an independently validated
complete heap-and-pool snapshot plus its subsequent event history. The current
CPU recipes and register windows do not contain that history. Captured fighter
addresses may validate a derived result; they must not be inputs that prescribe
that result. No default fresh heap or per-scenario snapshot may fill this gap.

After the context provider is justified, integrate it in shared compiled code:

1. Model all allocation modes actually reached by that context and check their
   state/order against untouched original code and original-process diagnostics.
2. Bind source identity, host pointer, span, owner kind and generation explicitly.
   Keep host dereference and source identity separate; invalidate on teardown and
   source-context replacement. A numeric source address is not a host pointer.
3. Derive original global/SDA and relevant stack identities from the declared
   source layout. Model each relevant caller's reaching definition, preservation
   and clobber, including paths that deliberately carry zero. Unknown provenance
   must stop admission rather than provide a guessed word.
4. Change only the original skipped-conversion CPU path to consume the modeled
   carry. Keep ordinary conversion and explicit neutral commands intact. Test
   call-site semantics independently, then rerun the frozen paired corpus and
   prior human/CPU checkpoints with original compiled CPU decisions.

This component has no roster, stage, source tick, observed fighter address,
expected stick output or replay identifier. Its synthetic base relocation checks
establish that placement follows its supplied history. They do not establish
that the supplied history matches a retail session.

Both development and prototype pages must obtain this eventual behavior from
one shared compiled runtime. No browser mount API should accept retail CPU
outputs, source-address overrides or match-specific stick rules. The prerequisite
adds no dependency on page layout, frame scheduling or public deployment. It
should be reviewed as groundwork; resolving tick 2,495 still requires the
source-context and call-site work above.

## Deferred source ARQ prerequisite

The [original ARQ completion boundary](SOURCE_ARQ_COMPLETION.md) now exercises
the pinned SDK queue against checked owned memory spans and deferred transfer
completion. Its scoped source tests cover priority, cancellation, callback
reentry and interrupt masking. The [standalone DevCom boundary](SOURCE_DEVCOM_STARTUP.md)
connects the first direct type-3 request to the validated startup heap. Full
audio startup and the live browser source context remain open.

The [original ARInit profile](SOURCE_AR_INIT_PROFILE.md) separately executes
the source hardware-size probes and allocator initialization from declared
owned boot inputs. It has not yet joined the DevCom or live browser providers.

The [DSP startup protocol fixture](SOURCE_DSP_INIT_PROFILE.md) validates original
SDK task and interrupt handling behind checked synthetic services. It does not
execute AXOut, DSP firmware or the remaining application allocation history.
