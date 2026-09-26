# Allocation trace comparison

`scripts/compare_allocation_traces.py` compares the first strict difference in a
selected allocation history. It writes versioned `report.json`, concise
`report.md`, and a bounded `event-window.jsonl` into a new output directory.
The output directory must be outside both input directories. It never edits
its inputs. A nonzero exit code means the streams differed, were
incomplete, or lacked evidence for alignment; read `report.json` for the exact
outcome.

## Retained trace fields

The original collector writes `melee-web-original-allocation-history` v1
JSONL. `sequence` orders debugger stops, while paired `enter`/`return` records
carry the function, call/parent/thread, ABI `args`, raw caller LR, observed
allocator metadata, and return `result`. It captures only the generated
profile's boundaries. The header has a profile hash but no world generation.
Request sizes come from source API arguments. `OSAllocFromHeap` has no explicit
alignment argument; the pinned allocator contract fixes its source alignment at
32 bytes. The source-address replay and profile remain the authority for
derived identities. The end row distinguishes a captured boundary from full
ownership completion.

The browser report retains `final_snapshot.source_allocation_trace`, a bounded
ring of `{sequence, operation, heap, host, source, requested, generation}`.
Operation `1` is allocation and `0` is free. `generation` is the browser world
generation. The array length, sequence range, and total count establish whether
the ring overwrote initial events. `source` is a `source::Address`; `host` is a
process pointer and is never compared. Caller and explicit request alignment
are absent. The comparator uses the same documented 32-byte source allocator
contract for this OS heap projection and states that derivation in the report.
Browser scenario completion is reported separately from ring coverage.

The browser's `retail-port.jsonl` session/state format contains frames and
fighter states, not allocation operations. Passing it instead of the retained
allocation report produces an evidence-gap report; it cannot establish a first
allocation difference. Pool allocation/free operations also are not present in
the browser ring, so the raw-pair projection is limited to OS heap allocation
and free operations. The original tool's existing parser and disk-backed trace
store validate and stream the large original JSONL.

## Immediate diagnosis command

Set `ORIGINAL_ALLOCATION_TRACE` to the retained original `allocations.jsonl` and
`BROWSER_ALLOCATION_REPORT` to the source-allocation generation report. The
following selection is the current tick-1776 investigation's recorded
HSD-main-heap entry through first `Fighter_Create`, paired with browser world
generation 3. The mappings are written into the report for review; they are
explicit comparison premises, not tool-generated facts.

```sh
python3 scripts/compare_allocation_traces.py \
  --original-trace "$ORIGINAL_ALLOCATION_TRACE" \
  --browser-trace "$BROWSER_ALLOCATION_REPORT" \
  --out work/allocation-trace-compare-1776 \
  --original-start-sequence 131672 --original-end-sequence 135764 \
  --original-heap 1 --browser-generation 3 --browser-heap 0 \
  --owner-identity vs-main-hsd-heap \
  --owner-evidence 'Original HSD_CreateMainHeap sequence 131672 and OS heap selector 1; browser source heap handle 0 at the source HSD main-heap replacement boundary in generation 3.' \
  --world-identity first-vs-match \
  --world-evidence 'Original selected range is the first VS main-heap construction; browser generation 3 is the selected live VS source-memory world.' \
  --boundary-identity main-heap-to-first-fighter \
  --boundary-evidence 'Original sequence 131672 HSD_CreateMainHeap entry through exclusive sequence 135764 Fighter_Create entry; browser source allocation events selected from generation 3.' \
  --address-domain gale01r2-source-u32 \
  --address-domain-evidence 'Original OSAllocFromHeap result is a GALE01 source pointer; browser source is source::Address. Host pointers are excluded.'
```

Use an unused `--out` directory for each run. To compare normalized JSONL
fixtures or exports, each header must declare
`melee-web-normalized-allocation-trace` v1 and whether initial events are
present. Each event carries its world/boundary and source-evidenced owner
identity; allocation operations also carry request size, alignment, and a
trace-local lifetime id. Supply explicit world/boundary selectors when a file
contains multiple values. A common address-domain id and evidence id are
required before source addresses are compared.

The comparator compares events at the same strict ordinal. It does not skip an
unmatched operation to extend the prefix. Address equality cannot rescue a
request-size difference. A look-ahead window is context only and never changes
`matched_prefix_length` or the reported first strict difference. No result
labels the first observed difference as the root cause.
