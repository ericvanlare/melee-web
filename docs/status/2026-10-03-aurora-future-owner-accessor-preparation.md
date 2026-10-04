# Aurora Future/object owner accessor

**Compiled / ABI-only real-WebGPU diagnostic**

The [scoped receipt](../evidence/aurora-future-owner-accessor-v1.json) records
the normal build, actual Wasm export audit, focused controls, full unit suite
and real-HTTP headless installed-Chrome runs on an Apple M4 Mac mini with 16 GiB.
The native bundle was built at `05d0ca8b9be65dd785de3223313d2b6459b46841`;
the final browser/full-suite harness ran at
`6b6ae24555ddf030e875d1650e5ec3d0fe13a652`, with every native build input
verified unchanged. This entry's final update changes documentation only.

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

The ABI-only build is deliberately separate and uses the repository's
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

Observed validation: normal bootstrap passed in 24.129s and the affected
RelWithDebInfo target built with default compiler jobs in 82.635s. Actual Wasm
parsing found all 24 expected function exports among 84 function exports.
The focused VM/native/order controls passed 3 tests; the required full suite
passed 1,871 tests with 141 skips in 374.194s.

The final real-WebGPU fixture passed in 1.008s on installed Chrome 154.0.8037.97.
It observed adapter/device/work Future IDs 1/2/4 and the queue Promise identity 6
before and after completion, then closed with no pending/late callback and
exactly one owned Destroyed callback. The accessor released its private
references. Ten recorded PIDs and two groups were absent, the HTTP listener
refused connections, the temporary Chrome profile disappeared, source/bundle
hashes remained unchanged, and the 800x300 screenshot was inspected. The first
5.091s run before the publication-order assertion remains retained separately.
These elapsed times describe the diagnostic job, not foreground performance.

To stage the fixture after the normal build, copy the fresh
`build/browser/aurora_future_owner_probe.js` and `.wasm`,
`tests/aurora_future_owner_probe.html`, and
`tests/aurora_future_owner_probe.mjs` into a new owned site directory, naming
the latter `wasm_abi_probe.mjs`. The driver itself stays under `tests/` so its
relative imports resolve. Run the driver under an owned ProcessSupervisor;
the local receipts additionally track Chrome's separately created process
group and audit process/profile/listener absence. Retain the driver JSON and
screenshot on failure; a timeout never authorizes an unchanged retry.
