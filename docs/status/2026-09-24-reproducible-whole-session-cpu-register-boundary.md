# Reproducible whole-session CPU register boundary

**Compiled / Source identified / Native traced / Browser exercised**: a fresh
three-match four-Mario CPU9 reference now has exact SI input recording. A control
replay and a read-only register-probe replay agree on every declared primary
observation, excluding host timestamps and run IDs. The bounded headless browser
comparison isolates a zero-knockback CPU-input difference; the probe confirms
the original seed-pointer and fighter-pointer register carry. Exact hashes,
comparison fields, first divergence, validation and retained failures are in the
[diagnostic receipt](../evidence/whole-session-cpu-register-v1.json) and
[register investigation](../CPU_REGISTER_COMPATIBILITY.md#reproducible-whole-session-register-diagnostics).

This establishes a reproducible diagnostic boundary. The live original allocation
context and shared compiled register-carry implementation remain open;
full-session browser equivalence, pixels, PCM and performance are not established.
