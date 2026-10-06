"""Exercise probe cleanup after partial init and close failures, without gameplay."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import unittest

from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "tests/gameplay_bootstrap_state_probe.mjs"

MOCK = r'''
const fs = require('node:fs');
const config = CONFIG;
module.exports = async () => {
  let next = 256;
  let live = false;
  const heap = new Uint8Array(8192);
  const view = new DataView(heap.buffer);
  const counts = {init: 0, close: 0, allocations_created: 0, owned_allocations: 0};
  const save = () => fs.writeFileSync(config.counts, JSON.stringify(counts));
  const strings = {1: config.hashes[0], 2: config.hashes[1], 3: config.hashes[2],
                   4: 'injected init or close refusal'};
  return {
    HEAPU8: heap,
    UTF8ToString: ptr => strings[ptr],
    _malloc: size => {const ptr = next; next += size; return ptr;},
    _free: () => {},
    _melee_web_gameplay_bootstrap_state_probe_identity: () => 1,
    _melee_web_gameplay_bootstrap_state_source_c_identity: () => 2,
    _melee_web_gameplay_bootstrap_state_source_h_identity: () => 3,
    _melee_web_gameplay_bootstrap_state_abi_size: () => 128,
    _melee_web_gameplay_bootstrap_state_abi_version: () => 1,
    _melee_web_gameplay_bootstrap_state_schema: () => 0x47504253,
    _melee_web_gameplay_bootstrap_state_capture: (ptr, size) => {
      if (!live || !ptr || size !== 128) return 0;
      heap.fill(0, ptr, ptr + size);
      const u32 = (offset, value) => view.setUint32(ptr + offset, value, true);
      const u64 = (offset, value) => view.setBigUint64(ptr + offset, value, true);
      u32(0, 1); u32(4, 128); u32(8, 0x47504253);
      u64(32, 1n); u64(40, 1n); u64(48, 65536n);
      u32(64, 4096); u32(88, 1);
      return 1;
    },
    _melee_web_snapshot_observation: () => 128,
    _melee_web_snapshot_quiescent: () => 1,
    _melee_web_snapshot_error: () => 4,
    ccall: () => {
      counts.init++;
      counts.allocations_created++;
      counts.owned_allocations++;
      live = true;
      save();
      if (config.mode === 'init_throw') throw new Error('injected init throw');
      if (config.normal_close) {
        view.setUint32(136, 1, true);
        return 1;
      }
      return 0;
    },
    _melee_web_snapshot_close: () => {
      counts.close++; save();
      if (config.mode === 'close_throw') throw new Error('injected close throw');
      if (config.mode === 'close_refused') return 0;
      live = false;
      counts.owned_allocations--;
      save();
      return 1;
    },
  };
};
'''


class BootstrapProbeCleanupTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.node = shutil.which("node")
        if not cls.node:
            raise unittest.SkipTest("Node is needed for diagnostic probe cleanup controls")
        cls.scratch = cls.new_workspace(ROOT, "bootstrap-probe-cleanup-")

    def check_failure(self, mode, success, returncode, normal_close=False):
        directory = self.scratch / mode
        directory.mkdir()
        sources = []
        for name in ("probe.c", "producer.c", "producer.h"):
            file = directory / name
            file.write_text(name + " fixture only\n")
            sources.append(file)
        assets = directory / "assets"
        assets.mkdir()
        counts = directory / "counts.json"
        config = {"mode": mode, "normal_close": normal_close, "counts": str(counts),
                  "hashes": [hashlib.sha256(p.read_bytes()).hexdigest() for p in sources]}
        runtime = directory / "runtime.js"
        runtime.write_text(MOCK.replace("CONFIG", json.dumps(config)))
        runtime.with_suffix(".wasm").write_bytes(b"mock identity only; never executed")
        output = directory / "report"
        result = subprocess.run([
            self.node, str(RUNNER), "--runtime", str(runtime), "--assets", str(assets),
            "--probe-source", str(sources[0]), "--source-c", str(sources[1]),
            "--source-h", str(sources[2]), "--out", str(output),
        ], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 1, result.stderr)
        report = json.loads((output / "bootstrap-state-report.json").read_text())
        self.assertEqual(report["result"], "failed")
        self.assertEqual(json.loads(counts.read_text()), {
            "init": 1, "close": 1, "allocations_created": 1,
            "owned_allocations": 0 if success else 1,
        })
        cleanup = report["cleanup"]
        self.assertTrue(cleanup["required"])
        self.assertTrue(cleanup["attempted"])
        self.assertEqual(cleanup["phase"],
                         "normal source close" if normal_close else "after failure")
        self.assertEqual(cleanup["success"], success)
        self.assertEqual(cleanup["returncode"], returncode)
        self.assertIn("source close failed" if normal_close else "init", report["failure"])
        self.assertEqual(len(report["captures"]), 2 if normal_close else 0)
        if not success:
            self.assertTrue(cleanup["error"])

    def test_partial_init_refusal_is_closed_once(self):
        self.check_failure("init_refused", True, 1)

    def test_throwing_init_is_closed_once(self):
        self.check_failure("init_throw", True, 1)

    def test_refused_close_is_recorded_without_retry(self):
        self.check_failure("close_refused", False, 0, normal_close=True)

    def test_throwing_close_is_recorded_without_retry(self):
        self.check_failure("close_throw", False, None, normal_close=True)


if __name__ == "__main__":
    unittest.main()
