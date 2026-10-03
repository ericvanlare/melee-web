# Whole-session replay producer and bounded consumer evidence

**Compiled / Source identified / Browser exercised**: PR59 adds an MWRC v8
exporter, typed initial-context import and strict scene/input ownership checks.
The original workload uses four level-9 Mario CPUs. Its bounded browser
comparison exposes a CPU-input difference consistent with the existing source
address/register-context limitation; whole-session equivalence remains open.
Builds, tests, exact comparison scope and retained failures are in the
[PR59 evidence receipt](../evidence/whole-session-replay-pr59-v1.json) and
[whole-session section](../VERSUS_RETURN_LOOP.md#pr59-whole-session-producer-and-bounded-replay-evidence-2026-09-24).
