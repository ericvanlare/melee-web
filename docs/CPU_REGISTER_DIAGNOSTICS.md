# Read-only CPU register diagnostics

`scripts/capture_cpu_registers.py` extends the existing retail replay runner
with a bounded GDB observer. It is development tooling. Its register sidecar
has a separate diagnostic schema and cannot be admitted as a replay reference.
See [the investigation](CPU_REGISTER_COMPATIBILITY.md) for the unresolved
compatibility boundary and the frozen corpus.

Use the original scenario's donor snapshot, GC directory, provenance, complete
input plan and **frozen inline collector**. That collector includes the original
CSS/SSS preparation; substituting the generic collector changes the starting
context. The wrapper copies these inputs into a new owned run directory. It
retains the complete input plan and executes an explicitly hashed prefix ending
after `--end-tick`. It supplies only the original human PAD samples and declared
CPU-port connection state. CPU decisions remain source-generated observations.

For example, with local paths supplied explicitly:

```sh
python3 scripts/capture_cpu_registers.py \
  --dolphin "$DOLPHIN_APP" --disc "$OWNED_DISC" --dol "$OWNED_DOL" \
  --template-user "$SCENARIO_USER" --snapshot "$DONOR_SNAPSHOT" \
  --checkpoint-gc "$SCENARIO_GC" --provenance "$SCENARIO_PROVENANCE" \
  --input-plan "$COMPLETE_INPUT_PLAN" --collector "$FROZEN_COLLECTOR" \
  --probes tools/cpu-register-gale01r2.json \
  --start-tick 2494 --end-tick 2496 \
  --stack-bytes 256 --backchain-depth 16 --timeout 5400 \
  --output work/cpu-register/local-run/registers.jsonl
```

The command refuses an existing output. Keep all raw captures, register bytes,
stack bytes, assets and machine paths in ignored local storage. Share hashes,
source-relative analysis and comparison reports, rather than raw memory dumps.

## What the observer records

The GALE01 revision-2 probe document pins each original instruction word before
installing a hardware breakpoint. Its addresses identify debugger observation
sites and globals; they are **not** a production address-compatibility model.
Probes are disabled until the declared source-tick window and disabled again
after its final tick. The source scheduler and input-boundary observers remain
active throughout the prefix.

Each hit records all 32 GPRs, PC/LR/CTR/CR, raw F0–F6 bytes, bounded stack
backchains, every active fighter's pointer and GObj relationship, CPU block,
knockback/position/velocity bits and hitlag flags. Optional bounded global-memory
regions expose original allocator and thread context. The current schema calls
the floating-point field at `Fighter+0x1850` `kb_applied_bits`; it is distinct
from the integer damage-applied field at `Fighter+0x183C`.

The raw sidecar preserves call order and exact source ticks. Instruction pin
failures, incomplete registers, read errors, timeout and process failures remain
failed diagnostics, with owned logs retained. A completed diagnostic window
means only that the requested observation ran. It does not prove that its
trajectory matches the accepted reference or that a port implements the bug.

## Relationship to accepted evidence

The wrapper retains the original full-plan hash, executed-prefix hash,
collector and helper hashes, disc/DOL/Dolphin identity, source configuration and
owned snapshot/GC hashes. It executes the original game in a fresh Dolphin
process. The diagnostic disables the expensive CPU/draw *observation sidecars*;
original drawing still executes. Any resulting trajectory difference must be
reported against both fixed references before using the register observations
as causal evidence. The diagnostic never replaces or shortens an accepted
complete match.

Compare the ended source-state prefix with both frozen complete references:

```sh
python3 scripts/check_cpu_register_prefix.py \
  "$GOLD_A" "$GOLD_B" "$DIAGNOSTIC_PREFIX" "$RUN_METADATA" \
  --output work/cpu-register/local-run/prefix-comparison.json
```

The checker first requires the full gold pair to repeat, then validates the
ended prefix with the existing strict retail schema. It binds the original
collector bundle, complete human-input plan, exact executed prefix, window and
available disc/DOL/source/Dolphin identity. It compares every source field in
match entry, initial state and each included tick. Expected metadata differences
(collector instrumentation, capture ID, requested bound and plan prefix) are
reported separately; fields are not removed from source-state comparison.
The prefix end is a diagnostic bound, not a natural match ending.

Exit 0 means `diagnostic_prefix`, with no source divergence through the bound;
it does not admit a reference. Exit 1 indicates a divergence or an explicitly
retained failed diagnostic; exit 2 indicates invalid inputs. `--allow-failed`
permits analysis of a `diagnostic_failed` run while preserving that failed
status and failure reason even if its source-state prefix agrees. It cannot
turn a raw register-read failure into admitted evidence. Register sidecar
shape/read validation remains the capture wrapper's separate responsibility.

The original SDK terminates stack backchains with either zero or `FFFFFFFF`.
The observer retains the terminal word and stops before dereferencing it,
following `OSContext.c`'s `OSDumpContext`. Other unreadable addresses and cyclic
backchains remain diagnostic errors.

`gameplay_retail_trace --cpu-hitlag-diagnostic` also emits a separately labeled
`CPU_ADDRESS_AUDIT` initial record for the port's fighter/CPU addresses. These
host/Wasm addresses diagnose the allocation difference. They are not substituted
for the original pointers, and the ordinary semantic CPU observer remains
unchanged.
