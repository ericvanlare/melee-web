# Typed gameplay bootstrap owner

**Compiled / Source identified**

This result covers the synchronous Wasm fixture only. The
[scoped receipt](../evidence/gameplay-bootstrap-owner-v1.json) records a
read-only producer for all 21 private scalar, pointer and callback variables in
`src/gameplay_bootstrap.c`. Its versioned 128-byte record identifies the arena,
session, generations, ticks, lifecycle flags and callback identities. It neither
dereferences those identities nor captures their payloads or authorizes restore.

The optional wrapper publishes a record only after checking source-memory health,
world/session ownership, allocation identity/generation/size, and public gameplay
statistics. Refusal leaves the caller buffer unchanged, including when a producer
writes its private staged record before failing. The normal build keeps this
wrapper disabled. Only the exact `gameplay_snapshot_probe` selection can enable
`--gameplay-bootstrap-state`; other selections fail before source preparation.

## Reproduction

After normal bootstrap, run the focused controls and affected diagnostic build:

```sh
python3 scripts/agent_workspace.py run -- python3 -m unittest discover \
  -s tests -p 'test_gameplay_bootstrap_state_*.py' -v
python3 scripts/agent_workspace.py run -- python3 -m unittest discover \
  -s tests -p test_gameplay_bootstrap_probe_cleanup.py -v
python3 scripts/agent_workspace.py run -- python3 -m unittest discover \
  -s tests -p test_selected_trace_build.py -v
python3 scripts/build.py --trace-target gameplay_snapshot_probe \
  --gameplay-bootstrap-state --configuration RelWithDebInfo
```

Use the existing locally owned Mario/Final Destination snapshot fixture assets.
The output directory must be absent. Run this synchronous Node command under
`reference-capture/slippi/process.py`'s ProcessSupervisor with a 60-second whole
budget and ten seconds reserved for process cleanup; it performs source PCM
processing without host speaker output:

```sh
python3 scripts/agent_workspace.py run -- \
  node tests/gameplay_bootstrap_state_probe.mjs \
  --runtime build/browser/gameplay_snapshot_probe.js \
  --assets /path/to/owned/snapshot-mario \
  --probe-source tests/gameplay_bootstrap_state_probe.c \
  --source-c src/gameplay_bootstrap.c --source-h src/gameplay_bootstrap.h \
  --out /path/to/new/observation
```

The runner compares compiled producer/header/wrapper hashes before initialization.
It follows the existing source Entry/Ready fixture, permits at most 600 ordinary
steps, requires source quiescence, and compares two complete records byte for byte.
The step limit is a diagnostic budget, not an authored source table bound.

## Observed result and limits

At source head `4d9b972fe6161a9e024419529b7599df654fd247`, based on main
`c3ddd39d191abcd59a72cd428a085c0958c922a9`, both normal disabled and enabled
RelWithDebInfo builds passed with default compiler jobs. Actual Wasm export
parsing found zero diagnostic functions when disabled and all seven when enabled.
The source fixture reached Ready at tick 124, returned two identical records,
passed before-init, wrong-size, null-output and after-close refusal controls, and
closed its source match/audio/world. All 34 owned asset hashes stayed unchanged.
Each owned process group exited and all 1,697 tracked source pins stayed unchanged.

The required full suite passed 1,883 tests with 141 skips in 333.990 seconds.
The named machine was an arm64 Mac16,10 with 16 GiB, macOS 26.6 build 25G72 and
Node 22.23.2. Job elapsed times in the receipt describe these checks; they are not
snapshot/restore costs or foreground performance evidence.

The reviewed runner correction at `9a617d49b54c02a8fa1bfbe4f0b348955892d7a3`
registers cleanup before entering initialization, attempts source close once,
and preserves its first structured receipt after a refusal or exception. The
test-only successor `d57cf8dd57f70d4746917104a496b26e140c4bc2` exercises
refused/throwing initialization and refused/throwing normal close after two
successful mock captures. All four lifecycle controls passed. A real Wasm
fixture again reached Ready at tick 124 and closed once. Deliberately empty
assets produced a separate **failed** probe at missing `PlCo.dat`; its
expected-failure control passed with a successful single close.

The post-review full suite passed 1,887 tests with 141 skips in 350.894 seconds
(351.188 seconds through the owned wrapper). The final normal enabled rebuild
passed in 17.206 seconds and produced JS/Wasm bytes identical to the original
enabled artifacts. All 1,700 tracked pins stayed unchanged and each owned
process group exited. The receipt keeps the original measurements separate
from these new runner/build/test identities; the compiled producer and wrapper
did not change.

Arena payloads, SDK heaps and allocator free chains, HSD object/process graphs,
source-memory ownership, mutable Wasm globals/tables, callback graphs, host
objects, renderer/GPU, browser clocks/input, Web Audio and external effects remain
uncovered. The [earlier full-memory experiment](../evidence/source-snapshot-feasibility-v1.json)
has its own explicit exclusions. This result establishes no combined restore,
replay correctness, original/Slippi state agreement, whole match/rematch, impaired
rollback, pixels/PCM fidelity, physical input or Internet acceptance. The
[multiplayer acceptance issue](https://github.com/ericvanlare/melee-web/issues/115)
remains open.
