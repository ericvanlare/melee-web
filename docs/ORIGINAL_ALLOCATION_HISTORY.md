# Original allocation-history experiment

This diagnostic experiment investigates the missing source context described in
[the allocation prerequisite](SOURCE_ADDRESS_CONTEXT.md). It starts the owned
GALE01 revision-2 executable at its original entry point, observes allocation
operations, and drives ordinary controller inputs through the original menus.
It does not change CPU gameplay, inject source state, prescribe fighter
addresses, or resolve the tick-2,495 CPU command divergence.

The three existing development workloads remain frozen: 1,199 ticks of
Mario/Fox CPU1 on Final Destination, 4,346 ticks of Marth/Falco CPU5/Mario CPU9
on Yoshi's Story, and 3,838 ticks of Fox/Mario CPU3/Marth CPU6/Falco CPU8 on
Dream Land. Their original saved-state reference pairs remain gold inputs to
their existing scoped comparisons. A new cold-boot allocation experiment has
a different declared application context; it must not replace those golds or
claim their trajectory solely because the scenario and input plan match.

## Collection boundary

`scripts/capture_allocation_history.py` reuses the retail runner's isolated
Dolphin user directory, pinned executable/configuration checks, copied original
card data, hardware execute breakpoints, controller pipes, and owned-process
cleanup. It omits the saved-state launch argument and requires the initial PC
to equal the entry from the owned DOL header. No source memory, registers,
executable instructions, RNG, or CPU decisions are written. Original execution
may update its own copied card data; the supplied card stays unchanged.

`tools/retail_allocation_profile.py` resolves function boundaries and global
identities from the pinned source symbol map and the independently hashed
original DOL. It binds function-body hashes, entry instructions, and every
unconditional return instruction in the selected function. Runtime instruction
checks reject mismatched entry/return boundaries. These are diagnostics for
this exact original executable, not a shipped PowerPC interpreter.

`tools/reference_allocation_capture.py` installs all allocation breakpoints
before continuing. Its optional stream can accompany the existing reference
collector; a future controller-driven application can reuse that stream without
embedding the experimental menu driver or allocation replay in gameplay.

The configured observation boundaries cover:

- Original ARAM stack initialization, allocation, LIFO release, and size reads,
  including the audio allocations preceding `lbMemory` initialization.
- OS arena changes and arena allocations; heap initialization, creation,
  destruction, selection, allocation, and release.
- HSD heap selection and main-heap replacement; pool initialization, explicit
  or automatic refill, object allocation/free, and registry forgetting.
- HSD memory wrappers, preserving their relationship to the underlying OS
  operation so replay does not allocate their bytes twice.
- Game heap construction, transitions, allocation/free wrappers, and the
  separate `lbMemory` handle allocator's creation, allocation, release,
  destruction, and compaction request boundaries.
- Fighter initialization/construction and original VS entry/exit, with fighter
  identities and relevant global pointers recorded as validation observations.

Allocator descriptors are metadata. The collector does not dump object
payloads, archives, textures, executable bodies, or whole RAM. It does not
pretend that observing a compaction request proves that an asynchronous move
has been fully modeled; additional observed modes need their own boundaries.

## Version-1 stream

Each JSONL row has a contiguous `sequence`. The header identifies the cold DOL
entry and hashes the profile. It retains observed boot arena/memory metadata
for comparison with an independently justified boot context. Those observations
alone do not authorize treating a captured heap boundary as a replay input.

An `enter` row records a unique `call`, function name, parent call, original
thread and stack identity, caller LR, ABI arguments, complete GPR/PC/LR/CTR/CR
context, source counters, and applicable allocator metadata. A `return` row
references its entry and separately records the original result and observed
post-operation metadata. OS heap descriptor heads/sizes, HSD pool descriptors,
and game handle metadata remain explicit. Fighter addresses are independently
observed at original `Fighter_Create` return.

A `repeated_stop` row preserves a debugger retrap. It may refer to the preceding
boundary only when the complete recorded machine context and allocator metadata
are identical. It contributes no allocation event. Changed state, unmatched
returns, excessive consecutive repeats, or an exhausted event budget stop the
capture explicitly. Per-thread call stacks preserve interleaved execution.

The `end` row records call/retrap counts and pending frames. Pending calls or
collector errors make the stream incomplete. The runner also requires a complete
original state trace and exact consumption of every frozen PAD sample before
reporting a full diagnostic capture. `--boot-only` is a separate smoke boundary
ending at the first original scheduler return; it is never a scenario result.

## Evidence and replay requirements

Every run uses a fresh output directory. Preserve failed preparation, timeout,
collector, menu, and replay attempts. Keep raw streams, process logs, generated
profiles/drivers, copied configuration, input-command logs, original state/draw
outputs, and their hashes under ignored `work/`. Portable receipts may contain
counts, digests, source identities, validation outcomes and first differences;
they must omit personal paths, game assets, raw payloads and oversized streams.

Replay must begin from a source-justified boot/heap definition, derive placement
through the compiled source-address model, and validate original results
separately. Pointer arguments must resolve to previously derived owners or
independent original layout identities. Feeding an observed allocation result
back as the answer for the next operation is not replay. Unknown ownership,
missing history, or an unimplemented mode must identify the first affected
event and stop the corresponding claim.

Independent captures need separate process lifetimes and immutable original
inputs. Compare complete event streams and derived identities, retaining the
earliest difference. Synthetic relocation tests must still relocate outputs
with their declared bases. Model code must contain no scenario, fighter, tick,
expected address, or command-output rule. Native and checked Wasm differential
tests use untouched original allocation implementations as the oracle.

CPU register-carry integration remains a later change. Even a passing address
replay would still need source-to-host ownership bindings, relevant global and
stack provenance, and original caller/clobber semantics before changing the
skipped-conversion CPU path. This experiment does not run reserved holdouts,
start the issue-14 desktop application, or modify public deployment.

## Independent boot definition

`tools/original_boot_context.py` takes the owned disc, original DOL and untouched
pinned source checkout. It accepts no allocation trace. It derives the initial
arena from the original apploader's FST reservation and BI2 data, then decodes
the DOL's OSInit linker/stack constants, crash-handler reservation, framebuffer
dimensions/count, FIFO size and heap parameters. Instruction shapes, executable
identity and apploader hash are checked before applying this bounded derivation.
The supported hardware context is the pinned Dolphin default GameCube memory
configuration, without an original debugger monitor. Expanded-memory and other
boot contexts fail explicitly until independently derived.

The resulting roots are compared with observed boot words and OSInitAlloc
arguments. ARAM begins at the SDK-defined stack base; recorded allocation sizes
and releases must derive the later `lbMemory` arena. Its observed address cannot
be supplied as that root.

## Capture prerequisite found during cold boot

The frozen GCI's source save payload has zero character and stage unlock masks.
The existing SSS checkpoints retained separately prepared availability state;
that state is absent when the same external card is loaded from a fresh DOL
boot. The first 3P and 4P attempts therefore stopped at unavailable Marth.
Their logs and allocation prefixes are retained. The first 2P attempt reached
SSS but sent B during stage-selector construction; the adapter now waits for the
source cursor process and input cooldown before using that route.

Completing the three requested cold-boot scenarios requires an owned persistent
save with their characters and stages unlocked. The collector does not write
availability bits or manufacture a replacement save. The input prerequisite
has been requested; no complete scenario capture or fighter-address replay is
claimed from these failed attempts.

Two separate original boot runs with the final 45-function collector reached the
first scheduler return. Each retained 554 rows and 276 calls. The complete raw
streams are byte-for-byte identical, including machine context, arguments,
return values and allocator metadata. This is boot
context evidence, not the required pair of captures for any CPU scenario.
The paired boots exercise 34 of the 45 configured functions. The receipt lists
the count for each function; unhit boundaries, including fighter construction,
are instrumentation coverage rather than observed original behavior.

Both histories replay identically through 33 model operations and validate 13
pointer aliases with the independently derived boot context. Native and checked
Wasm outputs agree. The first unsupported event is call 55, sequence 111,
`lbHeap_80015900`: the source game-heap planner applies transient/persistent
partitions and replacement lifetimes. Its static descriptor table is already
derived from the DOL; replay still needs the source-owned orchestration that
destroys/recreates OS and handle heaps and updates HSD pool lifetimes. No earlier
allocation mismatch is observed. The identical captures therefore identify a
missing replay dependency, not an independent-run divergence.

No fighter construction is reached in these boot captures. Zero requested full
scenario captures are complete, and no fighter address has been derived. The
first acceptance gate remains open. After supplying the unlocked persistent save,
record two fresh controller-driven captures for each development scenario, then
extend the planner against untouched source and validate every observed fighter
and relevant global identity. Do not integrate CPU register carry before those
requirements and the caller/global provenance requirements above are satisfied.

Replay a retained raw stream with the owned inputs and pinned symbols:

```sh
python3 scripts/replay_allocation_history.py \
  --trace "$ALLOCATION_TRACE" --profile "$ALLOCATION_PROFILE" \
  --disc "$OWNED_DISC" --dol "$OWNED_DOL" \
  --symbols .deps/melee/config/GALE01/symbols.txt --checked-wasm
```

Retail replay independently derives its roots again and rejects numeric root
overrides, changed boot observations, mismatched original identities, and an ARAM
stack or handle global inconsistent with the static source layout. Numeric roots
remain available only for explicitly synthetic tests. A successful prefix report
always has `complete: false`; `--require-complete` fails for this bounded model.

## Model and test boundaries

The merged OS heap/HSD pool implementation is unchanged. The separate address-only
`source_aram` component models the observed aligned ARAM allocation stack and
LIFO release. The `source_handle` component models original `lbMemory` descriptor
free lists, best-fit placement, the last-equal-gap rule, and descriptor recycling.
Both receive explicit source context and reject missing or unsupported behavior.
Compaction that would move payloads asynchronously returns `async_move_required`;
the original captures so far only exercised no-op compaction.

Their native ASan/UBSan and checked Wasm models are compared with the untouched
original `ar.c` and `lbmemory.c` bodies compiled under the original 32-bit layout.
The ARAM oracle sets its private allocator globals to declared synthetic roots;
it tests ARAlloc/ARFree/ARGetSize, not hardware/MMIO initialization. The handle
oracle supplies an explicit ARAM fixture and aborts if an asynchronous alarm or
device-copy path is reached. These narrow environments are not runtime services.
Synthetic descriptor, heap and ARAM bases are relocated, with corresponding
normalized outputs required to agree. No original assets or executable bodies
are embedded in tests.

Use `MELEE_SOURCE_ADDRESS_EVIDENCE_DIR`, `MELEE_SOURCE_HANDLE_EVIDENCE_DIR` and
`MELEE_SOURCE_ARAM_EVIDENCE_DIR` to retain component operation streams, outputs
and hash receipts in fresh ignored directories. The allocation runner likewise
preserves every attempt, checks the owned plan copy, freezes generated execution
inputs, and verifies that original inputs and owned collector files stay unchanged.

The [portable evidence receipt](evidence/original-allocation-history-v1.json)
records both final runs, all retained attempts, precise configured and observed
function counts, input/output hashes, source identities, replay boundaries,
component comparisons and validation totals. The receipt contains no raw streams
or game payloads. Runtime, graphics and fighter Release builds pass. These are
compile and bounded allocator results, without gameplay or performance admission.
The final repository suite passes 709 tests with 44 optional fixture/target skips;
the final focused collector, menu, boot and replay run passes 29 tests. All 2,651
original source files and 15 frozen gold-corpus files match their reserved hashes.
