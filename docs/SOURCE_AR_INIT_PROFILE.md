# Original ARInit hardware-profile boundary

The checked-Wasm oracle runs the pinned SDK `ARInit`, including its original
`__ARChecksize` DMA probes, against a declared default GameCube ARAM profile.
It then exercises original `ARAlloc`/`ARFree` with a synthetic allocation stack.
This is a standalone component boundary; it is not installed in the browser
or connected to the [DevCom startup fixture](SOURCE_DEVCOM_STARTUP.md).

The [scoped receipt](evidence/source-ar-init-profile-v1.json) records the owned
run, original comparison, negative controls and validation. The evidence labels
are **Compiled**, **Source identified**, **Native traced**, and **Retail compared**
for the six runtime relations and two independently derived input fields only. Full audio startup, live source
allocation history, CPU register carry and full-session equivalence remain open.

## Source and service ownership

The oracle compiles original `ar.c` as Wasm32 C++ so a typed register proxy can
observe its original MMIO expressions. A checked temporary-source adaptation
applies `patches/melee-source-ar-init-cxx.patch`, changing exactly three legacy
C pointer-result casts to explicit C++ casts, and
adds four read-only private-state accessors. Original and generated hashes are
retained; the upstream checkout is unchanged. Integer/pointer widths are checked
and floating-point contraction is disabled. This is compiled source, not a
PowerPC instruction interpreter.

The declared profile has 16 MiB ARAM, no expansion device, the SDK base, and
the default GameCube bus clock. The register provider implements the mode-4
alias writes used during sizing and the resulting mode-3 memory behavior from
the pinned Dolphin DSP interface. The null expansion device leaves read
buffers unchanged and ignores writes. Transfers crossing the supported
ARAM/expansion boundary fail explicitly. This is a bounded service profile,
not a general DSP implementation or a hardware-timing claim.

The source probe owns three temporary aligned stack buffers. Cache flush
registers only spans within that live stack scope and publishes their bytes to
separate DMA-visible storage. DMA uses those registered spans; invalidation
reloads published bytes. The bindings expire when initialization returns.
Register-operation budgets stop stalled polls. Unknown registers, unowned
spans, unexpected interrupt-context services and unsupported profiles fail.
Synchronous probe transfers do not change the separately deferred ARQ provider.

`tools/source_ar_init_fixture.py::run_owned_fixture` independently derives boot
roots from the owned disc, pinned DOL and untouched source/symbols. It validates
the authored 16-entry stack table against actual zero-initialized DOL BSS and
rejects initialized-data overlap. The numeric table identity labels the source
object; it is never a host pointer. Matching JSON labels alone are not trusted
provenance. The lower-level argument helper is also used with explicitly
synthetic contexts in tests.

The binary reports actual initialization returns, private allocator state,
physical size-cell contents and all stack words. It deliberately does not
self-attest the supplied symbol identity. The owned adapter establishes that
input derivation; captured values enter only the subsequent comparator.
Repeated initialization is checked with different arguments to verify the
source early return preserves the existing state without additional DMA.

## Validation scope

The original comparison checks two derived input fields (stack identity and
capacity) and six runtime relations: return value, discovered size, stack
pointer, free blocks, block-table offset within the supplied array, and init
flag. The source operates on a fixture-static array; this does not establish
a live binding to the original DOL BSS object.
It does not compare DSP registers, cache-operation counts or timing. A shifted
block-table offset is rejected by the same post-run comparator.

The later allocation/free sequence is synthetic component evidence; it does
not claim the intervening synth calls or their allocation order have executed.
Additional controls reject unowned DMA/cache spans, duplicate/invalid profile
arguments and stalled hardware readiness. A nonzero-buffer test distinguishes
null-expansion preservation from an incorrect zero-fill implementation.

Run the focused boundary after provisioning dependencies:

```sh
MELEE_SOURCE_AR_INIT_EVIDENCE_DIR=work/ar-init-new-run \
  python3 -m unittest discover -s tests -p 'test_source_ar_init*.py' -v
```

The evidence directory must be new. Without it, successful component artifacts
are temporary; compilation failures are retained under ignored `work/`.
AI/AX, DSP tasks/mailboxes, physical interrupt timing, PCM, full `lbAudioAx`,
application heaps and browser gameplay remain outside this target. No deployment
follows from this component result.
