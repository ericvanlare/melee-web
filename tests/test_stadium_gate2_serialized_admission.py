"""Pure retained raw preflight; no browser/native services are executed."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'scripts'), str(ROOT / 'reference-capture/dolphin')]
spec = importlib.util.spec_from_file_location('serialized_admission', ROOT / 'tools/stadium_later_b_reference.py')
reference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)


class SerializedProjectionAdmissionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        names = ('MELEE_GATE2_OLD_BINDING', 'MELEE_GATE2_OLD_PROJECTION',
                 'MELEE_GATE2_NEW_BINDING', 'MELEE_GATE2_NEW_PROJECTION')
        present = [name in os.environ for name in names]
        if not any(present):
            raise unittest.SkipTest('optional exact serialized admission fixtures absent')
        if not all(present) or not all(os.environ[name] for name in names):
            raise ValueError('partial serialized admission fixture configuration')
        cls.paths = [Path(os.environ[name]) for name in names]

    def test_actual_old_and_replay_raw_exporter_and_cli(self):
        for binding, projection in (self.paths[:2], self.paths[2:]):
            value = reference.admit_serialized_projection(binding, projection)
            self.assertFalse(value['runtime_admission'])
            self.assertEqual(value['projection']['sha256'], hashlib.sha256(projection.read_bytes()).hexdigest())
            actual = subprocess.run([sys.executable, str(ROOT / 'tools/stadium_later_b_reference.py'),
                '--admit-serialized-projection', str(binding), str(projection)],
                text=True, capture_output=True, timeout=30, check=True)
            self.assertEqual(json.loads(actual.stdout), value)

    def test_projection_and_binding_mutations_and_symlinks_refuse(self):
        binding, projection = self.paths[2:]
        with tempfile.TemporaryDirectory() as folder:
            d = Path(folder)
            bad = d / 'projection.json'
            noncanonical = d / 'noncanonical.json'
            noncanonical.write_text(json.dumps(reference.build_gate2_projection(binding), indent=2) + '\n')
            self.assertNotEqual(noncanonical.read_bytes(), projection.read_bytes())
            with self.assertRaisesRegex(ValueError, 'strict raw binding regeneration'):
                reference.admit_serialized_projection(binding, noncanonical)
            bad.write_bytes(projection.read_bytes().replace(b'"binding_sha256": "', b'"binding_sha256" :"', 1))
            with self.assertRaisesRegex(ValueError, 'strict raw binding regeneration'):
                reference.admit_serialized_projection(binding, bad)
            value = json.loads(binding.read_bytes())
            value['files']['primary']['sha256'] = '0' * 64
            altered = d / 'binding.json'
            altered.write_text(json.dumps(value))
            with self.assertRaises(ValueError):
                reference.admit_serialized_projection(altered, projection)
            value['files'].pop('primary')
            altered.write_text(json.dumps(value))
            with self.assertRaises(ValueError):
                reference.admit_serialized_projection(altered, projection)
            link = d / 'projection-link.json'
            link.symlink_to(projection)
            with self.assertRaises(ValueError):
                reference.admit_serialized_projection(binding, link)

    def test_actual_js_old_new_same_mwb2_and_negative_bytes(self):
        script = r"""
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {createHash} from 'node:crypto';
import {pathToFileURL} from 'node:url';
const c=await import(pathToFileURL(process.argv[1]));
const [ob,op,nb,np]=process.argv.slice(2).map(p=>fs.readFileSync(p));
const old=c.deriveLaterBGate2Expectations(op), fresh=c.deriveLaterBGate2Expectations(np,nb);
assert.deepEqual(fresh.runtimePayload,old.runtimePayload);
assert.deepEqual(fresh.commonRowsByAction,old.commonRowsByAction);
const token='12345678-1234-4234-8234-123456789abc';
assert.deepEqual(c.encodeLaterBGate2NativeManifest(fresh.runtimePayload,token),
                 c.encodeLaterBGate2NativeManifest(old.runtimePayload,token));
const native={arm:async()=>1,queue:async()=>1,approve:async()=>1};
const legacyDriver=new c.StadiumSetupCfActionDriver({laterBProjection:op,laterBToken:token,native,admittedWorldGeneration:7});
const replayDriver=new c.StadiumSetupCfActionDriver({laterBProjection:np,laterBProjectionBinding:nb,laterBToken:token,native,admittedWorldGeneration:7});
assert.deepEqual(replayDriver.nativeManifest,legacyDriver.nativeManifest);
assert.throws(()=>c.deriveLaterBGate2Expectations(np),/identity differs/);
assert.throws(()=>c.deriveLaterBGate2Expectations(np,ob),/binding identity differs/);
assert.throws(()=>c.deriveLaterBGate2Expectations(np,{sha256:'0'.repeat(64)}),/exact serialized/);
const changed=Buffer.from(np); changed[1000]^=1;
assert.throws(()=>c.deriveLaterBGate2Expectations(changed,nb));
const arbitrary=Buffer.from('{}');
const forged=Buffer.from(np.toString().replace(createHash('sha256').update(nb).digest('hex'),
    createHash('sha256').update(arbitrary).digest('hex')));
// Pure JS only establishes byte equivalence. The actual caller must also run
// the strict exporter, which rejects this arbitrary binding before Chrome.
c.deriveLaterBGate2Expectations(forged,arbitrary);
console.log('old/new exact semantic and MWB2 equivalence; byte negatives passed');
"""
        actual = subprocess.run(['node', '--input-type=module', '-e', script,
            str(ROOT / 'tests/stadium_matched_setup_cf_consumer.mjs'), *map(str, self.paths)],
            text=True, capture_output=True, timeout=30)
        self.assertEqual(actual.returncode, 0, actual.stderr)
        self.assertIn('byte negatives passed', actual.stdout)
        with tempfile.TemporaryDirectory() as folder:
            bad = Path(folder) / 'binding.json'
            bad.write_text('{}')
            with self.assertRaises(ValueError):
                reference.admit_serialized_projection(bad, self.paths[3])

    def test_host_strict_admission_precedes_browser_resource(self):
        source = (ROOT / 'scripts/capture_whole_session_browser.mjs').read_text()
        start = source.index('      if (laterBGate2BindingPath !== null) {')
        self.assertLess(start, source.index('browserTools ||= await loadBrowserTools'))
        self.assertIn("'--admit-serialized-projection'", source[start:])
        self.assertIn('admission.source_binding.sha256!==createHash', source[start:])
        self.assertIn('admission.projection.sha256!==createHash', source[start:])


if __name__ == '__main__':
    unittest.main()
