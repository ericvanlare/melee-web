# Aurora Future/object owner accessor preparation

This packet adds an opt-in, read-only diagnostic target for the pinned Aurora
WebGPU runtime. It observes the module-local `WebGPU.Internals.futures` and
`jsObjects` sparse stores through the existing Emscripten JS-library boundary;
the accessor itself does not replace a producer, enroll a callback, submit
queue work, create a device, or authorize restoration. The populated fixture
separately exercises the real Emdawn adapter/device/queue-submission path so the
rows are meaningful. The diagnostic is explicitly unsupported for native
builds and Emscripten pthreads.

The source pin is Aurora `749d6ee7a22bdfab78c8ece9047bca5d79aa72ca`. The
downstream patch is applied by `scripts/bootstrap.py`; the opt-in target is
`aurora_future_owner_probe` and is enabled only when `MELEE_WEB_AURORA_FUTURE_OWNER_DIAGNOSTIC=ON`.
This diagnostic is based on main `73c66e925634b6be1f9de924c59df83d3cbe40f9`;
the downstream patch SHA-256 is
`f4af73e2e1d2fb625461984a24e1fac599d0e0708fdbb89f2bc6f2635e54edbb`.
The target inherits the production Aurora Emscripten configuration, including
the existing exception and Asyncify settings, and exports only the diagnostic
probe entry points plus the accessor ABI. The normal build targets and
player-facing exports are unchanged.

Focused controls are available without bootstrapping `.deps/aurora`:

```sh
python3 -m unittest discover -s tests -p 'test_aurora_future_owner_accessor.py' -v
```

After bootstrap, the focused VM controls execute the exact pinned JS-library
body in isolated module contexts:

```sh
python3 scripts/agent_workspace.py run -- \
  node tests/aurora_future_owner_accessor_vm.mjs
```

The later ABI-only build is deliberately separate and uses the repository's
normal `build/browser` private development output under the checkout workspace
guard; it must not be redirected to an arbitrary new output root:

```sh
python3 scripts/agent_workspace.py run -- \
  python3 scripts/build.py --trace-target aurora_future_owner_probe
```

The HTML fixture is a subsequent real-HTTP headless-browser boundary. It keeps
speaker output silent, reports console/page errors and cleanup, and carries a
data-URL favicon so an automatic request cannot create an unrelated 404. The
driver's `--timeout-ms` is a wall-clock budget for the phases it explicitly
times and for bounded cleanup. It reserves ten seconds for CDP, screenshot,
detach, context, browser, and HTTP-server close operations; it does not guarantee
process termination or cancel the underlying operation after a timeout. Run it
under the repository's ProcessSupervisor, which owns its exact process group.
The outer owner must also track and verify Chrome descendants that create their
own groups. This preparation description makes no gameplay or restore claim.

Once the opt-in target has produced a fresh bundle and the page/driver files
have been staged beside its `.js`/`.wasm` output, the corresponding bounded
probe is:

```sh
python3 scripts/agent_workspace.py run -- \
  node tests/aurora_future_owner_browser_driver.mjs \
  --site /path/to/fresh/staged-site \
  --out /Volumes/AgentStorage/melee-web/runs/<fresh-output> \
  --browser-tools /path/to/melee-web/scripts/browser_tools.mjs \
  --playwright-dir /path/to/installed/playwright \
  --timeout-ms 45000
```

The populated probe uses `scripts/browser_tools.mjs`, a real HTTP server,
headless Chrome, silent launch options, a bounded screenshot, browser CDP
process diagnostics, and explicit page/context/browser/server cleanup. It
requires the fixture's real native adapter/device/queue-submission path,
checks the synchronous and post-completion Future/object rows and Promise
identity, and verifies the exact Destroyed device-lost cleanup callback. The
accessor remains read-only and the report records `graph_restore: unsupported`;
this is not a gameplay or renderer-restore claim.

A native C++ control compiles the reviewed accessor and verifies that each unsupported native operation returns its explicit error without writing the caller buffer. Passing scratch is removed; failed controls retain their source and logs.

The VM controls cover sparse holes versus own `undefined`, descriptor getter
refusal without invoking a getter, identity replacement, invalid output and
ordinal zero-write refusal, disposal/re-capture, independent module state,
and the explicit retained-reference/tracked-entry budgets. Those budgets are
diagnostic resource limits, not authored WebGPU table bounds.

The native callback-before-Future-publication bit is rejected at every recorded
checkpoint. Cleanup observes one event-loop turn after destruction; it does
not establish that no callback can arrive later. Accessor disposal releases
only its private references and labels, while the host sparse stores remain
untouched. Buffer arguments follow the Wasm32 unsigned ABI, and writes are
limited to the fixed record size.
