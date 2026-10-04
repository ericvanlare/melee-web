# Aurora browser ownership descriptor

## Scope and evidence

`Compiled`, `Source identified`, and `Browser exercised` apply to the
[bound receipt](../evidence/aurora-browser-quiescence-v1.json), using a fresh
Release build from the recorded current-main base and the pinned Aurora source.
The actual source Mario/Final Destination fixture follows original Entry/Ready,
then renders source frames 1 and 2. The generated JavaScript is unmodified.

The new schema records every authored frame/staging slot, all five packet and
retained shadow ByteBuffer owners, shadow high-water lengths, staging generation,
and the actual returned WebGPU Future ID plus queue/encoder identities.
Registration and callback sequences distinguish completion activity from scalar
frame state. Counts and row extents come from the native descriptor; callers
must query its byte size. These identities are observations, not reconstructed
WebGPU objects.

The production owner book publishes registration before `OnSubmittedWorkDone`,
so spontaneous completion before the call returns still joins the real Future
ID. Old-generation callbacks cannot mutate a reused slot. A current-generation
registration mismatch or duplicate completion fails explicitly before release.
The focused compiled regression exercises this actual header.

Two captures at an idle boundary agree byte for byte and validate unchanged.
Invalid inputs and a deliberately perturbed descriptor are refused; an ordinary
subsequent source step invalidates the prior descriptor and diagnostic lease.
Host polling preserves source counters. The receipt retains GPU diagnostics,
the inspected screenshot, exact build/run hashes, and independent source,
browser-process, process-group and HTTP-listener cleanup checks.

The retained field comparison locates actual changes in shadow lengths,
submission/Future/encoder identities and counters across that source step.
Buffer addresses, capacities and ownership flags remain stable in this interval.
That stability covers metadata only; allocator state, buffer contents and the
complete host graph were not compared.

## Reproduction and limits

After normal bootstrap, run the host regression and affected build:

```sh
python3 scripts/agent_workspace.py run -- python3 -m unittest discover -s tests -p test_aurora_browser_submission_owner.py -v
python3 scripts/build.py --target graphics --configuration Release
```

The required local checks passed at code head `08366be`: 1,837 tests with 140
skipped (294.920 seconds), followed by the standard graphics Release build
(13.345 seconds). The [receipt](../evidence/aurora-browser-quiescence-v1.json)
binds both guarded runs, their artifacts and independent cleanup audits; each
verified 1,744 source/tool pins. The earlier missing-build preparation failure
remains retained. This documentation update changes no executable behavior.
The three complete retained workloads required for renderer/lifetime admission
have not been run; the two-frame diagnostic does not satisfy that gate.

The real browser receipt uses the retained private GameplayMatchSession fixture
and locally owned assets; this change adds no public browser reproduction target.
Its temporary diagnostic CMake target was reversed after the fresh build.
Bootstrap recognizes only the exact preceding reviewed Aurora tree when upgrading
an existing dependency checkout.

Descriptor/lease calls use the existing submission-status polling path, which
can process queued completions and submits no new draw. They explicitly refuse
Emscripten pthread builds because packet and shadow fields lack a synchronized
threaded read boundary. Owner bookkeeping itself is synchronized.

These APIs authorize no heap or renderer restoration. Their counters live in
Wasm memory, so they cannot establish independent host authority after a heap
restore. WebGPU objects, pending Promise/finalizer identities, presentation,
pipeline persistence, full audio and externally committed effects still need
their own ownership gates. Original pixels/PCM, complete gameplay/rematch,
impaired rollback, Internet, physical input and foreground performance remain
unrun here. The multiplayer acceptance task remains
[open](https://github.com/ericvanlare/melee-web/issues/115).
