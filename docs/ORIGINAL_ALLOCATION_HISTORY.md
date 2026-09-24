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

The passive `ReferenceAllocation::Observer` backend in
`reference-capture/dolphin/source/Core/PowerPC/ReferenceAllocationObserver.cpp`
uses the generated identity-only profile from
`tools/generate_reference_allocation_profile.py`. `Arm()` performs profile and
path validation without guest reads; `Start()` is then called only for the
verified original DOL entry word. The observer validates each generated
function body hash, records bounded read-only metadata on verified entries and
returns, and writes off the emulation callback through a finite ring. Its
`boundary_complete` marker becomes true only at the verified return from the
first `gm_Scene_Vs_OnEnter` after all source-thread stacks reconcile. That
prefix boundary is separate from whole-session or ownership completion and does
not provide observed pointers as replay inputs.

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

Profile version 2 also observes the original handle callback, RAM alarm chunks,
DevCom request/completion wrapper, and DVD preload completion. The address-only
handle model separates move start, transfer completion, and the next callback.
RAM chunks advance at most `0x19000` bytes; low-address moves wait for explicit
DevCom completion. Generations reject stale callbacks and unfinished teardown.
The model moves identities, not payload bytes, and does not implement transport
scheduling or validate copied contents.

Diagnostic replay compares manager fields at entry and return, including nested
callbacks. The original manager retains its copy fields after completion and
clears only its active size; a nested no-op compaction may replace the global
cursor before the earlier callback returns. Call-local completion tokens preserve
that ordering. Initial manager words come from the independently verified DOL
BSS range, while DevCom request numbering starts from owned executable data.
Relocation updates derived payload labels and teardown invalidates them. Captured
manager words, pointers, and request results remain comparison targets.

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

Both histories now replay all 276 captured calls through 211 model operations
and validate 94 allocation identities each. Native and checked Wasm outputs
agree, including the SDK heap descriptor heads, ordinary HSD pool descriptors,
ARAM stack metadata and game-heap/handle words. The earlier call-55
`lbHeap_80015900` boundary is covered by the new source game-heap planner.

The continuation also captured a fresh original menu prefix through Mario/Fox
CSS and original SSS. Its first retained attempt contains 25,168 completed calls;
all replay through 20,070 model operations and 12,189 allocation identities in
native and checked Wasm. The capture itself failed at the SSS readiness check,
so it has no complete scenario receipt. Its raw stream and the failed replay
attempts are preserved. The observed pool reset/replacement/refill sequence at
call 1,878 exposed a stale ownership check: after a descriptor reset the OS heap
still owns orphaned backing cells, but the descriptor no longer references them.
That distinction is now modeled and checked against the untouched HSD code.
The SSS readiness failure was a separate controller-driver bug: the prior gate
read the unrelated main-menu cooldown. The driver now checks the original SSS
cursor process and its own cooldown; a fresh run has returned through original
SSS to CSS and reached the original rules menu without a state write.
That second attempt reached its declared 600-second wall limit in rules setup;
all 47,974 captured calls replay through 39,489 model operations and 22,867
allocation identities in native and checked Wasm. It includes 14 observed
`lbMemFreeToHeap` releases, bound to model-derived owners and payload labels.
Neither partial menu attempt has a complete scenario receipt.

The two menu histories agree on the first 23,397 completed allocation calls,
including arguments, results and allocator metadata. Their first differing
allocation is call 23,397, after the corrected driver presses B at SSS frame 31;
the older driver instead remains neutral in its failing readiness wait. This
is an explained controller-context difference, not an unexplained allocator
mismatch or a complete same-input scenario pair. No allocation mismatch remains
inside either captured prefix.

No fighter construction is reached in these boot/menu captures. Zero of six
requested full scenario captures are complete, and no fighter address has been
derived. The [bounded save search](OWNED_SAVE_PREREQUISITE.md) found no eligible
owned unlocked persistent card. The first acceptance gate remains open: supply
that ordinary save, record two fresh controller-driven captures for each
workload, and validate every observed fighter and relevant global identity.
CPU integration also requires source-to-host ownership and caller/global/stack
provenance. The diagnostic prefix does not supply those bindings.

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

The OS heap/HSD pool component now supports explicit descriptor resets, registry
forgetting, and separate backing-cell adoption and object pops. A reset clears
the descriptor's chains while the OS heap retains orphaned backing cells. The
replay adapter executes the raw child allocation before linking the pool refill;
it never allocates the same request twice or passes an observed pointer to the
model. All observed retail HSD refills select heap 1; selected-heap variants in
tests are synthetic coverage, not a new observed retail mode. Dedicated heaps
and number/heap limit modes remain explicitly unsupported.

The separate `source_game_heap` component reproduces the source descriptor
partitioning, ordered transient destruction, HSD main-heap replacement, current
ARAM handle replacement, and persistent child recreation. Its step interface
allows original HSD pool resets and registry forgetting to occur inside the
main-heap replacement at their original boundaries. Invalid layout requests
remain retryable. The native and checked Wasm models are compared with unchanged
`lbheap.c` under a 32-bit oracle whose nested allocator services are explicit
synthetic boundaries; original capture replay separately validates those nested
services. Type-0 table descriptors are not admitted; the original sentinel is
removed by the independent context adapter.

The separate address-only `source_aram` component models the observed aligned ARAM allocation stack and
LIFO release. The `source_handle` component models original `lbMemory` descriptor
free lists, best-fit placement, the last-equal-gap rule, and descriptor recycling.
Both receive explicit source context and reject missing or unsupported behavior.
The legacy synchronous diagnostic method still returns `async_move_required`
when movement is needed. The explicit asynchronous interface described above
handles move start and completion at separate source boundaries.

Their native ASan/UBSan and checked Wasm models are compared with the untouched
original `ar.c` and `lbmemory.c` bodies compiled under the original 32-bit layout.
The ARAM oracle sets its private allocator globals to declared synthetic roots;
it tests ARAlloc/ARFree/ARGetSize, not hardware/MMIO initialization. The handle
legacy oracle supplies an explicit ARAM fixture and aborts if an asynchronous
alarm or device-copy path is reached. The separate asynchronous oracle exercises
queued low-address DevCom completion against untouched `lbmemory.c`; its excluded
RAM alarm services abort. RAM manager transitions are separately compared with
the original capture. These narrow environments are not runtime services.
Synthetic descriptor, heap and ARAM bases are relocated, with corresponding
normalized outputs required to agree. No original assets or executable bodies
are embedded in tests.

Use `MELEE_SOURCE_ADDRESS_EVIDENCE_DIR`, `MELEE_SOURCE_HANDLE_EVIDENCE_DIR`,
`MELEE_SOURCE_ARAM_EVIDENCE_DIR`, `MELEE_SOURCE_GAME_HEAP_EVIDENCE_DIR` and
`MELEE_SOURCE_POOL_LIFETIME_EVIDENCE_DIR` to retain component operation streams, outputs
and hash receipts in fresh ignored directories. The allocation runner likewise
preserves every attempt, checks the owned plan copy, freezes generated execution
inputs, and verifies that original inputs and owned collector files stay unchanged.

The [version-1 evidence receipt](evidence/original-allocation-history-v1.json)
records both final runs, all retained attempts, precise configured and observed
function counts, input/output hashes, source identities, replay boundaries,
component comparisons and validation totals. The receipt contains no raw streams
or game payloads. Runtime, graphics and fighter Release builds pass. These are
compile and bounded allocator results, without gameplay or performance admission.
The version-1 repository suite passed 709 tests with 44 optional fixture/target
skips; its focused collector, menu, boot and replay run passed 29 tests. All 2,651
original source files and 15 frozen gold-corpus files match their reserved hashes.

The continuation's [version-2 receipt](evidence/original-allocation-history-v2.json)
records the heap and pool lifetime extensions, retained failures, fresh menu
prefixes, relocated component comparisons, and the bounded save search. Its
complete local suite passes 723 tests with 44 documented optional skips; all 54
affected collector/context/native/Wasm/replay checks pass within that suite.
Runtime, graphics, and fighter Release builds pass. Exact-head CI results are
recorded on the draft PR after publication. No public deployment files or CPU
integration path changed.

## Passive application companion

The passive allocation diagnostic is a companion to `MWRC_ENABLE=1`, with the
normal owned-disc observer configuration and exact input replay. Set
`MWRC_ALLOCATION_OUTPUT` to a fresh local JSONL path. A requested allocation
failure invalidates the capture; it is never silently dropped. Allocation-only
callbacks do not add primary MWRO events. The JIT also observes the logical
return boundary when it follows an unconditional BLR within a block.

Regenerate or verify the tracked identity table with the owned profile:

```sh
python3 tools/generate_reference_allocation_profile.py --profile "$ALLOCATION_PROFILE" --output reference-capture/dolphin/source/Core/PowerPC/ReferenceAllocationProfile.h --check
```

The profile JSON and raw allocation history stay under ignored work evidence.
The tracked header contains only names, identities, bounds, instruction words
and hashes. Its comments bind the original DOL, pinned source and symbol input.
The reference build archives the generator and profile recipe alongside the
header, patch series and hash-bound build receipt. None of this metadata is a
runtime source-address provider.

## First VS initialization capture

The [compaction receipt](evidence/original-allocation-compaction-v1.json) records
repeat original cold boots driven by the same immutable SI input recording:
four Mario CPU9 players, four stocks, Final Destination. The allocation streams
end at the first verified VS-entry return and are byte-identical. Native and
checked-Wasm replay derive all four fighter identities and compare every captured
allocation call in this prefix. The [earlier passive receipt](evidence/original-allocation-passive-v1.json)
retains the failure at the first asynchronous compaction. In the replay report,
`completion_scope` describes the captured source boundary; only the status,
completed-call count and ownership evidence describe how far replay validated.

Pool identities now come from pinned source declarations joined to the symbol
map, including allocator members at the start of larger structures. Equal-size
unrelated globals cannot enter the inventory, and observations cannot extend it.

The next model boundary is repeated fighter ownership and teardown. Original VS preload
rebuilds the main HSD heap, but retained game heaps, preload caches and audio/ARAM
owners still constrain its bounds. The browser therefore also needs an
independent source-context provider, followed by register-carry integration.

This evidence is a first-VS allocation prefix. It does not establish repeated
fighter ownership, full-session browser agreement, pixel or PCM agreement, or
performance. The diagnostic tools can be used while these gates remain open.
