# Architecture decisions

## 001 — Compile recovered source

Use doldecomp/melee source with a separate platform layer and browser build target.
No PowerPC CPU emulator, JIT, or hidden interpreter fallback. The original binary
is an offline validation reference. Keep downstream patches reviewable and small.

## 002 — Evaluate Aurora before writing a renderer

Aurora already translates GX state and draws to WebGPU. Its native implementation
is the starting point. The prototype's browser adaptations must execute all queued
render work; disabling worker creation without replacing its execution is invalid.

The Dusk browser fork is a reference for integration seams, not our dependency.
Its documented skipped game-worker threads do not constitute a viable scheduler.

## 003 — Browser first, native debugging companion

Build and validate the browser boundary immediately. Native builds can isolate
compiler/logic problems but do not substitute for a working browser target. Keep
web UI limited to local launch, errors and evidence until gameplay exists.

## 004 — Explicit binary data conversion

WASM32 matches the original pointer width but not GameCube byte order. Convert
typed asset fields and relocations at an audited boundary; do not bulk-swap unknown
payloads. Validate offsets, counts and buffer bounds before pointer relocation.
Start with one synthetic archive test and one real asset, then expand by type.

## 005 — Preserve numerical semantics

No fast-math. The native compatibility macros are not a correctness specification.
In particular Melee's host __frsqrte placeholder uses sqrt although original callers
expect a reciprocal-square-root estimate. Resolve these operations and establish
reference tests before relying on gameplay math.

## 006 — Stage performance evidence

The first probe reports CPU submission and requestAnimationFrame intervals only.
They are not GPU duration, physical display latency, or a full-game benchmark.
Keep shader validation enabled during bring-up. Measure release builds separately.
